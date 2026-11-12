// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/render/game_view.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game/resource.hpp"
#include "viceroy/game_files.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <map>

namespace viceroy {

namespace {

Bytes readFile(const std::filesystem::path& folder, std::string_view name) {
    const auto path = findFileNoCase(folder, name);
    if (!path) {
        throw FormatError(std::string(name) + " is missing");
    }
    std::ifstream in(*path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), {});
}

// --- Sprite numbers in PHYS0.SS (see docs/formats/map-drawing.md) ---------
constexpr int kMajorRiver = 0;  // + neighbour mask
constexpr int kMinorRiver = 16; // + neighbour mask
constexpr int kMountains = 32;
constexpr int kHills = 48;
constexpr int kForest = 64;
constexpr int kRoad = 80; // 80 alone, 81-88 towards N, NE, E, ... NW
constexpr int kPlowed = 149;
constexpr int kCoast = 108; // + 4 × group + quadrant; see drawCoast
constexpr int kUnexplored = 148;
constexpr int kGroundBlend = 104; // + side: north, east, south, west
constexpr int kMajorMouth = 140;  // + side the river comes from: north, east, south, west
constexpr int kMinorMouth = 144;
constexpr int kBeach = 150;       // land north-west, north-east, south-west, south-east
constexpr int kScrub = 9;     // desert forest
constexpr int kScrubTile = 8; // TERRAIN.SS: desert with cacti

// --- Sprite numbers in ICONS.SS ------------------------------------------------
constexpr int kColonyOpen = 3; // no stockade; 0 stockade, 1 fort, 2 fortress
constexpr int kSettlementBase = 10; // + the tribe's level
constexpr int kNationFlagBase = 118; // red, blue, yellow, orange pennants
constexpr int kExpertBase = 81;      // colonists by skill

// Colours of the original's text and frames.
constexpr std::uint8_t kBlack = 0x00;
constexpr std::uint8_t kWhite = 0x0F;
constexpr std::uint8_t kGreen = 0x44;
constexpr std::uint8_t kGold = 0x95;
constexpr std::uint8_t kMinimapFrame = 0x06;
// Colonies and units on the small map, seen in the original for the French,
// Spanish and Dutch; the English red is a guess.
constexpr std::array<std::uint8_t, 4> kMinimapNation{0x0C, 0x09, 0x0E, 0x0D};

const IndexedImage* sprite(const SpriteSheet& sheet, int index) {
    if (index < 0 || index >= static_cast<int>(sheet.sprites.size())) {
        return nullptr;
    }
    return &sheet.sprites[static_cast<std::size_t>(index)].image;
}


int baseTile(const MapSquare& sq) {
    switch (sq.terrain) {
    case 24:
        return 9; // arctic
    case 25:
        return 10; // ocean
    case 26:
        return 11; // sea lane
    default:
        // Scrub forest is its own tile, desert with cacti; other forests show
        // the open land under the trees.
        return (sq.terrain & 15) == kScrub ? kScrubTile : sq.terrain & 7;
    }
}

// The open ground of a square: forests (scrub too) stand on the land 8
// lower. Used where a neighbour's ground shows through.
int groundTile(const MapSquare& sq) {
    return sq.terrain < 24 ? sq.terrain & 7 : baseTile(sq);
}

// Neighbour mask used by the joined-up overlays: 1 east, 2 west, 4 south,
// 8 north. Squares off the map count as not matching.
template <typename Pred>
int neighbourMask(const SaveMap& map, int x, int y, Pred pred) {
    auto test = [&](int nx, int ny) { return map.inside(nx, ny) && pred(map.at(nx, ny)); };
    return (test(x + 1, y) ? 1 : 0) | (test(x - 1, y) ? 2 : 0) | (test(x, y + 1) ? 4 : 0) | (test(x, y - 1) ? 8 : 0);
}

// The shore on a water square. Each 8×8 quarter of the square gets a piece
// chosen by its three land neighbours: the side before it going clockwise,
// the corner, and the side after it. Pixel 0 keeps the water, transparent
// pixels show the neighbouring land's ground, everything else is drawn.
// The quarters of a square (top left, top right, bottom right, bottom
// left), each with the neighbours that decide its coast piece.
struct Quarter {
    int ox, oy;
    Step first, corner, second;
};
constexpr Quarter kQuarters[4] = {
    {0, 0, {-1, 0}, {-1, -1}, {0, -1}}, // top left: west, north-west, north
    {8, 0, {0, -1}, {1, -1}, {1, 0}},   // top right: north, north-east, east
    {8, 8, {1, 0}, {1, 1}, {0, 1}},     // bottom right: east, south-east, south
    {0, 8, {0, 1}, {-1, 1}, {-1, 0}},   // bottom left: south, south-west, west
};

// The land whose ground shows through a quarter's open pixels: the first
// side if it is land, else the second, else the corner.
Step coastLandOf(const SaveMap& map, int x, int y, int q) {
    const auto& quarter = kQuarters[q];
    auto land = [&](Step s) { return map.inside(x + s.dx, y + s.dy) && map.at(x + s.dx, y + s.dy).isLand(); };
    return land(quarter.first) ? quarter.first : land(quarter.second) ? quarter.second : quarter.corner;
}

void drawCoast(Screen& screen, const Art& art, const SaveMap& map, int x, int y, int px, int py) {
    auto land = [&](Step s) { return map.inside(x + s.dx, y + s.dy) && map.at(x + s.dx, y + s.dy).isLand(); };
    for (int q = 0; q < 4; ++q) {
        const auto& quarter = kQuarters[q];
        const int group = (land(quarter.first) ? 1 : 0) + (land(quarter.corner) ? 2 : 0) + (land(quarter.second) ? 4 : 0);
        if (group == 0) {
            continue;
        }
        const auto* piece = sprite(art.phys, kCoast + 4 * group + q);
        const Step from = coastLandOf(map, x, y, q);
        const auto* ground = sprite(art.terrain, groundTile(map.at(x + from.dx, y + from.dy)));
        if (piece == nullptr) {
            continue;
        }
        for (int j = 0; j < piece->height && j < 8; ++j) {
            for (int i = 0; i < piece->width && i < 8; ++i) {
                const auto c = piece->pixels[static_cast<std::size_t>(j * piece->width + i)];
                if (c == 0) {
                    continue;
                }
                if (c == kTransparent) {
                    if (ground != nullptr && ground->width == kSquare && ground->height == kSquare) {
                        screen.pixel(px + quarter.ox + i, py + quarter.oy + j,
                                     ground->pixels[static_cast<std::size_t>((quarter.oy + j) * kSquare + quarter.ox + i)]);
                    }
                    continue;
                }
                screen.pixel(px + quarter.ox + i, py + quarter.oy + j, c);
            }
        }
    }
}

// Measured on the original's view-mode captures. A water square that has
// land on two sides and the corner between them, and water on the other two
// sides and the corner between those, gets a sandy beach across it. A water
// square with a river (relief in the save) gets the river's mouth from each
// side where a land square with a river joins it.
void drawBeachAndMouth(Screen& screen, const Art& art, const SaveMap& map, int x, int y, int px, int py) {
    auto land = [&](int dx, int dy) { return map.inside(x + dx, y + dy) && map.at(x + dx, y + dy).isLand(); };
    struct Corner {
        int dx, dy;
    };
    static constexpr Corner kCorners[4] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    static constexpr int kBeachSide[4] = {1, 0, 0, 1};
    for (int c = 0; c < 4; ++c) {
        const int dx = kCorners[c].dx;
        const int dy = kCorners[c].dy;
        if (land(dx, 0) && land(0, dy) && land(dx, dy) && !land(-dx, 0) && !land(0, -dy) && !land(-dx, -dy)) {
            const auto* piece = sprite(art.phys, kBeach + c);
            if (piece == nullptr || piece->width != kSquare || piece->height != kSquare) {
                continue;
            }
            for (int j = 0; j < kSquare; ++j) {
                for (int i = 0; i < kSquare; ++i) {
                    const auto at = static_cast<std::size_t>(j * kSquare + i);
                    auto v = piece->pixels[at];
                    if (v == kTransparent) {
                        const int sx = kBeachSide[c] != 0 ? 0 : dx;
                        const int sy = kBeachSide[c] != 0 ? dy : 0;
                        const auto* ground = sprite(art.terrain, groundTile(map.at(x + sx, y + sy)));
                        if (ground == nullptr || ground->width != kSquare || ground->height != kSquare) {
                            continue;
                        }
                        v = ground->pixels[at];
                    }
                    screen.pixel(px + i, py + j, v);
                }
            }
        }
    }
    const auto& sq = map.at(x, y);
    if (!sq.river()) {
        return;
    }
    static constexpr Step kSides[4] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    for (int side = 0; side < 4; ++side) {
        const int nx = x + kSides[side].dx;
        const int ny = y + kSides[side].dy;
        if (map.inside(nx, ny) && map.at(nx, ny).isLand() && map.at(nx, ny).river()) {
            blitSprite(screen, art.phys, (sq.majorRiver() ? kMajorMouth : kMinorMouth) + side, px, py);
        }
    }
}

// Where two kinds of ground meet, the original lets the neighbour's ground
// show through a sparse dither along the shared side. The dither is in
// PHYS0.SS sprites 104 to 107 (north, east, south, west): pixel 0 takes the
// neighbour's ground at the same spot. The sides are applied in that order,
// so a later side wins in the corners. Found by comparing with captures of
// the original. An unexplored square gets the same dither from the explored
// squares next to it.
void blendGround(Screen& screen, const Art& art, const SaveMap& map, int x, int y, int px, int py, int nation) {
    static constexpr Step kSides[4] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    const auto& sq = map.at(x, y);
    const bool explored = sq.seenBy(nation);
    const int own = explored ? groundTile(sq) : -1;
    for (int side = 0; side < 4; ++side) {
        const int nx = x + kSides[side].dx;
        const int ny = y + kSides[side].dy;
        if (!map.inside(nx, ny)) {
            continue;
        }
        const auto& n = map.at(nx, ny);
        // Explored land takes in other land and water other water (ocean
        // and sea lane); between land and water the shore is drawn on the
        // water square instead. The unexplored dark takes in everything.
        if ((explored && n.isLand() != sq.isLand()) || !n.seenBy(nation) || groundTile(n) == own) {
            continue;
        }
        const auto* mask = sprite(art.phys, kGroundBlend + side);
        const auto* ground = sprite(art.terrain, groundTile(n));
        if (mask == nullptr || ground == nullptr || ground->width != kSquare || ground->height != kSquare) {
            continue;
        }
        for (int j = 0; j < mask->height && j < kSquare; ++j) {
            for (int i = 0; i < mask->width && i < kSquare; ++i) {
                if (mask->pixels[static_cast<std::size_t>(j * mask->width + i)] == 0) {
                    screen.pixel(px + i, py + j, ground->pixels[static_cast<std::size_t>(j * kSquare + i)]);
                }
            }
        }
    }
}

bool forestShown(const MapSquare& sq) {
    return sq.isForest() && (sq.terrain & 15) != kScrub && !sq.hills() && !sq.mountains();
}

} // namespace

namespace {

// PHYS0.SS sprite of each resource (89 to 102), drawn last on its square.
int resourceSprite(Resource r) {
    switch (r) {
    case Resource::DepletedSilver: return 89;
    case Resource::Oasis: return 90;
    case Resource::Grain: return 91;
    case Resource::Cotton: return 92;
    case Resource::Tobacco: return 93;
    case Resource::Sugar: return 94;
    case Resource::Minerals: return 95;
    case Resource::Fish: return 96;
    case Resource::Beaver: return 97;
    case Resource::Deer: return 98;
    case Resource::Timber: return 99;
    case Resource::Silver: return 101;
    case Resource::HillsOre: return 102;
    case Resource::None: break;
    }
    return -1;
}

void drawResource(Screen& screen, const Art& art, const SaveMap& map, int x, int y, int px, int py) {
    const int s = resourceSprite(resourceAt(map, x, y));
    if (s >= 0) {
        blitSprite(screen, art.phys, s, px, py);
    }
}

} // namespace

void drawTerrain(Screen& screen, const Art& art, const Game& game, int x, int y, int px, int py) {
    const auto& map = game.state().map;
    if (!map.inside(x, y)) {
        blitSprite(screen, art.phys, kUnexplored, px, py);
        return;
    }
    const auto& sq = map.at(x, y);
    if (!sq.seenBy(game.human())) {
        blitSprite(screen, art.phys, kUnexplored, px, py);
        blendGround(screen, art, map, x, y, px, py, game.human());
        return;
    }
    blitSprite(screen, art.terrain, baseTile(sq), px, py);
    if (!sq.isLand()) {
        blendGround(screen, art, map, x, y, px, py, game.human());
        drawCoast(screen, art, map, x, y, px, py);
        drawBeachAndMouth(screen, art, map, x, y, px, py);
        drawResource(screen, art, map, x, y, px, py);
        return;
    }
    blendGround(screen, art, map, x, y, px, py, game.human());
    if (sq.flags & MapSquare::kPlowed) {
        blitSprite(screen, art.phys, kPlowed, px, py);
    }
    if (forestShown(sq)) {
        blitSprite(screen, art.phys, kForest + neighbourMask(map, x, y, forestShown), px, py);
    }
    if (sq.river()) {
        const int mask = neighbourMask(map, x, y, [](const MapSquare& n) { return n.river(); });
        blitSprite(screen, art.phys, (sq.majorRiver() ? kMajorRiver : kMinorRiver) + mask, px, py);
    }
    if (sq.hills()) {
        blitSprite(screen, art.phys, kHills + neighbourMask(map, x, y, [](const MapSquare& n) { return n.hills(); }),
                   px, py);
    }
    if (sq.mountains()) {
        blitSprite(screen, art.phys,
                   kMountains + neighbourMask(map, x, y, [](const MapSquare& n) { return n.mountains(); }), px, py);
    }
    if ((sq.flags & MapSquare::kRoad) && !(sq.flags & MapSquare::kHasColony)) {
        bool joined = false;
        for (std::size_t d = 0; d < kSteps.size(); ++d) {
            const int nx = x + kSteps[d].dx;
            const int ny = y + kSteps[d].dy;
            if (map.inside(nx, ny) && (map.at(nx, ny).flags & (MapSquare::kRoad | MapSquare::kHasColony))) {
                blitSprite(screen, art.phys, kRoad + 1 + static_cast<int>(d), px, py);
                joined = true;
            }
        }
        if (!joined) {
            blitSprite(screen, art.phys, kRoad, px, py);
        }
    }
    drawResource(screen, art, map, x, y, px, py);
}

namespace {

// The small box with the unit's nation's colour and its orders letter.
void drawOrdersBox(Screen& screen, const Art& art, const Game& game, int index, Rect box, int orders,
                   bool listInk = false) {
    const auto& u = game.unit(index);
    const std::uint8_t colour = nationColour(art, u.nation);
    screen.fill(box, colour);
    screen.frame(box, kBlack);
    static constexpr const char* kLetters = "-STGLFFBPR-";
    const char letter = (orders >= 0 && orders < 11) ? kLetters[orders] : '-';
    const char text[2] = {letter, 0};
    // A unit with orders shows its letter in the darker half of the
    // nation's colour; a dash is always black (seen for a French dragoon
    // with orders 11 in the combat analysis), and so
    // are the Dutch letters on the map (seen for P and R on orange). In the
    // colony's list of units the Dutch letters are dark orange too, except
    // the F of a unit still fortifying (orders 5), which is black.
    constexpr std::uint8_t kDutchOrange = 0x0D;
    const std::uint8_t ink = letter != '-' && colour >= 8 && colour < 16 &&
                                     (colour != kDutchOrange || (listInk && orders != SaveUnit::kFortify))
                                 ? static_cast<std::uint8_t>(colour - 8)
                                 : kBlack;
    const int w = Screen::textWidth(art.tiny, text);
    screen.text(art.tiny, box.x + (box.w - w) / 2 + 1, box.y + 2, text, {ink, kNoColour, kNoColour});
}

// A unit on its square at (px, py), as measured on the original: the sprite
// a few pixels into the square, a black shadow two pixels left of it, then
// the orders box, then the unit. Where the sprite and the box go depends on
// the picture; these were measured in about 250 captures.
struct UnitPlacement {
    int spriteX;  // from the square's left
    int boxX;     // the orders box, from the square's top-left
    int boxY;
};

UnitPlacement unitPlacement(const Game& game, int index, int icon, int width) {
    switch (icon) {
    case 6: return {3, 0, 0}; // merchantman: the box sits behind the stern
    case 14:
    case 15: return {2, 9, 0}; // privateer, frigate: by the mast
    case 9:                    // artillery
    case 16: return {2, 6, 0}; // treasure
    case 76: return {2, 0, 0}; // dragoon: behind the rider
    case 65: return {2, 6, 2}; // damaged artillery
    default: break;
    }
    const int type = game.unit(index).type;
    if (isShipType(game.rules(), type)) {
        return {2, 9, 0};
    }
    if (type == unit_type::kDragoon || type == unit_type::kScout || type == 7 || type == 8 || type == 21 ||
        type == 22) {
        return {2, 0, 0};
    }
    if (type == unit_type::kWagonTrain) {
        return {2, 6, 0};
    }
    // People: six or seven wide (a pioneer) stand three in, eight wide two
    // in; the box at the feet.
    const int x = width <= 7 ? 3 : 2;
    return {x, x + width - 2, 7};
}

// The black shadow two pixels left of the sprite, the orders box, then the
// sprite.
void drawUnitParts(Screen& screen, const Art& art, const Game& game, int index, const IndexedImage& image, int x,
                   int y, Rect box, int orders, bool listInk) {
    for (int j = 0; j < image.height; ++j) {
        for (int i = 0; i < image.width; ++i) {
            if (image.pixels[static_cast<std::size_t>(j * image.width + i)] != kTransparent) {
                screen.pixel(x - 2 + i, y + j, kBlack);
            }
        }
    }
    drawOrdersBox(screen, art, game, index, box, orders, listInk);
    screen.blit(image, x, y);
}

void drawUnitOnSquare(Screen& screen, const Art& art, const Game& game, int index, int px, int py) {
    const int icon = unitIcon(game, index);
    const auto* image = sprite(art.icons, icon);
    if (image == nullptr || image->width <= 0) {
        return;
    }
    const auto place = unitPlacement(game, index, icon, image->width);
    const Rect box{px + place.boxX, py + place.boxY, 7, 9};
    drawUnitParts(screen, art, game, index, *image, px + place.spriteX, py, box, game.unit(index).orders, false);
}

void drawCentredText(Screen& screen, const Font& font, int cx, int y, const std::string& s, TextColours colours) {
    screen.text(font, cx - Screen::textWidth(font, s) / 2, y, s, colours);
}


std::string lineOf(const Art& art, const char* section, int row, const char* fallback) {
    if (const auto* s = art.names.find(section)) {
        if (row >= 0 && static_cast<std::size_t>(row) < s->lines.size()) {
            const auto fields = splitFields(s->lines[static_cast<std::size_t>(row)]);
            if (!fields.empty()) {
                return fields[0];
            }
        }
    }
    return fallback;
}

std::string terrainName(const Art& art, const MapSquare& sq) {
    std::size_t row = sq.terrain & 7u;
    if (sq.mountains()) {
        row = 19;
    } else if (sq.hills()) {
        row = 20;
    } else if (sq.terrain >= 24) {
        row = 16 + (sq.terrain - 24u);
    } else if (sq.isForest()) {
        row = 8 + (sq.terrain & 7u);
    }
    std::string name = row < art.rules.terrain.size() ? art.rules.terrain[row].name : "?";
    if (sq.majorRiver()) {
        name += "/" + lineOf(art, "OTHER_NAMES", 2, "Major River");
    } else if (sq.minorRiver()) {
        name += "/" + lineOf(art, "OTHER_NAMES", 3, "Minor River");
    }
    return name;
}

std::uint8_t minimapColour(const Art& art, const MapSquare& sq) {
    // The middle pixel of the ground, or of the hills or mountains, stands
    // for the square (forests show their ground). Checked on the original.
    int tile = baseTile(sq);
    const IndexedImage* img = nullptr;
    if (sq.mountains()) {
        img = sprite(art.phys, kMountains);
    } else if (sq.hills()) {
        img = sprite(art.phys, kHills);
    }
    if (img == nullptr || img->pixels.empty()) {
        img = sprite(art.terrain, tile);
    }
    if (img == nullptr || img->pixels.empty()) {
        return kBlack;
    }
    const auto c = img->pixels[static_cast<std::size_t>((img->height / 2) * img->width + img->width / 2)];
    return c == kTransparent ? kBlack : c;
}

} // namespace

void blitSprite(Screen& screen, const SpriteSheet& sheet, int index, int x, int y) {
    if (const auto* img = sprite(sheet, index)) {
        screen.blit(*img, x, y);
    }
}

std::string seasonName(const Art& art, int season) {
    if (const auto* section = art.names.find("SEASONS")) {
        if (season >= 0 && static_cast<std::size_t>(season) < section->lines.size()) {
            return section->lines[static_cast<std::size_t>(season)];
        }
    }
    return season == 0 ? "Spring" : "Autumn";
}

int colonistIcon(int skill) {
    // Colonists in a colony keep 0x1C for a free colonist; units use 0x13.
    // Pioneers, soldiers, scouts and dragoons at work wear plain clothes.
    if (skill >= 20 && skill <= 23) {
        return 38 + skill;
    }
    if (skill == 24) {
        return 66; // a missionary
    }
    return kExpertBase + (skill >= 28 ? 19 : skill);
}

void drawUnitOrders(Screen& screen, const Art& art, const Game& game, int index, int px, int py) {
    drawUnitOnSquare(screen, art, game, index, px, py);
}

void drawSmallUnit(Screen& screen, const Art& art, const Game& game, int index, int px, int py, int right) {
    const int icon = unitIcon(game, index);
    const auto* image = sprite(art.icons, icon);
    if (image == nullptr || image->width <= 0) {
        return;
    }
    // Measured: the picture shrunk to three eighths, sampling rows and
    // columns 1, 4, 7, 9, 12 and 15, centred on px.
    int columns = 0;
    while ((8 * columns + 5) / 3 < image->width) {
        ++columns;
    }
    const int x0 = px - columns / 2;
    for (int i = 0;; ++i) {
        const int sy = (8 * i + 5) / 3;
        if (sy >= image->height) {
            break;
        }
        for (int j = 0;; ++j) {
            const int sx = (8 * j + 5) / 3;
            if (sx >= image->width) {
                break;
            }
            const auto c = image->pixels[static_cast<std::size_t>(sy * image->width + sx)];
            if (c != kTransparent && x0 + j < right) {
                screen.pixel(x0 + j, py + i, c);
            }
        }
    }
}

void drawUnitInColonyList(Screen& screen, const Art& art, const Game& game, int index, int px, int py) {
    int orders = game.unit(index).orders;
    if (orders == SaveUnit::kNoOrders) {
        orders = SaveUnit::kSentry;
    }
    const int icon = unitIcon(game, index);
    const auto* image = sprite(art.icons, icon);
    if (image == nullptr || image->width <= 0) {
        return;
    }
    const auto place = unitPlacement(game, index, icon, image->width);
    drawUnitParts(screen, art, game, index, *image, px + place.spriteX, py, {px + place.boxX, py + place.boxY, 7, 9},
                  orders, true);
}

std::string textLine(const TextFile& file, const char* section, int row, const char* fallback) {
    if (const auto* s = file.find(section)) {
        if (row >= 0 && static_cast<std::size_t>(row) < s->lines.size()) {
            const auto fields = splitFields(s->lines[static_cast<std::size_t>(row)]);
            if (!fields.empty()) {
                return fields[0];
            }
        }
    }
    return fallback;
}

Art loadArt(const std::filesystem::path& folder) {
    Art art;
    art.palette = readVgaPalette(readFile(folder, "VICEROY.PAL"));
    art.terrain = readSpriteSheet(readFile(folder, "TERRAIN.SS"));
    art.phys = readSpriteSheet(readFile(folder, "PHYS0.SS"));
    art.icons = readSpriteSheet(readFile(folder, "ICONS.SS"));
    art.cursor = readSpriteSheet(readFile(folder, "CURSOR.SS"));
    const auto wood = readSpriteSheet(readFile(folder, "WOODTILE.SS"));
    if (!wood.sprites.empty()) {
        art.wood = wood.sprites.front().image;
    }
    art.tiny = readFont(readFile(folder, "FONTTINY.FF"));
    art.small = readFont(readFile(folder, "FONTSMAL.FF"));
    art.labels = readFont(readFile(folder, "FONTINTR.FF"));
    art.names = parseTextFile(readFile(folder, "NAMES.TXT"));
    art.colonyNames = parseTextFile(readFile(folder, "COLONY.TXT"));
    art.labelText = parseTextFile(readFile(folder, "LABELS.TXT"));
    art.gameText = parseTextFile(readFile(folder, "GAME.TXT"));
    art.menus = readMenus(parseTextFile(readFile(folder, "MENU.TXT")));
    placeMenus(art, art.menus);
    art.portraits.push_back(readSpriteSheet(readFile(folder, "MSS0.SS")));
    art.portraits.push_back(readSpriteSheet(readFile(folder, "MSS2.SS")));
    art.portraits.push_back(readSpriteSheet(readFile(folder, "MSS3.SS")));
    art.portraits.push_back(readSpriteSheet(readFile(folder, "MSS4.SS")));
    art.portraits.push_back(readSpriteSheet(readFile(folder, "MSS1.SS")));
    art.portraits.push_back(readSpriteSheet(readFile(folder, "MSS5.SS")));
    art.rules = readRuleset(art.names);
    return art;
}

int minimapTop(const Game& game, const ViewState& view) {
    const auto& map = game.state().map;
    return std::clamp(view.originY + kViewRows / 2 - 20, 0, std::max(0, map.height - 41));
}

std::optional<std::pair<int, int>> minimapSquare(const Game& game, const ViewState& view, int x, int y) {
    const auto& map = game.state().map;
    // Measured on the original: a click lands one square up and to the left
    // of the square drawn under the pointer's tip.
    const int mx = x - kMinimapArea.x - 1;
    const int my = minimapTop(game, view) + y - kMinimapArea.y - 1;
    if (x < kMinimapArea.x || x >= kMinimapArea.x + kMinimapArea.w || y < kMinimapArea.y ||
        y >= kMinimapArea.y + kMinimapArea.h || !map.inside(mx, my)) {
        return std::nullopt;
    }
    return std::make_pair(mx, my);
}

void centreOn(ViewState& view, const Game& game, int x, int y) {
    const auto& map = game.state().map;
    // The outermost row and column of the map are a border the view never
    // shows (the original stops at row 1 and at the second-last row).
    view.originX = std::clamp(x - kViewColumns / 2, 1, std::max(1, map.width - 1 - kViewColumns));
    view.originY = std::clamp(y - kViewRows / 2, 1, std::max(1, map.height - 1 - kViewRows));
}

std::uint8_t nationColour(const Art& art, int nation) {
    // Measured on the original (orders boxes, the small map, missions):
    // French 0x09, Spanish 0x0E, Dutch 0x0D. The English red is a guess.
    static constexpr std::uint8_t kNations[kEuropeanNations] = {0x0C, 0x09, 0x0E, 0x0D};
    if (nation >= 0 && nation < kEuropeanNations) {
        return kNations[nation];
    }
    const auto tribe = static_cast<std::size_t>(nation - kEuropeanNations);
    if (tribe < art.rules.tribes.size()) {
        return static_cast<std::uint8_t>(art.rules.tribes[tribe].colour);
    }
    return kWhite;
}

int unitIcon(const Game& game, int index) {
    const auto& u = game.unit(index);
    const auto& types = game.rules().units;
    if (u.type == unit_type::kColonist && u.profession >= 0 && u.profession <= 28) {
        return colonistIcon(u.profession);
    }
    if (u.type < 0 || u.type >= static_cast<int>(types.size())) {
        return 0;
    }
    // Measured in the combat analysis: damaged artillery has its own,
    // smaller picture.
    constexpr int kDamagedArtilleryIcon = 65;
    if (game.isDamagedArtillery(index)) {
        return kDamagedArtilleryIcon;
    }
    // @UNIT gives an icon number, counted from 1. That is the expert's
    // picture; anyone else armed, mounted, with tools or as a missionary
    // shows the plainer one 28 sprites before it (seen on the original:
    // a soldier who is no veteran wears no blue coat).
    const int icon = types[static_cast<std::size_t>(u.type)].icon - 1;
    int expert = -1;
    switch (u.type) {
    case unit_type::kSoldier:
    case unit_type::kDragoon: expert = 0x15; break;
    case unit_type::kPioneer: expert = 0x14; break;
    case unit_type::kScout: expert = 0x16; break;
    case unit_type::kMissionary: expert = 0x18; break;
    default: return icon;
    }
    return u.profession == expert ? icon : icon - 28;
}

namespace {
bool settlementAt(const SaveGame& save, int x, int y) {
    for (const auto& s : save.settlements) {
        if (s.x == x && s.y == y) {
            return true;
        }
    }
    return false;
}
} // namespace

void drawMap(Screen& screen, const Art& art, const Game& game, const ViewState& view) {
    screen.setClip(kMapArea);
    const auto& save = game.state();
    for (int row = 0; row < kViewRows; ++row) {
        for (int col = 0; col < kViewColumns; ++col) {
            drawTerrain(screen, art, game, view.originX + col, view.originY + row, kMapArea.x + col * kSquare,
                       kMapArea.y + row * kSquare);
        }
    }
    auto visible = [&](int x, int y) {
        return save.map.inside(x, y) && save.map.at(x, y).seenBy(game.human()) && x >= view.originX &&
               y >= view.originY && x < view.originX + kViewColumns && y < view.originY + kViewRows;
    };
    auto screenX = [&](int x) { return kMapArea.x + (x - view.originX) * kSquare; };
    auto screenY = [&](int y) { return kMapArea.y + (y - view.originY) * kSquare; };

    for (const auto& s : save.settlements) {
        if (visible(s.x, s.y)) {
            const auto tribe = static_cast<std::size_t>(s.nation - kEuropeanNations);
            const int level = tribe < art.rules.tribes.size() ? art.rules.tribes[tribe].level : 0;
            // Centred on the square like a colony.
            const int px = screenX(s.x);
            const int py = screenY(s.y);
            if (const auto* image = sprite(art.icons, kSettlementBase + std::clamp(level, 0, 3))) {
                screen.blit(*image, px + (kSquare - image->width) / 2, py);
            }
            // A mission: a cross in the colour of the nation that sent it on
            // a black plate (byte 5: 0xFF none, else the nation in the low
            // four bits). Seen on the original for a French mission.
            const int mission = s.raw[5];
            if (mission != 0xFF && (mission & 15) < kEuropeanNations) {
                const std::uint8_t colour = nationColour(art, mission & 15);
                // The plate's rim, colours 0x88 and 0x93, as seen.
                screen.pixel(px + 8, py + 4, 0x88);
                for (int j = 6; j <= 10; ++j) {
                    const std::uint8_t rim = j == 8 ? 0x93 : 0x88;
                    screen.pixel(px + 5, py + j, rim);
                    screen.pixel(px + 11, py + j, rim);
                }
                for (int i = 6; i <= 10; ++i) {
                    screen.pixel(px + i, py + 11, i == 8 ? 0x93 : 0x88);
                }
                screen.fill({px + 6, py + 5, 5, 6}, kBlack);
                screen.fill({px + 8, py + 6, 1, 4}, colour);
                screen.fill({px + 7, py + 7, 3, 1}, colour);
            }
        }
    }

    // Colonies, each with its name, in the order of the save; a later one
    // covers the end of an earlier name, and units cover them all.
    for (const auto& c : save.colonies) {
        if (!visible(c.x, c.y)) {
            continue;
        }
        const int px = screenX(c.x);
        const int py = screenY(c.y);
        const int fortification = c.raw[132] & 7;
        const int icon = fortification == 0 ? kColonyOpen : fortification == 1 ? 0 : fortification == 3 ? 1 : 2;
        // The picture is 21 wide, centred on the square; its pennant is
        // overdrawn with the owner's.
        if (const auto* image = sprite(art.icons, icon)) {
            screen.blit(*image, px + (kSquare - image->width) / 2, py);
        }
        blitSprite(screen, art.icons, kNationFlagBase + std::clamp(c.nation, 0, kEuropeanNations - 1), px + 3, py);
        const std::string population = std::to_string(c.population);
        drawCentredText(screen, art.tiny, px + 8, py + 5, population, {kWhite, kBlack, kBlack});
        screen.text(art.labels, px + 2, py + 16, c.name, {kWhite, kBlack, kBlack});
    }
    // Units: the top one on each square (the active unit wins its square).
    std::map<std::pair<int, int>, int> top;
    auto shown = [&](int i) {
        const auto& u = game.unit(i);
        // Units inside a colony or a native settlement are not drawn, nor
        // another nation's units the player cannot see: byte 3 of a unit has
        // a bit per nation (4 + nation) for who sees it, as the "seen" map
        // layer does.
        const bool seen = u.nation == game.human() || ((u.raw[3] >> (4 + game.human())) & 1) != 0;
        return seen && game.onMap(i) && !game.isAboard(i) && !game.colonyAt(u.x, u.y) && !settlementAt(save, u.x, u.y);
    };
    const int sliding = view.slideUnit >= 0 && view.slideUnit < game.unitCount() ? view.slideUnit : -1;
    for (int i = 0; i < game.unitCount(); ++i) {
        if (shown(i) && i != sliding) {
            // Units on one square form a chain (bytes 24-25 point to the
            // next one); the original shows the last of the chain, the unit
            // with no next (checked on three stacks of COLONY00).
            const auto& raw = game.unit(i).raw;
            const bool last = raw[24] == 0xFF && raw[25] == 0xFF;
            const auto key = std::make_pair(game.unit(i).x, game.unit(i).y);
            const auto it = top.find(key);
            if (it == top.end() || last) {
                top[key] = i;
            }
        }
    }
    const int active = game.activeUnit();
    if (active >= 0 && active < game.unitCount() && shown(active) && active != sliding) {
        top[std::make_pair(game.unit(active).x, game.unit(active).y)] = active;
    }
    for (const auto& [pos, i] : top) {
        if (!visible(pos.first, pos.second) || (i == game.activeUnit() && !view.blinkOn)) {
            continue;
        }
        const int px = screenX(pos.first);
        const int py = screenY(pos.second);
        drawUnitOnSquare(screen, art, game, i, px, py);
    }
    if (sliding >= 0 && game.onMap(sliding)) {
        const auto& u = game.unit(sliding);
        const int px = screenX(u.x) + view.slideX;
        const int py = screenY(u.y) + view.slideY;
        drawUnitOnSquare(screen, art, game, sliding, px, py);
    }

    screen.resetClip();
}

void drawMenuBar(Screen& screen, const Art& art) {
    drawMenuBar(screen, art, art.menus, nullptr);
}

void drawPanel(Screen& screen, const Art& art, const Game& game, const ViewState& view) {
    const auto& save = game.state();
    screen.fill({240, 8, 1, 192}, kBlack);
    screen.tile(art.wood, kPanelArea);

    // The small map of the world, 58 squares wide and 40 high around the view.
    const Rect mini = kMinimapArea;
    screen.frame({mini.x, mini.y, mini.w, mini.h}, kMinimapFrame);
    const int top = minimapTop(game, view);
    // Who is on each square: colonies and units in their nation's colour,
    // settlements in their tribe's.
    std::map<std::pair<int, int>, std::uint8_t> marks;
    auto owner = [&](int nation) -> std::uint8_t {
        if (nation >= 0 && nation < kEuropeanNations) {
            return kMinimapNation[static_cast<std::size_t>(nation)];
        }
        const auto tribe = static_cast<std::size_t>(nation - kEuropeanNations);
        return tribe < art.rules.tribes.size() ? static_cast<std::uint8_t>(art.rules.tribes[tribe].colour) : kWhite;
    };
    for (const auto& s : save.settlements) {
        marks[{s.x, s.y}] = owner(s.nation);
    }
    for (int i = 0; i < game.unitCount(); ++i) {
        if (game.onMap(i)) {
            marks[{game.unit(i).x, game.unit(i).y}] = owner(game.unit(i).nation);
        }
    }
    for (const auto& c : save.colonies) {
        marks[{c.x, c.y}] = owner(c.nation);
    }
    // Inside the frame: map columns 1 to 56, and 39 rows below the top one.
    for (int y = mini.y + 1; y < mini.y + mini.h - 1; ++y) {
        for (int x = mini.x + 1; x < mini.x + mini.w - 1; ++x) {
            const int mx = x - mini.x;
            const int my = top + (y - mini.y);
            std::uint8_t c = kBlack;
            if (save.map.inside(mx, my) && save.map.at(mx, my).seenBy(game.human())) {
                const auto mark = marks.find({mx, my});
                c = mark != marks.end() ? mark->second : minimapColour(art, save.map.at(mx, my));
            }
            screen.pixel(x, y, c);
        }
    }
    screen.frame({mini.x + view.originX, mini.y + view.originY - top, kViewColumns, kViewRows}, kWhite);
    screen.fill({241, 49, 79, 1}, kBlack);

    const TextColours green{kGreen, kNoColour, kNoColour};
    const TextColours gold{kGold, kNoColour, kNoColour};
    const int year = view.shownYear >= 0 ? view.shownYear : save.year;
    const int season = view.shownYear >= 0 ? view.shownSeason : save.season;
    screen.text(art.tiny, 242, 51, seasonName(art, season) + " " + std::to_string(year), green);
    const auto& nation = save.nations[static_cast<std::size_t>(game.human())];
    screen.text(art.tiny, 242, 58,
                "Gold:" + std::to_string(nation.gold) + "$  Tax: " + std::to_string(nation.taxRate) + "%", green);

    const int active = game.activeUnit();
    if (active >= 0 && active < game.unitCount()) {
        const auto& u = game.unit(active);
        drawUnitOnSquare(screen, art, game, active, 242, 68);
        const int left = game.movesLeft(active);
        std::string moves = std::to_string(left / 3);
        if (left % 3 != 0) {
            moves += (left / 3 > 0 ? " " : "") + std::to_string(left % 3) + "/3";
            if (left / 3 == 0) {
                moves = std::to_string(left % 3) + "/3";
            }
        }
        screen.text(art.tiny, 260, 70, "Moves: " + moves, green);
        screen.text(art.tiny, 260, 77, "Locat: (" + std::to_string(u.x) + ", " + std::to_string(u.y) + ")", green);
        const std::string nationName = lineOf(art, "NATIONALITY", u.nation, "");
        screen.text(art.tiny, 242, 86, (nationName.empty() ? "" : nationName + " ") + game.unitName(active), green);
        // Then a line each, 7 apart (seen for a pioneer): a colonist's
        // skill and a pioneer's tools in gold, the orders, the square's
        // terrain and its road and plowed field.
        int y = 93;
        auto line = [&](const std::string& text, const TextColours& colours) {
            screen.text(art.tiny, 242, y, text, colours);
            y += 7;
        };
        if (!game.isShip(active) && u.type < unit_type::kTreasure) {
            // 0x1C, the free colonist, past the end of @JOB, reads as its
            // "Colonist" row.
            constexpr std::size_t kColonistRow = 19;
            const auto row = static_cast<std::size_t>(u.profession) < 28 ? static_cast<std::size_t>(u.profession)
                                                                        : kColonistRow;
            if (row < art.rules.jobs.size()) {
                line(art.rules.jobs[row].name, gold);
            }
        }
        if (u.type == unit_type::kPioneer) {
            line("(" + std::to_string(u.tools) + " Tools)", gold);
        }
        line(lineOf(art, "ORDERS", u.orders, "No Orders"), gold);
        if (save.map.inside(u.x, u.y)) {
            const auto& sq = save.map.at(u.x, u.y);
            line("(" + terrainName(art, sq) + ")", green);
            // "Road" and "Plowed" in the MISC section of LABELS.TXT.
            if ((sq.flags & MapSquare::kRoad) != 0) {
                line("(" + textLine(art.labelText, "MISC", 31, "Road") + ")", green);
            }
            if ((sq.flags & MapSquare::kPlowed) != 0) {
                line("(" + textLine(art.labelText, "MISC", 83, "Plowed") + ")", green);
            }
        }
    }

    if ((active < 0 || active >= game.unitCount()) && save.map.inside(view.lookX, view.lookY)) {
        drawSquarePanel(screen, art, game, view);
    }

    if (!view.message.empty()) {
        // Wrapped onto lines of the panel's width.
        int y = 180;
        std::string line;
        std::string word;
        auto flush = [&]() {
            screen.text(art.tiny, 242, y, line, gold);
            y += 7;
            line.clear();
        };
        for (std::size_t i = 0; i <= view.message.size(); ++i) {
            const char ch = i < view.message.size() ? view.message[i] : ' ';
            if (ch != ' ') {
                word.push_back(ch);
                continue;
            }
            const std::string next = line.empty() ? word : line + " " + word;
            if (Screen::textWidth(art.tiny, next) > 76 && !line.empty()) {
                flush();
                line = word;
            } else {
                line = next;
            }
            word.clear();
        }
        if (!line.empty()) {
            flush();
        }
    }
}

void drawSquarePanel(Screen& screen, const Art& art, const Game& game, const ViewState& view) {
    // Measured at the end of a turn, a pioneer at work on plains: the
    // square, then each unit on it with its tools and orders, then "End of
    // Turn" in white.
    const auto& save = game.state();
    const auto& sq = save.map.at(view.lookX, view.lookY);
    const TextColours green{kGreen, kNoColour, kNoColour};
    const TextColours gold{kGold, kNoColour, kNoColour};
    int y = 68;
    auto line = [&](const std::string& text) {
        screen.text(art.tiny, 242, y, text, green);
        y += 7;
    };
    // Guess: the number after the place is the region from the path layer.
    line("Locat: (" + std::to_string(view.lookX) + ", " + std::to_string(view.lookY) + ") " +
         std::to_string(sq.region & 0x0F));
    const auto human = static_cast<std::size_t>(game.human());
    if (human < save.players.size() && sq.isLand()) {
        line(save.players[human].country);
    }
    line("(" + terrainName(art, sq) + ")");
    if ((sq.flags & MapSquare::kRoad) != 0) {
        line("(" + textLine(art.labelText, "MISC", 31, "Road") + ")");
    }
    if ((sq.flags & MapSquare::kPlowed) != 0) {
        line("(" + textLine(art.labelText, "MISC", 83, "Plowed") + ")");
    }
    y += 3;
    // A ship comes first with the people aboard after it, in the order of
    // its cargo chain from the last one loaded (seen: merchantman, veteran,
    // pioneer); then the rest.
    std::vector<int> order;
    const auto here = game.unitsAt(view.lookX, view.lookY);
    for (const int i : here) {
        if (std::find(order.begin(), order.end(), i) != order.end()) {
            continue;
        }
        order.push_back(i);
        for (int next = game.unit(i).prevInTransport; next >= 0 && next < game.unitCount() &&
                                                       std::find(order.begin(), order.end(), next) == order.end();
             next = game.unit(next).prevInTransport) {
            if (std::find(here.begin(), here.end(), next) != here.end()) {
                order.push_back(next);
            }
        }
    }
    for (const int i : order) {
        const auto& u = game.unit(i);
        drawUnitOnSquare(screen, art, game, i, 242, y);
        std::string name = game.unitName(i);
        if (u.type == unit_type::kPioneer) {
            name = std::to_string(u.tools) + " Tools";
        } else if ((u.type == unit_type::kSoldier || u.type == unit_type::kDragoon) && u.profession == 0x15) {
            name = textLine(art.labelText, "MISC", 65, "Veteran");
        }
        screen.text(art.tiny, 260, y + 4, name, gold);
        screen.text(art.tiny, 260, y + 10, lineOf(art, "ORDERS", u.orders, "No Orders"), gold);
        y += 18;
        if (y > 160) {
            break;
        }
    }
    if (view.endOfTurn) {
        screen.text(art.tiny, 242, y + 7, "End of Turn", {kWhite, kNoColour, kNoColour});
    }
}

void drawGameScreen(Screen& screen, const Art& art, const Game& game, const ViewState& view) {
    screen.clear(kBlack);
    drawMenuBar(screen, art);
    drawMap(screen, art, game, view);
    drawPanel(screen, art, game, view);
}

} // namespace viceroy
