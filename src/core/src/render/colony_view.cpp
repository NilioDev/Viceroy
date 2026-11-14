// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/render/colony_view.hpp"

#include "viceroy/data/image.hpp"
#include "viceroy/data/reader.hpp"
#include "viceroy/game/colony.hpp"
#include "viceroy/game/natives.hpp"
#include "viceroy/game_files.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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

constexpr std::uint8_t kBlack = 0x00;
constexpr std::uint8_t kWhite = 0x0F;
constexpr std::uint8_t kGreen = 0x44;
constexpr std::uint8_t kSelectGreen = 0x45;
constexpr std::uint8_t kBrightGreen = 0x0A;
constexpr int kExpertIcons = 81;
constexpr std::uint8_t kHarbourText = 0x39;
constexpr std::uint8_t kStockText = 0x3D;
constexpr std::uint8_t kStockHundreds = 0x94;
constexpr std::uint8_t kStockOver = 0x04;
constexpr std::uint8_t kRed = 0x0C;
constexpr std::uint8_t kGold = 0x95;
constexpr std::uint8_t kMenuBar = 0x8A;
constexpr std::uint8_t kMenuGrey = 0x08;   // a job the warehouse cannot equip
constexpr std::uint8_t kPanelInk = 0x39;   // the Make panel: words and the dark edge of buttons
constexpr std::uint8_t kPanelLight = 0x30; // the light edge of buttons
constexpr std::uint8_t kButtonShadow = 0x3F;
constexpr std::uint8_t kUnitFrame = 0x0A;     // bright green
constexpr std::uint8_t kSelectedFrame = 0x0F; // white: the unit clicked

// Screen areas.
constexpr Rect kTown{0, 8, 199, 120};
constexpr Rect kSide{200, 8, 120, 120};
constexpr Rect kAreaFrame{223, 31, 74, 74};
constexpr int kAreaSquare = 24;
constexpr int kLowerTop = 128;

// Sprites in ICONS.SS.
constexpr int kGoodsIcon = 22;       // + cargo row, food to muskets
constexpr int kHammerIcon = 54;
constexpr int kCrossIcon = 56;
constexpr int kBellIcon = 62;
constexpr int kCrossedOut = 55;
constexpr int kFish = 57;
constexpr int kButtonProduction = 67;
constexpr int kButtonUnits = 68;
constexpr int kButtonBuild = 69;
constexpr int kEmptyHold = 122;
constexpr int kRebelFlag = 123;
constexpr int kCrown = 124;

int goodIcon(int good) {
    if (good == cargo::kHammers) {
        return kHammerIcon;
    }
    if (good == cargo::kCrosses) {
        return kCrossIcon;
    }
    if (good == cargo::kBells) {
        return kBellIcon;
    }
    return kGoodsIcon + good;
}

// Each kind of building, its place among the spots of its size, and the
// sprites of BUILDING.SS for "nothing built here" and each level. The places
// count in the order the kinds first appear in NAMES.TXT's @BUILDING.
struct BuildingSlot {
    BuildingKind kind;
    int place;
    int empty;
    std::array<int, 3> levels;
    int x = 0; // where this colony puts it
    int y = 0;
    int size = 0; // 0 small, 1 wide, 2 large, 3 the stockade's, 4 the docks'
};

constexpr int kFillerLarge = 42;
constexpr int kFillerWide = 43;
constexpr int kFillerSmall = 44;
constexpr int kFillerShore = 45;
constexpr int kNoStockade = 16;
constexpr int kWarehouse = 15;
constexpr int kStables = 46;
constexpr int kWarehouseAndStables = 47;

constexpr std::array<BuildingSlot, 15> kSlots{{
    {BuildingKind::Docks, 14, kFillerShore, {6, 7, 8}},
    {BuildingKind::Armory, 7, kFillerWide, {3, 4, 5}},
    {BuildingKind::Fortification, 13, kNoStockade, {0, 1, 2}},
    {BuildingKind::TownHall, 11, kFillerLarge, {9, 9, 9}},
    {BuildingKind::Schoolhouse, 8, kFillerWide, {12, 13, 14}},
    {BuildingKind::Warehouse, 9, kFillerWide, {kWarehouse, kWarehouse, kWarehouse}},
    {BuildingKind::CustomHouse, 0, kFillerSmall, {18, 18, 18}},
    {BuildingKind::PrintingPress, 1, kFillerSmall, {19, 20, 20}},
    {BuildingKind::Weaver, 2, kFillerSmall, {21, 22, 23}},
    {BuildingKind::Tobacconist, 3, kFillerSmall, {24, 25, 26}},
    {BuildingKind::Distiller, 4, kFillerSmall, {27, 28, 29}},
    {BuildingKind::FurTrader, 5, kFillerSmall, {32, 33, 34}},
    {BuildingKind::Carpenter, 10, kFillerWide, {35, 36, 36}},
    {BuildingKind::Church, 12, kFillerLarge, {37, 38, 38}},
    {BuildingKind::Blacksmith, 6, kFillerSmall, {39, 40, 41}},
}};

// The fifteen spots of the town (read from the original): seven small,
// four wide, two large, the stockade's and the docks'.
constexpr std::array<std::array<int, 2>, 15> kSpots{{{56, 13},
                                                     {145, 15},
                                                     {173, 18},
                                                     {8, 41},
                                                     {37, 45},
                                                     {67, 54},
                                                     {96, 53},
                                                     {6, 14},
                                                     {128, 53},
                                                     {10, 76},
                                                     {15, 102},
                                                     {87, 11},
                                                     {66, 87},
                                                     {123, 106},
                                                     {123, 55}}};

// Read from the original: every colony shuffles the spots of each size
// among the kinds of that size, with the C library's random numbers seeded
// by its square and the game's seed, so a colony always looks the same.
// Spot by spot, a free place of the spot's size is drawn at random.
std::array<BuildingSlot, 15> townSlots(const SaveGame& save, const SaveColony& colony) {
    constexpr std::array<int, 5> count{7, 4, 2, 1, 1};
    constexpr std::array<int, 5> first{0, 7, 11, 13, 14};
    OriginalRandom random((static_cast<std::uint32_t>((colony.y << 8) | colony.x) + gameSeed(save)) & 0x7FFF);
    std::array<int, 15> spotOf{};
    spotOf.fill(-1);
    for (int spot = 0, size = 0; spot < 15; ++spot) {
        while (spot >= first[static_cast<std::size_t>(size)] + count[static_cast<std::size_t>(size)]) {
            ++size;
        }
        const auto s = static_cast<std::size_t>(size);
        for (;;) {
            const int place = random.between(0, count[s] - 1) + first[s];
            if (spotOf[static_cast<std::size_t>(place)] < 0) {
                spotOf[static_cast<std::size_t>(place)] = spot;
                break;
            }
        }
    }
    auto slots = kSlots;
    for (auto& slot : slots) {
        const auto& at = kSpots[static_cast<std::size_t>(spotOf[static_cast<std::size_t>(slot.place)])];
        slot.x = at[0];
        slot.y = at[1];
        slot.size = slot.place < 7 ? 0 : slot.place < 11 ? 1 : slot.place < 13 ? 2 : slot.place - 10;
    }
    return slots;
}

// Read from the original, by the size of the spot: where the people at work
// stand and how wide a space they share, and where the row of what they make
// goes and how wide it is.
constexpr std::array<int, 5> kWorkersX{3, 20, 25, 5, 0};
constexpr std::array<int, 5> kWorkersY{12, 8, 22, 5, 0};
constexpr std::array<int, 5> kWorkersWidth{17, 21, 25, 65, 0};
constexpr std::array<int, 5> kMadeX{-5, 0, 0, 0, 0};
constexpr std::array<int, 5> kMadeY{-3, 1, 1, 1, 1};
constexpr std::array<int, 5> kMadeWidth{20, 22, 30, 20, 20};

// The k-th of n people at work: they share the space, centred in it, each
// counted as wide as the figure of an expert at the job plus its shadow, and
// at most one pixel more apart.
std::array<int, 2> workerAt(const BuildingSlot& slot, int k, int n, int figure) {
    const int kFigure = figure + 2;
    const auto s = static_cast<std::size_t>(slot.size);
    const int space = kWorkersWidth[s];
    const int step = n > 1 ? std::clamp((space - kFigure) / (n - 1), 1, kFigure + 1) : 1;
    const int span = (n - 1) * step + kFigure;
    return {slot.x + kWorkersX[s] + (space - span) / 2 + step * k, slot.y + kWorkersY[s] + 1};
}

// The size of a building's spot, by the filler drawn when it is empty
// (docs/rules/colony-screen.md).
Rect slotRect(const BuildingSlot& slot) {
    switch (slot.empty) {
    case kFillerShore:
        return {slot.x, slot.y, 75, 48};
    case kNoStockade:
        return {slot.x, slot.y, 73, 18};
    case kFillerLarge:
        return {slot.x, slot.y, 53, 37};
    case kFillerWide:
        return {slot.x, slot.y, 44, 22};
    default:
        return {slot.x, slot.y, 23, 27};
    }
}

// The job done in a kind of building, or -1.
int buildingJob(BuildingKind kind) {
    for (int j = job::kDistiller; j <= job::kTeacher; ++j) {
        if (buildingForJob(j) == kind) {
            return j;
        }
    }
    return -1;
}

// Measured: the units outside stand in a row 11 pixels apart over the
// stockade's spot, centred on it.
constexpr int kOutsideCentre = 157;
constexpr int kOutsideStep = 11;
constexpr int kOutsideY = 111;
constexpr std::uint8_t kTownShadow = 0x5F;

int outsideX(int k, int count) { return kOutsideCentre - kOutsideStep * (count - 1) / 2 + kOutsideStep * k; }

int spriteWidth(const SpriteSheet& sheet, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= sheet.sprites.size()) {
        return 0;
    }
    return sheet.sprites[static_cast<std::size_t>(index)].image.width;
}

// Read from the original: people at work are spaced by the figure of the
// expert at their job (ICONS.SS 81 + the job).
int figureWidth(const Art* art, int job) { return art == nullptr ? 8 : spriteWidth(art->icons, kExpertIcons + job); }

// The shadow of a figure in the town, two pixels to its left.
void drawShadow(Screen& screen, const IndexedImage& image, int x, int y) {
    for (int j = 0; j < image.height; ++j) {
        for (int i = 0; i < image.width; ++i) {
            if (image.pixels[static_cast<std::size_t>(j * image.width + i)] != kTransparent) {
                screen.pixel(x - 2 + i, y + j, kTownShadow);
            }
        }
    }
}

void blitWithShadow(Screen& screen, const IndexedImage& image, int x, int y) {
    drawShadow(screen, image, x, y);
    screen.blit(image, x, y);
}

// A number in white on a black box, laid over the first of a row of icons.
// `inTown`: read from the original, the town's rows put the number three
// pixels in from where the row starts, whatever the icon.
void drawCount(Screen& screen, const Art& art, int iconX, int iconY, int iconW, int count, int dy = 2,
               bool inTown = false) {
    const std::string text = std::to_string(count);
    const int w = Screen::textWidth(art.tiny, text);
    const int tx = inTown ? iconX + 3 : iconX + (iconW - w + 1) / 2 + 1;
    const int ty = iconY + dy;
    screen.fill({tx - 1, ty - 1, w + 1, 7}, kBlack);
    screen.text(art.tiny, tx, ty, text, {kWhite, kWhite, kWhite});
}

// A row of `count` icons of a good starting at x, squeezed into `room`
// pixels when there are many. Returns the x where the row ends.
int drawGoodsRow(Screen& screen, const Art& art, int good, int count, int x, int y, int room, int countDy = 2,
                 bool inTown = false) {
    if (count <= 0) {
        return x;
    }
    const int icon = goodIcon(good);
    const int w = spriteWidth(art.icons, icon);
    int step = w + 1;
    if (count > 1 && (count - 1) * step + w > room) {
        step = std::max(1, (room - w) / (count - 1));
    }
    for (int i = 0; i < count; ++i) {
        blitSprite(screen, art.icons, icon, x + i * step, y);
    }
    drawCount(screen, art, x, y, w, count, countDy, inTown);
    return x + (count - 1) * step + w;
}

// A row of icons centred in a box `room` pixels wide.
void drawGoodsCentred(Screen& screen, const Art& art, int good, int count, int x, int y, int room,
                      bool inTown = false) {
    if (count <= 0) {
        return;
    }
    const int w = spriteWidth(art.icons, goodIcon(good));
    int step = w + 1;
    if (count > 1 && (count - 1) * step + w > room) {
        step = std::max(1, (room - w) / (count - 1));
    }
    const int span = (count - 1) * step + w;
    drawGoodsRow(screen, art, good, count, x + std::max(0, (room - span) / 2), y, room, 2, inTown);
}

void drawTopLine(Screen& screen, const Art& art, const Game& game, const SaveColony& colony) {
    screen.tile(art.wood, {0, 0, Screen::kWidth, 7});
    screen.fill({0, 7, Screen::kWidth, 1}, kBlack);
    const auto& save = game.state();
    const auto& nation = save.nations[static_cast<std::size_t>(game.human())];
    // Measured on the original: one space more after the name.
    const std::string text = colony.name + ".   " + seasonName(art, save.season) + ", " + std::to_string(save.year) +
                             ".  Gold: " + std::to_string(nation.gold) + "$";
    screen.text(art.tiny, 160 - Screen::textWidth(art.tiny, text) / 2, 1, text, {kGreen, kNoColour, kNoColour});
}

constexpr int kLabelTrees = 122;         // @MISC
constexpr int kLabelOutsideColony = 123; // @MISC

// Read from the original: the name of the building at a place of the town in
// white on black, centred on the place and at its top (for the large
// buildings, the docks and empty places halfway down); the stockade's adds
// "(Outside Colony)" and an empty place says "Trees". It stays in the town.
void drawTownLabel(Screen& screen, const Art& art, const Game& game, const SaveColony& colony, int place) {
    for (const auto& slot : townSlots(game.state(), colony)) {
        if (slot.place != place) {
            continue;
        }
        const Rect r = slotRect(slot);
        const int level = buildingLevel(colony, slot.kind);
        const auto& rows = game.rules().buildings;
        std::string text;
        bool middle = slot.size == 2 || slot.size == 4;
        if (level <= 0) {
            text = textLine(art.labelText, "MISC", kLabelTrees, "Trees");
            middle = true;
        } else {
            int row = kBuildingFields[static_cast<std::size_t>(slot.kind)].firstRow + level - 1;
            if (slot.kind == BuildingKind::Warehouse && storageLimit(colony) >= 300) {
                ++row; // the warehouse expansion
            }
            if (row >= 0 && static_cast<std::size_t>(row) < rows.size()) {
                text = rows[static_cast<std::size_t>(row)].name;
            }
            if (slot.kind == BuildingKind::Fortification) {
                text += " " + textLine(art.labelText, "MISC", kLabelOutsideColony, "(Outside Colony)");
            }
        }
        const int w = Screen::textWidth(art.tiny, text) + 1;
        int x = r.x + r.w / 2 - w / 2;
        const int y = middle ? r.y + r.h / 2 - 3 : r.y;
        x = std::clamp(x, 0, kTown.x + kTown.w - w);
        screen.fill({x, y, w, 7}, kBlack);
        screen.text(art.tiny, x + 1, y + 1, text, {kWhite, kNoColour, kNoColour});
        return;
    }
}

void drawTown(Screen& screen, const Art& art, const ColonyArt& colonyArt, const Game& game, const SaveColony& colony,
              const ColonyProduction& production, int selected, int selectedOutside) {
    screen.setClip(kTown);
    screen.tile(colonyArt.parchment, kTown);
    const auto slots = townSlots(game.state(), colony);
    for (const auto& slot : slots) {
        int level = buildingLevel(colony, slot.kind);
        int sprite = level > 0 ? slot.levels[static_cast<std::size_t>(level - 1)] : slot.empty;
        if (slot.kind == BuildingKind::Warehouse) {
            const bool stables = buildingLevel(colony, BuildingKind::Stables) > 0;
            sprite = level > 0 ? (stables ? kWarehouseAndStables : kWarehouse) : (stables ? kStables : slot.empty);
        }
        blitSprite(screen, colonyArt.buildings, sprite, slot.x, slot.y);
    }

    // People at work inside, and what they make there.
    for (const auto& slot : slots) {
        // Measured: nobody is drawn in a building that is not there.
        if (buildingLevel(colony, slot.kind) <= 0) {
            continue;
        }
        int n = 0;
        for (const auto& work : production.colonists) {
            n += work.building && *work.building == slot.kind ? 1 : 0;
        }
        // Read from the original: the row of what they make comes first,
        // then the people over it, one by one, each two pixels right of its
        // place with its shadow on the place itself.
        int good = -1;
        int made = 0;
        std::vector<std::size_t> who;
        for (std::size_t i = 0; i < production.colonists.size(); ++i) {
            const auto& work = production.colonists[i];
            if (work.building && *work.building == slot.kind) {
                who.push_back(i);
                good = work.good;
                made += work.amount;
            }
        }
        // Read from the original: the town hall shows the colony's bells
        // but the printing press's share, which the press shows itself; the
        // church shows all the crosses.
        if (slot.kind == BuildingKind::TownHall) {
            good = cargo::kBells;
            made = production.produced[cargo::kBells] - production.pressBells;
        } else if (slot.kind == BuildingKind::PrintingPress) {
            good = cargo::kBells;
            made = production.pressBells;
        } else if (slot.kind == BuildingKind::Church) {
            good = cargo::kCrosses;
            made = production.produced[cargo::kCrosses];
        }
        if (good >= 0 && made > 0) {
            const auto z = static_cast<std::size_t>(slot.size);
            drawGoodsCentred(screen, art, good, made, slot.x + kMadeX[z], slot.y + kMadeY[z] + 1, kMadeWidth[z], true);
        }
        const int figure = who.empty() ? 0 : figureWidth(&art, colony.jobs[who.front()]);
        for (std::size_t k = 0; k < who.size(); ++k) {
            const auto i = who[k];
            const auto [x, y] = workerAt(slot, static_cast<int>(k), n, figure);
            const int icon = colonistIcon(colony.skills[i]);
            if (icon < 0 || static_cast<std::size_t>(icon) >= art.icons.sprites.size()) {
                continue;
            }
            const auto& image = art.icons.sprites[static_cast<std::size_t>(icon)].image;
            blitWithShadow(screen, image, x + 2, y);
            if (static_cast<int>(i) == selected) {
                // Read from the original: a bright green frame (colour 10)
                // from one row below the figure's top to its foot.
                screen.frame({x - 1, y + 1, image.width + 4, image.height}, kBrightGreen);
            }
        }
    }

    // The units outside the colony, each with its shadow two pixels left.
    const auto outside = unitsOutside(game, colony);
    const int count = static_cast<int>(outside.size());
    for (int k = 0; k < count; ++k) {
        const int u = outside[static_cast<std::size_t>(k)];
        const int icon = unitIcon(game, u);
        if (icon < 0 || static_cast<std::size_t>(icon) >= art.icons.sprites.size()) {
            continue;
        }
        const auto& image = art.icons.sprites[static_cast<std::size_t>(icon)].image;
        const int x = outsideX(k, count);
        blitWithShadow(screen, image, x, kOutsideY);
        if (u == selectedOutside) {
            screen.frame({x - 1, kOutsideY - 1, image.width + 2, 18}, kSelectGreen);
        }
    }
    screen.resetClip();
    screen.fill({199, 8, 1, 120}, kBlack);
}

void drawArea(Screen& screen, const Art& art, const Game& game, const SaveColony& colony,
              const ColonyProduction& production, int selected) {
    screen.tile(art.wood, kSide);
    screen.frame(kAreaFrame, kBlack);
    const Rect inner{kAreaFrame.x + 1, kAreaFrame.y + 1, 3 * kAreaSquare, 3 * kAreaSquare};

    // The 3×3 squares are drawn at 16 pixels and shown half as large again.
    Screen square;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            square.clear(kBlack);
            drawTerrain(square, art, game, colony.x + dx, colony.y + dy, 0, 0);
            if (dx == 0 && dy == 0) {
                const int fortification = buildingLevel(colony, BuildingKind::Fortification);
                static constexpr int kColonyIcons[] = {3, 0, 1, 2};
                blitSprite(square, art.icons, kColonyIcons[std::clamp(fortification, 0, 3)], 0, 0);
            }
            const int ox = inner.x + (dx + 1) * kAreaSquare;
            const int oy = inner.y + (dy + 1) * kAreaSquare;
            for (int y = 0; y < kAreaSquare; ++y) {
                for (int x = 0; x < kAreaSquare; ++x) {
                    screen.pixel(ox + x, oy + y, square.at(x * 2 / 3, y * 2 / 3));
                }
            }
        }
    }

    // The town commons and the people working the squares.
    const int cx = inner.x + kAreaSquare;
    const int cy = inner.y + kAreaSquare;
    drawGoodsCentred(screen, art, cargo::kFood, production.centreFood, cx, cy + 1, kAreaSquare);
    if (production.centreGood >= 0) {
        drawGoodsCentred(screen, art, production.centreGood, production.centreAmount, cx, cy + 14, kAreaSquare);
    }
    for (std::size_t i = 0; i < production.colonists.size(); ++i) {
        const auto& work = production.colonists[i];
        if (work.square < 0) {
            continue;
        }
        const auto offset = kColonySquares[static_cast<std::size_t>(work.square)];
        const int sx = inner.x + (offset.dx + 1) * kAreaSquare;
        const int sy = inner.y + (offset.dy + 1) * kAreaSquare;
        const int icon = colonistIcon(colony.skills[i]);
        blitSprite(screen, art.icons, icon, sx + 14, sy + 6);
        if (work.good >= 0 && work.amount > 0) {
            drawGoodsCentred(screen, art, work.good, work.amount, sx, sy + 1, kAreaSquare);
        }
        if (static_cast<int>(i) == selected) {
            screen.frame({sx, sy, kAreaSquare, kAreaSquare}, kSelectGreen);
        }
    }
}

void drawHarbour(Screen& screen, const Art& art) {
    const std::string text = "No Ships In Port";
    screen.text(art.tiny, 163 - Screen::textWidth(art.tiny, text) / 2, 132, text, {kHarbourText, kNoColour, kNoColour});
    for (int i = 0; i < 6; ++i) {
        blitSprite(screen, art.icons, kEmptyHold, 127 + 12 * i, 165);
    }
}

// One good in the production view: `count` icons, the last `red` of them
// crossed out; `allCrossed` crosses every one and writes the number in red.
struct ProductionItem {
    int good;
    int count;
    int red;
    bool allCrossed = false;
    // Read from the original, for food: the first `count - red` icons are
    // fish (ICONS.SS 57) instead of the good, none are crossed out, and the
    // number counts them all.
    bool fish = false;
};

// Read from the original: the rows of the production view. Raw goods with
// what the workshops want beyond them; then horses and what the workshops
// make, with what they cannot make for want of the raw good; then lumber
// and hammers.
std::array<std::vector<ProductionItem>, 3> productionRows(const SaveColony& colony,
                                                          const ColonyProduction& production) {
    const auto made = [&](int good) { return production.produced[static_cast<std::size_t>(good)]; };
    const auto wanted = [&](int good) { return production.needed[static_cast<std::size_t>(good)]; };
    // What a workshop cannot make: what it wants beyond the stock and the
    // raw good made this turn.
    const auto lost = [&](int input) {
        return std::max(0, wanted(input) - made(input) - colony.stock[static_cast<std::size_t>(input)]);
    };
    std::array<std::vector<ProductionItem>, 3> rows;
    for (const int good : {cargo::kSugar, cargo::kTobacco, cargo::kCotton, cargo::kFurs, cargo::kOre, cargo::kSilver}) {
        if (made(good) != 0) {
            const int beyond = std::max(0, wanted(good) - made(good));
            rows[0].push_back({good, made(good) + beyond, beyond});
        }
    }
    // Horses: what the herd could breed, and what it does not for want of
    // food.
    const int surplus = made(cargo::kFood) - production.foodEaten;
    const int herd = horsesBred(colony, 1 << 14);
    const int horsesLost = herd - horsesBred(colony, surplus);
    if (herd != 0 || horsesLost != 0) {
        rows[1].push_back({cargo::kHorses, std::max(herd, horsesLost), horsesLost});
    }
    static constexpr std::array<std::array<int, 2>, 7> kMadeFrom{{{cargo::kRum, cargo::kSugar},
                                                                  {cargo::kCigars, cargo::kTobacco},
                                                                  {cargo::kCloth, cargo::kCotton},
                                                                  {cargo::kCoats, cargo::kFurs},
                                                                  {cargo::kTradeGoods, -1},
                                                                  {cargo::kTools, cargo::kOre},
                                                                  {cargo::kMuskets, cargo::kTools}}};
    for (const auto& [good, input] : kMadeFrom) {
        const int missing = input >= 0 ? std::min(made(good), lost(input)) : 0;
        if (made(good) != 0 || missing != 0) {
            rows[1].push_back({good, std::max(made(good), missing), missing});
        }
    }
    const int lumber = made(cargo::kLumber);
    const int used = wanted(cargo::kLumber);
    if (lumber > used) {
        if (used != 0) {
            rows[2].push_back({cargo::kLumber, used, 0});
        }
        rows[2].push_back({cargo::kLumber, lumber - used, 0});
    } else {
        if (lumber != 0) {
            rows[2].push_back({cargo::kLumber, lumber, 0});
        }
        if (used - lumber != 0) {
            rows[2].push_back({cargo::kLumber, used - lumber, 0, true});
        }
    }
    const int noHammers = std::min(made(cargo::kHammers), lost(cargo::kLumber));
    if (made(cargo::kHammers) != noHammers) {
        rows[2].push_back({cargo::kHammers, made(cargo::kHammers) - noHammers, 0});
    }
    if (noHammers != 0) {
        rows[2].push_back({cargo::kHammers, noHammers, 0, true});
    }
    return rows;
}

// Read from the original: a row of groups of icons laid out in `width`.
// The groups stand `gap` apart; when they do not fit the gap shrinks, then
// the icons overlap, and the extra icons of each group share what room is
// left, at most one pixel more apart than the icon is wide (or, with no
// room at all, only every second, fourth... icon is drawn).
void drawProductionRow(Screen& screen, const Art& art, const std::vector<ProductionItem>& items, int x, int y,
                       int width, int gap) {
    const int n = static_cast<int>(items.size());
    if (n == 0) {
        return;
    }
    int multi = 0;
    int extra = 0;
    int iconsWidth = 0;
    for (const auto& item : items) {
        if (item.count > 1) {
            ++multi;
            extra += item.count - 1;
        }
        iconsWidth += spriteWidth(art.icons, goodIcon(item.good));
    }
    int overlap = 0;
    int space = 0;
    for (;;) {
        const int icons = std::max(0, iconsWidth - n * overlap);
        space = width - (n - 1) * gap - icons;
        if (space >= multi) {
            break;
        }
        if (gap > 0) {
            --gap;
        } else if (icons > 0) {
            ++overlap;
        } else {
            space = multi;
            break;
        }
    }
    int step = 0;
    int shift = 0;
    if (extra != 0) {
        step = space / extra;
        if (step == 0) {
            do {
                ++shift;
            } while ((extra >> shift) > space);
        }
    }
    const TextColours white{kWhite, kWhite, kWhite};
    const TextColours red{kRed, kRed, kRed};
    const auto number = [&](int count, int at, const TextColours& colours) {
        if (count <= 0) {
            return;
        }
        const std::string text = std::to_string(count);
        const int w = Screen::textWidth(art.tiny, text);
        screen.fill({at, y + 2, w + 1, 7}, kBlack);
        screen.text(art.tiny, at + 1, y + 3, text, colours);
    };
    for (const auto& item : items) {
        const int icon = goodIcon(item.good);
        const int w = spriteWidth(art.icons, icon);
        const int e = step != 0 ? std::min(w + 1, step) : 1;
        const int start = x;
        const int redFrom = (item.count - item.red) >> shift;
        int redX = -1;
        const int units = item.count >> shift;
        for (int k = 0; k < units; ++k) {
            if (k == redFrom) {
                redX = x;
            }
            if (item.fish && k < redFrom) {
                blitSprite(screen, art.icons, kFish, x, y);
            } else {
                blitSprite(screen, art.icons, icon, x, y);
                if (item.allCrossed || (k >= redFrom && !item.fish)) {
                    blitSprite(screen, art.icons, kCrossedOut, x, y);
                }
            }
            if (k < units - 1) {
                x += e;
            }
        }
        number(item.fish ? item.count : item.count - item.red, start + 2, item.allCrossed ? red : white);
        if (!item.fish) {
            number(item.red, redX + 2, red);
        }
        x += w - overlap + gap;
    }
}

void drawProductionView(Screen& screen, const Art& art, const SaveColony& colony, const ColonyProduction& production) {
    const auto rows = productionRows(colony, production);
    for (std::size_t row = 0; row < rows.size(); ++row) {
        drawProductionRow(screen, art, rows[row], 213, 134 + 14 * static_cast<int>(row), 89, row == 2 ? 4 : 2);
    }
}

void drawPeople(Screen& screen, const Art& art, const Game& game, const SaveColony& colony,
                const ColonyProduction& production, int selected, int libertyExtra) {
    const auto outside = unitsOutside(game, colony);
    const TextColours white{kWhite, kWhite, kWhite};
    const int sol = sonsOfLibertyPercent(colony, libertyExtra);
    const int rebels = rebelCount(colony, libertyExtra);
    blitSprite(screen, art.icons, kRebelFlag, 2, 132);
    screen.text(art.tiny, 15, 133, std::to_string(sol) + "% (" + std::to_string(rebels) + ")", white);
    const std::string tory =
        std::to_string(100 - sol) + "% (" + std::to_string(colony.population - rebels) + ")";
    screen.text(art.tiny, 104 - Screen::textWidth(art.tiny, tory), 133, tory, white);
    blitSprite(screen, art.icons, kCrown, 104, 132);

    // Read from the original: the colonists, then after 4 more pixels the
    // units outside, 2 pixels apart, or less (even overlapping) until the
    // row fits in 96 pixels.
    std::vector<int> icons;
    for (int i = 0; i < colony.population; ++i) {
        icons.push_back(colonistIcon(colony.skills[static_cast<std::size_t>(i)]));
    }
    for (const int u : outside) {
        icons.push_back(unitIcon(game, u));
    }
    const int n = static_cast<int>(icons.size());
    int widths = 0;
    for (const int icon : icons) {
        widths += spriteWidth(art.icons, icon);
    }
    const int apart = outside.empty() ? 0 : 4;
    int gap = 2;
    while (gap * (n - 1) + apart + widths >= 96) {
        --gap;
    }
    int x = 2;
    int owed = 0; // pixels still to take back from figures at least 1 apart
    for (int i = 0; i < n; ++i) {
        const int icon = icons[static_cast<std::size_t>(i)];
        const int w = spriteWidth(art.icons, icon);
        blitSprite(screen, art.icons, icon, x, 142);
        if (i == selected) {
            screen.frame({x - 1, 143, w + 2, 16}, kBrightGreen);
        }
        int step = gap + w;
        if (step < 1) {
            owed += 1 - step;
            step = 1;
        }
        while (step > 1 && owed > 0) {
            --step;
            --owed;
        }
        x += step;
        if (i == colony.population - 1) {
            x += 4;
        }
    }

    // Read from the original: the food eaten, then what is left over or
    // (crossed out) what is missing, the fishermen's catch shown first as
    // fish; then the crosses and the bells.
    const int food = production.produced[cargo::kFood];
    const int eaten = production.foodEaten;
    int fish = 0;
    for (std::size_t i = 0; i < production.colonists.size(); ++i) {
        if (colony.jobs[i] == job::kFisherman && production.colonists[i].good == cargo::kFood) {
            fish += production.colonists[i].amount;
        }
    }
    std::vector<ProductionItem> items;
    if (food >= eaten) {
        const int fishEaten = std::min(fish, eaten);
        items.push_back({cargo::kFood, eaten, eaten - fishEaten, false, true});
        items.push_back({cargo::kFood, food - eaten, food - eaten - (fish - fishEaten), false, true});
    } else {
        items.push_back({cargo::kFood, food, food - fish, false, true});
        items.push_back({cargo::kFood, eaten - food, 0, true});
    }
    if (production.produced[cargo::kCrosses] != 0) {
        items.push_back({cargo::kCrosses, production.produced[cargo::kCrosses], 0});
    }
    if (production.produced[cargo::kBells] != 0) {
        items.push_back({cargo::kBells, production.produced[cargo::kBells], 0});
    }
    drawProductionRow(screen, art, items, 2, 163, 118, 4);
}

std::string goodName(const Game& game, int good) {
    const auto& cargo = game.rules().cargo;
    if (good >= 0 && static_cast<std::size_t>(good) < cargo.size()) {
        return cargo[static_cast<std::size_t>(good)].name;
    }
    return {};
}

// The three buttons right of the panel. The one showing is pressed in: it
// moves a pixel down and right with a dark line above and left of it; the
// others stand out with the dark line below and right.
void drawPanelButtons(Screen& screen, const Art& art, ColonyPanel showing) {
    static constexpr int kSprites[3] = {kButtonProduction, kButtonUnits, kButtonBuild};
    static constexpr int kTops[3] = {132, 147, 162};
    for (int i = 0; i < 3; ++i) {
        const int sprite = kSprites[i];
        const int w = spriteWidth(art.icons, sprite);
        const int h = static_cast<std::size_t>(sprite) < art.icons.sprites.size()
                          ? art.icons.sprites[static_cast<std::size_t>(sprite)].image.height
                          : 0;
        const int x = 303;
        const int y = kTops[i];
        if (static_cast<int>(showing) == i) {
            blitSprite(screen, art.icons, sprite, x + 1, y + 1);
            screen.fill({x, y, w, 1}, kButtonShadow);
            screen.fill({x, y + 1, 1, h - 1}, kButtonShadow);
        } else {
            blitSprite(screen, art.icons, sprite, x, y);
            screen.fill({x + w, y + 1, 1, h}, kButtonShadow);
            screen.fill({x + 1, y + h, w, 1}, kButtonShadow);
        }
    }
}

// LABELS.TXT rows.
constexpr int kLabelNothing = 32;  // @MISC
constexpr int kLabelRequires = 43; // @MISC
constexpr int kLabelComplete = 127; // @MISC
constexpr int kLabelBuy = 2;        // @CTITLE
constexpr int kLabelChange = 3;
constexpr int kLabelSelectItem = 4;
constexpr int kLabelNoProduction = 5;
constexpr int kLabelMore = 6;
constexpr int kLabelProfession = 8;
constexpr int kLabelClearSpecialty = 44; // @MISC
constexpr int kJobsTitleWidth = 186;     // the box less its edges

// A button of the panel: a raised box with a word in it.
void drawPanelBox(Screen& screen, const Art& art, const PanelButton& b, const std::string& word) {
    screen.fill({b.x, b.y, b.w, 1}, kPanelInk);
    screen.fill({b.x, b.y + 1, 1, b.h - 1}, kPanelInk);
    screen.fill({b.x + b.w - 1, b.y + 1, 1, b.h - 1}, kPanelLight);
    screen.fill({b.x + 1, b.y + b.h - 1, b.w - 1, 1}, kPanelLight);
    screen.text(art.tiny, b.x + 3, b.y + 2, word, {kWhite, kNoColour, kNoColour});
}

// "Units Present": the land units standing in the colony, in a row along
// the bottom of the panel, the first one framed.
constexpr int kUnitsPanelRight = 302; // the buttons start at x 303

} // namespace

std::vector<int> unitsPresent(const Game& game, const SaveColony& colony) {
    std::vector<int> out;
    const auto here = game.unitsAt(colony.x, colony.y);
    for (auto it = here.rbegin(); it != here.rend(); ++it) {
        if (!game.isShip(*it)) {
            out.push_back(*it);
        }
    }
    return out;
}

std::vector<int> unitsOutside(const Game& game, const SaveColony& colony) {
    // Measured: only people stand there (no artillery, wagons or treasure).
    std::vector<int> out;
    for (const int u : unitsPresent(game, colony)) {
        const int type = game.unit(u).type;
        if (type >= unit_type::kColonist && type < unit_type::kTreasure) {
            out.push_back(u);
        }
    }
    return out;
}

ColonyHit colonyHitAt(const Game& game, const SaveColony& colony, int x, int y, const Art* art) {
    ColonyHit hit;
    const auto& save = game.state();
    const auto production =
        colonyProduction(colony, save.map, game.rules(), save.difficulty, true, nationEffects(save, colony.nation));
    const auto inside = [&](const Rect& r) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; };
    if (inside(kTown)) {
        // The people first: they stand in front of the buildings.
        const auto slots = townSlots(save, colony);
        for (const auto& slot : slots) {
            if (buildingLevel(colony, slot.kind) <= 0) {
                continue;
            }
            int n = 0;
            for (const auto& work : production.colonists) {
                n += work.building && *work.building == slot.kind ? 1 : 0;
            }
            int k = 0;
            for (std::size_t i = 0; i < production.colonists.size(); ++i) {
                const auto& work = production.colonists[i];
                if (!work.building || *work.building != slot.kind) {
                    continue;
                }
                const auto [wx, wy] = workerAt(slot, k, n, figureWidth(art, colony.jobs[i]));
                if (inside(Rect{wx, wy, 10, 16})) {
                    hit.kind = ColonyHit::Kind::Colonist;
                    hit.colonist = static_cast<int>(i);
                }
                ++k;
            }
        }
        if (hit.kind != ColonyHit::Kind::None) {
            return hit;
        }
        const auto outside = unitsOutside(game, colony);
        const int count = static_cast<int>(outside.size());
        for (int k = count - 1; k >= 0; --k) {
            if (inside(Rect{outsideX(k, count), kOutsideY, 11, 18})) {
                hit.kind = ColonyHit::Kind::Outside;
                hit.unit = outside[static_cast<std::size_t>(k)];
                return hit;
            }
        }
        for (const auto& slot : slots) {
            if (!inside(slotRect(slot))) {
                continue;
            }
            if (slot.kind == BuildingKind::Fortification) {
                hit.kind = ColonyHit::Kind::Outside;
                return hit;
            }
            if (slot.kind == BuildingKind::CustomHouse && buildingLevel(colony, slot.kind) > 0) {
                hit.kind = ColonyHit::Kind::CustomHouse;
                return hit;
            }
            const int job = buildingJob(slot.kind);
            if (job >= 0 && buildingLevel(colony, slot.kind) > 0) {
                hit.kind = ColonyHit::Kind::Building;
                hit.job = job;
                return hit;
            }
        }
        return hit;
    }
    const Rect area{kAreaFrame.x + 1, kAreaFrame.y + 1, 3 * kAreaSquare, 3 * kAreaSquare};
    if (inside(area)) {
        const int dx = (x - area.x) / kAreaSquare - 1;
        const int dy = (y - area.y) / kAreaSquare - 1;
        for (std::size_t s = 0; s < kColonySquares.size(); ++s) {
            if (kColonySquares[s].dx != dx || kColonySquares[s].dy != dy) {
                continue;
            }
            hit.square = static_cast<int>(s);
            hit.kind = ColonyHit::Kind::Square;
            for (std::size_t i = 0; i < production.colonists.size(); ++i) {
                if (production.colonists[i].square == hit.square) {
                    hit.kind = ColonyHit::Kind::Colonist;
                    hit.colonist = static_cast<int>(i);
                }
            }
        }
    }
    return hit;
}

OutsideMenuBox outsideMenuBox(int lines) {
    // Measured: as wide as the jobs menu, 24 + 8 per line high, centred.
    const int h = 24 + 8 * lines;
    const int y = (Screen::kHeight - h) / 2;
    return {62, y, 196, h, y + 16};
}

int unitPresentAt(int x, int y) {
    // The five units in front, 18 pixels apart from x 213 at row 158.
    if (y < 157 || y >= 175 || x < 212) {
        return -1;
    }
    const int k = (x - 212) / 18;
    return k < 5 ? k : -1;
}

std::array<int, 2> townSpot(const SaveGame& save, const SaveColony& colony, BuildingKind kind) {
    for (const auto& slot : townSlots(save, colony)) {
        if (slot.kind == kind) {
            return {slot.x, slot.y};
        }
    }
    return {-1, -1};
}

int townPlaceAt(const SaveGame& save, const SaveColony& colony, int x, int y) {
    // Read from the original: the spots are tried in their order.
    const auto slots = townSlots(save, colony);
    for (const auto& spot : kSpots) {
        for (const auto& slot : slots) {
            if (slot.x != spot[0] || slot.y != spot[1]) {
                continue;
            }
            const Rect r = slotRect(slot);
            if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
                return slot.place;
            }
        }
    }
    return -1;
}

// 1 when the jobs menu starts with "(CLEAR SPECIALTY)": the colonist is an expert.
int jobsMenuClearLine(const SaveColony& colony, int colonist) {
    if (colonist < 0 || colonist >= colony.population) {
        return 0;
    }
    return Game::isExpert(colony.skills[static_cast<std::size_t>(colonist)]) ? 1 : 0;
}

namespace {

void drawUnitsPanel(Screen& screen, const Art& art, const Game& game, const SaveColony& colony, int selected) {
    const std::string title = textLine(art.labelText, "CMISC", 1, "Units Present");
    screen.text(art.tiny, 256 - (Screen::textWidth(art.tiny, title) - 1) / 2, 132, title, {kPanelInk, kNoColour, kNoColour});
    int k = 0;
    for (const int u : unitsPresent(game, colony)) {
        // Measured: five units 18 pixels apart at row 158, the first framed
        // in bright green. The rest stand small on the horizon behind them,
        // 5 pixels apart.
        if (k >= 5) {
            drawSmallUnit(screen, art, game, u, 214 + 5 * (k - 5), 151, kUnitsPanelRight);
            ++k;
            continue;
        }
        const int x = 213 + 18 * k;
        drawUnitInColonyList(screen, art, game, u, x, 158);
        // Measured: once a unit is clicked, a white frame stays on it (and
        // follows it to the front); until then the first is framed green.
        if (selected >= 0 ? u == selected : k == 0) {
            screen.frame({x - 1, 157, 18, 18}, selected >= 0 ? kSelectedFrame : kUnitFrame);
        }
        ++k;
    }
}

// The Make panel: what is being built, BUY and CHANGE, the hammers so far
// in four rows (each row a quarter of what is needed), and the tools it
// needs.
void drawMakePanel(Screen& screen, const Art& art, const Game& game, const SaveColony& colony) {
    const auto& rules = game.rules();
    const int construction = colony.construction;
    const std::string title = construction < 0 ? textLine(art.labelText, "MISC", kLabelNothing, "Nothing")
                                               : constructionName(rules, construction);
    screen.text(art.tiny, 256 - (Screen::textWidth(art.tiny, title) - 1) / 2, 132, title, {kPanelInk, kNoColour, kNoColour});
    if (construction >= 0) {
        drawPanelBox(screen, art, kBuyButton, textLine(art.labelText, "CTITLE", kLabelBuy, "BUY"));
    }
    drawPanelBox(screen, art, kChangeButton, textLine(art.labelText, "CTITLE", kLabelChange, "CHANGE"));
    if (construction < 0) {
        // Measured: with nothing to build the hammers kept show as one row
        // of at most 10.
        for (int i = 0; i < std::min(colony.hammers, 10); ++i) {
            blitSprite(screen, art.icons, kHammerIcon, 212 + 9 * i, 147);
        }
        return;
    }
    const int needed = constructionHammers(rules, construction);
    const int perRow = (needed + 3) / 4;
    int left = colony.hammers;
    for (int row = 0; row < 4 && left > 0 && perRow > 0; ++row) {
        const int count = std::min(left, perRow);
        left -= count;
        const int y = 147 + 6 * row;
        // Nine pixels apart while a row fits; otherwise spread so that the
        // last one stands about 80 pixels on, with the count on the row
        // once they stand closer than 2 pixels.
        const int spread = (80 * perRow + perRow - 2) / std::max(1, perRow - 1);
        const bool fits = perRow * 9 <= 92;
        for (int i = 0; i < count; ++i) {
            blitSprite(screen, art.icons, kHammerIcon, 212 + (fits ? 9 * i : i * spread / perRow), y);
        }
        if (!fits && spread < 2 * perRow) {
            drawCount(screen, art, 214, y, spriteWidth(art.icons, kHammerIcon), count);
        }
    }
    const int tools = constructionTools(rules, construction);
    if (tools > 0) {
        const std::string text = "(" + textLine(art.labelText, "MISC", kLabelRequires, "Requires") + " " +
                                 std::to_string(tools) + " " + goodName(game, cargo::kTools) + ")";
        screen.text(art.tiny, 256 - (Screen::textWidth(art.tiny, text) - 1) / 2, 170, text, {kWhite, kNoColour, kNoColour});
    }
}

void drawWarehouse(Screen& screen, const Art& art, const SaveColony& colony, int selectedGood) {
    for (int good = 0; good < kGoodsKinds; ++good) {
        const int centre = 10 + 19 * good;
        const int icon = kGoodsIcon + good;
        blitSprite(screen, art.icons, icon, centre - spriteWidth(art.icons, icon) / 2, 181);
        // Read from the original: the hundreds in another colour; goods the
        // custom house sells in green; goods beyond what the warehouse
        // keeps (not food) in red.
        const int stock = colony.stock[static_cast<std::size_t>(good)];
        std::uint8_t hundredsColour = kStockHundreds;
        std::uint8_t restColour = kStockText;
        if (buildingLevel(colony, BuildingKind::CustomHouse) > 0 && exportTicked(colony, good)) {
            restColour = kBrightGreen;
        }
        if (good != cargo::kFood && stock > storageLimit(colony)) {
            hundredsColour = kRed;
            restColour = kStockOver;
        }
        const std::string amount = std::to_string(stock);
        int x = centre + 1 - (Screen::textWidth(art.tiny, amount) + 1) / 2;
        if (stock >= 100) {
            x = screen.text(art.tiny, x, 194, std::to_string(stock / 100), {hundredsColour, kNoColour, kNoColour});
            const int rest = stock % 100;
            const std::string tens = (rest < 10 ? "0" : "") + std::to_string(rest);
            screen.text(art.tiny, x, 194, tens, {restColour, kNoColour, kNoColour});
        } else {
            screen.text(art.tiny, x, 194, amount, {restColour, kNoColour, kNoColour});
        }
    }
    if (selectedGood >= 0 && selectedGood < kGoodsKinds) {
        screen.frame({19 * selectedGood, 179, 20, 21}, kBrightGreen);
    }
    screen.text(art.tiny, 306, 179, "Exit", {kWhite, kNoColour, kNoColour});
}

// A wooden box as the original's menus on the colony screen have.
void drawWoodBox(Screen& screen, const Art& art, const Rect& box) {
    screen.setClip({box.x + 1, box.y + 1, box.w - 2, box.h - 2});
    for (int y = box.y; y < box.y + box.h; ++y) {
        for (int x = box.x; x < box.x + box.w; ++x) {
            const auto& wood = art.wood;
            if (wood.width > 0 && wood.height > 0) {
                screen.pixel(x, y,
                             wood.pixels[static_cast<std::size_t>(((y - box.y) % wood.height) * wood.width +
                                                                  (x - box.x) % wood.width)]);
            }
        }
    }
    screen.resetClip();
    // Black, a ring of 0x86, and a sunken edge: dark above and right,
    // light left and below.
    screen.frame(box, kBlack);
    screen.frame({box.x + 1, box.y + 1, box.w - 2, box.h - 2}, 0x86);
    screen.fill({box.x + 2, box.y + 2, box.w - 4, 1}, 0x80);
    screen.fill({box.x + box.w - 3, box.y + 2, 1, box.h - 4}, 0x80);
    screen.fill({box.x + 2, box.y + 3, 1, box.h - 5}, kMenuBar);
    screen.fill({box.x + 2, box.y + box.h - 3, box.w - 4, 1}, kMenuBar);
}

// The original's jobs menu: a wooden box with a line per job and what the
// colonist would make at it, then "(More)".
void drawJobsMenu(Screen& screen, const Art& art, const Game& game, const SaveColony& colony, int colonist,
                  int highlighted) {
    const auto& save = game.state();
    const auto& jobs = game.rules().jobs;
    const auto choices = jobChoices(colony, save.map, game.rules(), save.colonies, colonist, save.difficulty);
    auto nameOf = [&](int row, bool expert) -> std::string {
        if (row < 0 || static_cast<std::size_t>(row) >= jobs.size()) {
            return {};
        }
        return expert ? jobs[static_cast<std::size_t>(row)].expertName : jobs[static_cast<std::size_t>(row)].name;
    };
    const int skillOf = colony.skills[static_cast<std::size_t>(colonist)];
    // Skills 0x1C (free colonist) and the soldier-like ones use their @JOB rows.
    const int skillRow = skillOf == skill::kFreeColonist ? 19 : skillOf;
    const std::string title = "Select a Profession for " + nameOf(skillRow, true) + "(" +
                              nameOf(colony.jobs[static_cast<std::size_t>(colonist)], false) + "):";
    // Measured: a title too wide for the box goes on to a second line 6
    // pixels lower, and the box grows to hold every line, centred.
    std::vector<std::string> titleLines{""};
    {
        std::string word;
        for (std::size_t i = 0; i <= title.size(); ++i) {
            const char ch = i < title.size() ? title[i] : ' ';
            if (ch != ' ') {
                word.push_back(ch);
                continue;
            }
            auto& line = titleLines.back();
            const std::string next = line.empty() ? word : line + " " + word;
            if (!line.empty() && Screen::textWidth(art.tiny, next) > kJobsTitleWidth) {
                titleLines.push_back(word);
            } else {
                line = next;
            }
            word.clear();
        }
    }
    const int clear = jobsMenuClearLine(colony, colonist);
    const int lines = clear + static_cast<int>(choices.size()) + 1;
    const int extra = 6 * (static_cast<int>(titleLines.size()) - 1);
    const int h = 24 + 8 * lines + extra;
    const Rect box{62, (200 - h) / 2, 196, h};
    drawWoodBox(screen, art, box);

    const TextColours green{kGreen, kNoColour, kNoColour};
    for (std::size_t t = 0; t < titleLines.size(); ++t) {
        screen.text(art.tiny, 67, box.y + 6 + 6 * static_cast<int>(t), titleLines[t], green);
    }
    for (int i = 0; i < lines; ++i) {
        const int y = box.y + 16 + extra + 8 * i;
        if (i == highlighted) {
            screen.fill({64, y - 1, 190, 7}, kMenuBar);
        }
        if (i < clear) {
            screen.text(art.tiny, 75, y, textLine(art.labelText, "MISC", kLabelClearSpecialty, "(CLEAR SPECIALTY)"),
                        green);
            continue;
        }
        if (i == lines - 1) {
            screen.text(art.tiny, 75, y, "(More)", green);
            break;
        }
        const auto& c = choices[static_cast<std::size_t>(i - clear)];
        screen.text(art.tiny, 75, y, nameOf(c.job, false), green);
        if (jobOutput(c.job) >= 0) {
            const std::string value = std::to_string(c.amount) + " " + goodName(game, jobOutput(c.job));
            screen.text(art.tiny, 245 - Screen::textWidth(art.tiny, value), y, value, green);
        }
    }
    const std::string help = "(F1 for Help)";
    screen.text(art.tiny, 256 - Screen::textWidth(art.tiny, help), box.y + box.h - 9, help,
                {kGold, kNoColour, kNoColour});
}

// The menu of jobs outside the colony (measured): as the jobs menu, with
// what each job takes from the warehouse in brackets, greyed when the
// warehouse lacks it. The first line is "(More)" when the menu came from
// the jobs menu, else the present job.
void drawOutsideMenu(Screen& screen, const Art& art, const Game& game, const ColonyViewState& view) {
    const auto& save = game.state();
    const auto& colony = save.colonies[static_cast<std::size_t>(view.colony)];
    const auto& jobs = game.rules().jobs;
    const ColonyMover who{view.selectedOutside >= 0 ? -1 : view.selectedColonist, view.selectedOutside};
    const auto choices = game.outsideChoices(view.colony, who);
    const int lines = static_cast<int>(choices.size()) + 1;
    const auto b = outsideMenuBox(lines);
    drawWoodBox(screen, art, {b.x, b.y, b.w, b.h});
    const TextColours green{kGreen, kNoColour, kNoColour};
    const TextColours grey{kMenuGrey, kNoColour, kNoColour};
    auto nameOf = [&](int row, bool expert) -> std::string {
        if (row < 0 || static_cast<std::size_t>(row) >= jobs.size()) {
            return {};
        }
        return expert ? jobs[static_cast<std::size_t>(row)].expertName : jobs[static_cast<std::size_t>(row)].name;
    };
    int skill = 0;
    if (who.colonist >= 0) {
        skill = colony.skills[static_cast<std::size_t>(who.colonist)];
    } else if (who.unit >= 0) {
        skill = game.unit(who.unit).profession;
    }
    const int skillRow = skill == skill::kFreeColonist ? skill::kFreeColonistUnit : skill;
    const int now = game.currentJob(view.colony, who);
    const std::string title = textLine(art.labelText, "CTITLE", kLabelProfession, "Select a Profession for") + " " +
                              nameOf(skillRow, true) + "(" + nameOf(now, false) + "):";
    screen.text(art.tiny, b.x + 5, b.y + 6, title, green);
    for (int i = 0; i < lines; ++i) {
        const int y = b.firstLine + 8 * i;
        if (i == view.outsideMenu) {
            screen.fill({b.x + 2, y - 1, b.w - 6, 7}, kMenuBar);
        }
        if (i == 0) {
            const std::string first =
                view.outsideFromMore ? textLine(art.labelText, "CTITLE", kLabelMore, "(More)") : nameOf(now, false);
            screen.text(art.tiny, 75, y, first, green);
            continue;
        }
        const auto& c = choices[static_cast<std::size_t>(i - 1)];
        const auto& colours = c.affordable ? green : grey;
        screen.text(art.tiny, 75, y, nameOf(c.job, false), colours);
        std::string value;
        const auto bracket = [&](int amount, int good) {
            return "(" + std::to_string(amount) + " " + goodName(game, good) + ")";
        };
        if (c.job == outside_job::kPioneer) {
            value = bracket(c.tools, cargo::kTools);
        } else if (c.job == outside_job::kSoldier) {
            value = bracket(50, cargo::kMuskets);
        } else if (c.job == outside_job::kScout) {
            value = bracket(50, cargo::kHorses);
        } else if (c.job == outside_job::kDragoon) {
            value = bracket(50, cargo::kMuskets) + " " + bracket(50, cargo::kHorses);
        }
        if (!value.empty()) {
            screen.text(art.tiny, 245 - Screen::textWidth(art.tiny, value), y, value, colours);
        }
    }
    const std::string help = "(F1 for Help)";
    screen.text(art.tiny, 256 - Screen::textWidth(art.tiny, help), b.y + b.h - 9, help, {kGold, kNoColour, kNoColour});
}

// "Select An Item To Build": a wooden box with "(No Production)" and a line
// per thing the colony can build, with the hammers and tools still wanted.
void drawBuildList(Screen& screen, const Art& art, const Game& game, const SaveColony& colony, int highlighted) {
    const auto& save = game.state();
    const auto& rules = game.rules();
    const auto options = buildOptions(colony, save.map, rules, buildLimits(save, colony.nation));
    const auto b = buildListBox(static_cast<int>(options.size()));
    drawWoodBox(screen, art, {b.x, b.y, b.w, b.h});
    const TextColours green{kGreen, kNoColour, kNoColour};
    const TextColours gold{kGold, kNoColour, kNoColour};
    screen.text(art.tiny, b.x + 5, b.y + 6, textLine(art.labelText, "CTITLE", kLabelSelectItem, "Select An Item To Build"),
                green);
    const int lines = static_cast<int>(options.size()) + 1;
    for (int i = 0; i < lines; ++i) {
        const int y = b.firstLine + 8 * i;
        const bool lit = i == highlighted;
        if (lit) {
            screen.fill({b.x + 4, y - 1, b.w - 8, 7}, kMenuBar);
        }
        const TextColours ink = lit ? gold : green;
        if (i == 0) {
            screen.text(art.tiny, b.x + 9, y, textLine(art.labelText, "CTITLE", kLabelNoProduction, "(No Production)"), ink);
            continue;
        }
        const auto& o = options[static_cast<std::size_t>(i - 1)];
        std::string name = constructionName(rules, o.construction);
        for (auto& ch : name) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        screen.text(art.tiny, b.x + 9, y, name, ink);
        std::string right;
        if (o.complete) {
            right = "(" + textLine(art.labelText, "MISC", kLabelComplete, "COMPLETE") + ")";
        } else {
            const int hammers = std::max(0, constructionHammers(rules, o.construction) - colony.hammers);
            right = "(" + std::to_string(hammers) + " " + goodName(game, cargo::kHammers) + ")";
            if (const int tools = constructionTools(rules, o.construction); tools > 0) {
                right += "(" + std::to_string(tools) + " " + goodName(game, cargo::kTools) + ")";
            }
        }
        screen.text(art.tiny, b.x + 175 - Screen::textWidth(art.tiny, right), y, right, ink);
    }
    const std::string help = "(F1 for Help)";
    screen.text(art.tiny, b.x + b.w - 2 - Screen::textWidth(art.tiny, help), b.y + b.h - 9, help, gold);
}

} // namespace

BuildListBox buildListBox(int items) {
    const int h = 8 * items + 32;
    const int y = (200 - h) / 2;
    return {68, y, 184, h, y + 16};
}

ColonyArt loadColonyArt(const std::filesystem::path& folder) {
    ColonyArt art;
    art.buildings = readSpriteSheet(readFile(folder, "BUILDING.SS"));
    const auto parchment = readSpriteSheet(readFile(folder, "PARCH.SS"));
    if (!parchment.sprites.empty()) {
        art.parchment = parchment.sprites.front().image;
    }
    art.background = readPicture(readFile(folder, "COLONY.PIK")).image;
    art.europe = readPicture(readFile(folder, "EUROPE.PIK")).image;
    return art;
}

void drawColonyScreen(Screen& screen, const Art& art, const ColonyArt& colonyArt, const Game& game,
                      const ColonyViewState& view) {
    const auto& save = game.state();
    if (view.colony < 0 || static_cast<std::size_t>(view.colony) >= save.colonies.size()) {
        return;
    }
    const auto& colony = save.colonies[static_cast<std::size_t>(view.colony)];
    const auto production = colonyProduction(colony, save.map, game.rules(), save.difficulty, true,
                                             nationEffects(save, colony.nation));

    screen.resetClip();
    screen.clear(kBlack);
    drawTopLine(screen, art, game, colony);
    drawTown(screen, art, colonyArt, game, colony, production, view.selectedColonist, view.selectedOutside);
    if (view.townLabel >= 0) {
        drawTownLabel(screen, art, game, colony, view.townLabel);
    }
    drawArea(screen, art, game, colony, production, view.selectedColonist);
    screen.blit(colonyArt.background, 0, kLowerTop);
    screen.fill({0, kLowerTop, Screen::kWidth, 1}, kBlack);
    drawPeople(screen, art, game, colony, production, view.selectedColonist,
               nationEffects(save, colony.nation).libertyExtra(colony.nation == save.humanNation));
    drawHarbour(screen, art);
    if (view.panel == ColonyPanel::Make) {
        drawMakePanel(screen, art, game, colony);
    } else if (view.panel == ColonyPanel::Units) {
        drawUnitsPanel(screen, art, game, colony, view.selectedUnit);
    } else {
        drawProductionView(screen, art, colony, production);
    }
    drawPanelButtons(screen, art, view.panel);
    drawWarehouse(screen, art, colony, view.selectedGood);
    if (view.jobsMenu >= 0 && view.selectedColonist >= 0) {
        drawJobsMenu(screen, art, game, colony, view.selectedColonist, view.jobsMenu);
    }
    if (view.outsideMenu >= 0) {
        drawOutsideMenu(screen, art, game, view);
    }
    if (view.buildList >= 0) {
        drawBuildList(screen, art, game, colony, view.buildList);
    }
}

} // namespace viceroy
