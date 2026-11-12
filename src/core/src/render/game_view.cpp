// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/render/game_view.hpp"

#include "viceroy/data/reader.hpp"
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
void drawCoast(Screen& screen, const Art& art, const SaveMap& map, int x, int y, int px, int py) {
    struct Quarter {
        int ox, oy;
        Step first, corner, second;
    };
    static constexpr Quarter kQuarters[4] = {
        {0, 0, {-1, 0}, {-1, -1}, {0, -1}}, // top left: west, north-west, north
        {8, 0, {0, -1}, {1, -1}, {1, 0}},   // top right: north, north-east, east
        {8, 8, {1, 0}, {1, 1}, {0, 1}},     // bottom right: east, south-east, south
        {0, 8, {0, 1}, {-1, 1}, {-1, 0}},   // bottom left: south, south-west, west
    };
    auto land = [&](Step s) { return map.inside(x + s.dx, y + s.dy) && map.at(x + s.dx, y + s.dy).isLand(); };
    for (int q = 0; q < 4; ++q) {
        const auto& quarter = kQuarters[q];
        const int group = (land(quarter.first) ? 1 : 0) + (land(quarter.corner) ? 2 : 0) + (land(quarter.second) ? 4 : 0);
        if (group == 0) {
            continue;
        }
        const auto* piece = sprite(art.phys, kCoast + 4 * group + q);
        // The land whose ground shows through: a side first, then the corner.
        const Step from = land(quarter.first) ? quarter.first : land(quarter.second) ? quarter.second : quarter.corner;
        const auto* ground = sprite(art.terrain, baseTile(map.at(x + from.dx, y + from.dy)));
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
    const int own = explored ? baseTile(sq) : -1;
    for (int side = 0; side < 4; ++side) {
        const int nx = x + kSides[side].dx;
        const int ny = y + kSides[side].dy;
        if (!map.inside(nx, ny)) {
            continue;
        }
        const auto& n = map.at(nx, ny);
        // Explored ground takes in other land only (the shore is drawn on
        // the water square); the unexplored dark takes in water too.
        if ((explored && !n.isLand()) || !n.seenBy(nation) || baseTile(n) == own) {
            continue;
        }
        const auto* mask = sprite(art.phys, kGroundBlend + side);
        const auto* ground = sprite(art.terrain, baseTile(n));
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
        drawCoast(screen, art, map, x, y, px, py);
        return;
    }
    blendGround(screen, art, map, x, y, px, py, game.human());
    if (sq.flags & MapSquare::kPlowed) {
        blitSprite(screen, art.phys, kPlowed, px, py);
    }
    if (sq.river()) {
        const int mask = neighbourMask(map, x, y, [](const MapSquare& n) { return n.river() || n.isWater(); });
        blitSprite(screen, art.phys, (sq.majorRiver() ? kMajorRiver : kMinorRiver) + mask, px, py);
    }
    if (forestShown(sq)) {
        blitSprite(screen, art.phys, kForest + neighbourMask(map, x, y, forestShown), px, py);
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
}

namespace {

// The small box beside a unit with its nation's colour and its orders.
void drawOrdersBox(Screen& screen, const Art& art, const Game& game, int index, int px, int py) {
    const auto& u = game.unit(index);
    const Rect box{px + 9, py, 7, 9};
    screen.fill(box, nationColour(art, u.nation));
    screen.frame(box, kBlack);
    static constexpr const char* kLetters = "-STGLFFBPR-";
    const char letter = (u.orders >= 0 && u.orders < 11) ? kLetters[u.orders] : '-';
    const char text[2] = {letter, 0};
    const int w = Screen::textWidth(art.tiny, text);
    screen.text(art.tiny, box.x + (box.w - w) / 2 + 1, box.y + 2, text, {kBlack, kBlack, kBlack});
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
    // The middle pixel of the square's picture stands for it.
    int tile = baseTile(sq);
    const IndexedImage* img = nullptr;
    if (sq.mountains()) {
        img = sprite(art.phys, kMountains);
    } else if (sq.hills()) {
        img = sprite(art.phys, kHills);
    } else if (forestShown(sq)) {
        img = sprite(art.phys, kForest);
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
    drawOrdersBox(screen, art, game, index, px, py);
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
    art.rules = readRuleset(art.names);
    return art;
}

int minimapTop(const Game& game, const ViewState& view) {
    const auto& map = game.state().map;
    return std::clamp(view.originY + kViewRows / 2 - 20, 0, std::max(0, map.height - 40));
}

std::optional<std::pair<int, int>> minimapSquare(const Game& game, const ViewState& view, int x, int y) {
    const auto& map = game.state().map;
    const int mx = x - (kMinimapArea.x + 1);
    const int my = minimapTop(game, view) + y - kMinimapArea.y;
    if (x < kMinimapArea.x || x >= kMinimapArea.x + kMinimapArea.w || y < kMinimapArea.y ||
        y >= kMinimapArea.y + kMinimapArea.h || !map.inside(mx, my)) {
        return std::nullopt;
    }
    return std::make_pair(mx, my);
}

void centreOn(ViewState& view, const Game& game, int x, int y) {
    const auto& map = game.state().map;
    view.originX = std::clamp(x - kViewColumns / 2, 0, std::max(0, map.width - kViewColumns));
    view.originY = std::clamp(y - kViewRows / 2, 0, std::max(0, map.height - kViewRows));
}

std::uint8_t nationColour(const Art& art, int nation) {
    if (nation >= 0 && nation < kEuropeanNations) {
        if (const auto* img = sprite(art.icons, kNationFlagBase + nation)) {
            // The pennant's most used colour.
            std::array<int, 256> count{};
            for (const auto c : img->pixels) {
                if (c != kTransparent && c != kBlack) {
                    ++count[c];
                }
            }
            return static_cast<std::uint8_t>(std::max_element(count.begin(), count.end()) - count.begin());
        }
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
    // @UNIT gives an icon number, counted from 1.
    return types[static_cast<std::size_t>(u.type)].icon - 1;
}

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
            blitSprite(screen, art.icons, kSettlementBase + std::clamp(level, 0, 3), screenX(s.x), screenY(s.y));
        }
    }

    // Units: the top one on each square (the active unit wins its square).
    std::map<std::pair<int, int>, int> top;
    auto shown = [&](int i) {
        const auto& u = game.unit(i);
        return game.onMap(i) && !game.isAboard(i) && !game.colonyAt(u.x, u.y);
    };
    const int sliding = view.slideUnit >= 0 && view.slideUnit < game.unitCount() ? view.slideUnit : -1;
    for (int i = 0; i < game.unitCount(); ++i) {
        if (shown(i) && i != sliding) {
            top.emplace(std::make_pair(game.unit(i).x, game.unit(i).y), i);
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
        blitSprite(screen, art.icons, unitIcon(game, i), px, py);
        drawOrdersBox(screen, art, game, i, px, py);
    }
    if (sliding >= 0 && game.onMap(sliding)) {
        const auto& u = game.unit(sliding);
        const int px = screenX(u.x) + view.slideX;
        const int py = screenY(u.y) + view.slideY;
        blitSprite(screen, art.icons, unitIcon(game, sliding), px, py);
        drawOrdersBox(screen, art, game, sliding, px, py);
    }

    for (const auto& c : save.colonies) {
        if (!visible(c.x, c.y)) {
            continue;
        }
        const int px = screenX(c.x);
        const int py = screenY(c.y);
        const int fortification = c.raw[132] & 7;
        const int icon = fortification == 0 ? kColonyOpen : fortification == 1 ? 0 : fortification == 3 ? 1 : 2;
        blitSprite(screen, art.icons, icon, px, py);
        const std::string population = std::to_string(c.population);
        drawCentredText(screen, art.tiny, px + 8, py + 5, population, {kWhite, kBlack, kBlack});
        screen.text(art.labels, px + 2, py + 16, c.name, {kWhite, kBlack, kBlack});
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
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < std::min(58, save.map.width); ++x) {
            const int mx = x;
            const int my = top + y;
            std::uint8_t c = kBlack;
            if (save.map.inside(mx, my) && save.map.at(mx, my).seenBy(game.human())) {
                c = minimapColour(art, save.map.at(mx, my));
                if (save.map.at(mx, my).flags & MapSquare::kHasColony) {
                    c = kWhite;
                }
            }
            screen.pixel(mini.x + 1 + x, mini.y + y, c);
        }
    }
    screen.frame({mini.x + 1 + view.originX, mini.y + view.originY - top, kViewColumns, kViewRows}, kWhite);
    screen.fill({241, 49, 79, 1}, kBlack);

    const TextColours green{kGreen, kNoColour, kNoColour};
    const TextColours gold{kGold, kNoColour, kNoColour};
    screen.text(art.tiny, 242, 51, seasonName(art, save.season) + " " + std::to_string(save.year), green);
    const auto& nation = save.nations[static_cast<std::size_t>(game.human())];
    screen.text(art.tiny, 242, 58,
                "Gold: " + std::to_string(nation.gold) + "$  Tax: " + std::to_string(nation.taxRate) + "%", green);

    const int active = game.activeUnit();
    if (active >= 0 && active < game.unitCount()) {
        const auto& u = game.unit(active);
        blitSprite(screen, art.icons, unitIcon(game, active), 241, 68);
        drawOrdersBox(screen, art, game, active, 241, 68);
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
        const std::string orders = lineOf(art, "ORDERS", u.orders, "No Orders");
        screen.text(art.tiny, 242, 93, orders, gold);
        if (save.map.inside(u.x, u.y)) {
            screen.text(art.tiny, 242, 100, "(" + terrainName(art, save.map.at(u.x, u.y)) + ")", green);
        }
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

void drawGameScreen(Screen& screen, const Art& art, const Game& game, const ViewState& view) {
    screen.clear(kBlack);
    drawMenuBar(screen, art);
    drawMap(screen, art, game, view);
    drawPanel(screen, art, game, view);
}

} // namespace viceroy
