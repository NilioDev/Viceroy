// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/render/colony_view.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/game/colony.hpp"
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
constexpr std::uint8_t kHarbourText = 0x39;
constexpr std::uint8_t kStockText = 0x3D;
constexpr std::uint8_t kRed = 0x0C;
constexpr std::uint8_t kGold = 0x95;
constexpr std::uint8_t kMenuBar = 0x8A;
constexpr std::uint8_t kPanelInk = 0x39;   // the Make panel: words and the dark edge of buttons
constexpr std::uint8_t kPanelLight = 0x30; // the light edge of buttons
constexpr std::uint8_t kButtonShadow = 0x3F;
constexpr std::uint8_t kUnitFrame = 0x0A;     // bright green
constexpr std::uint8_t kSelectedFrame = 0x0F; // white: the unit clicked

// Screen areas.
constexpr Rect kTown{0, 8, 200, 120};
constexpr Rect kSide{201, 8, 119, 120};
constexpr Rect kAreaFrame{223, 31, 74, 74};
constexpr int kAreaSquare = 24;
constexpr int kLowerTop = 128;

// Sprites in ICONS.SS.
constexpr int kGoodsIcon = 22;       // + cargo row, food to muskets
constexpr int kHammerIcon = 54;
constexpr int kCrossIcon = 56;
constexpr int kBellIcon = 62;
constexpr int kCrossedOut = 55;
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

// Where each kind of building stands in the town, and the sprites of
// BUILDING.SS for "nothing built here" and each level.
struct BuildingSlot {
    BuildingKind kind;
    int x;
    int y;
    int empty;
    std::array<int, 3> levels;
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
    {BuildingKind::Docks, 123, 55, kFillerShore, {6, 7, 8}},
    {BuildingKind::Armory, 128, 53, kFillerWide, {3, 4, 5}},
    {BuildingKind::Fortification, 123, 106, kNoStockade, {0, 1, 2}},
    {BuildingKind::TownHall, 87, 11, kFillerLarge, {9, 9, 9}},
    {BuildingKind::Schoolhouse, 15, 102, kFillerWide, {12, 13, 14}},
    {BuildingKind::Warehouse, 6, 14, kFillerWide, {kWarehouse, kWarehouse, kWarehouse}},
    {BuildingKind::CustomHouse, 96, 53, kFillerSmall, {18, 18, 18}},
    {BuildingKind::PrintingPress, 145, 15, kFillerSmall, {19, 20, 20}},
    {BuildingKind::Weaver, 67, 54, kFillerSmall, {21, 22, 23}},
    {BuildingKind::Tobacconist, 37, 45, kFillerSmall, {24, 25, 26}},
    {BuildingKind::Distiller, 173, 18, kFillerSmall, {27, 28, 29}},
    {BuildingKind::FurTrader, 8, 41, kFillerSmall, {32, 33, 34}},
    {BuildingKind::Carpenter, 10, 76, kFillerWide, {35, 36, 36}},
    {BuildingKind::Church, 66, 87, kFillerLarge, {37, 38, 38}},
    {BuildingKind::Blacksmith, 56, 13, kFillerSmall, {39, 40, 41}},
}};

int spriteWidth(const SpriteSheet& sheet, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= sheet.sprites.size()) {
        return 0;
    }
    return sheet.sprites[static_cast<std::size_t>(index)].image.width;
}

// A number in white on a black box, laid over the first of a row of icons.
void drawCount(Screen& screen, const Art& art, int iconX, int iconY, int iconW, int count, int dy = 2) {
    const std::string text = std::to_string(count);
    const int w = Screen::textWidth(art.tiny, text);
    const int tx = iconX + (iconW - w + 1) / 2 + 1;
    const int ty = iconY + dy;
    screen.fill({tx - 1, ty - 1, w + 1, 7}, kBlack);
    screen.text(art.tiny, tx, ty, text, {kWhite, kWhite, kWhite});
}

// A row of `count` icons of a good starting at x, squeezed into `room`
// pixels when there are many. Returns the x where the row ends.
int drawGoodsRow(Screen& screen, const Art& art, int good, int count, int x, int y, int room, int countDy = 2) {
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
    drawCount(screen, art, x, y, w, count, countDy);
    return x + (count - 1) * step + w;
}

// A row of icons centred in a box `room` pixels wide.
void drawGoodsCentred(Screen& screen, const Art& art, int good, int count, int x, int y, int room) {
    if (count <= 0) {
        return;
    }
    const int w = spriteWidth(art.icons, goodIcon(good));
    int step = w + 1;
    if (count > 1 && (count - 1) * step + w > room) {
        step = std::max(1, (room - w) / (count - 1));
    }
    const int span = (count - 1) * step + w;
    drawGoodsRow(screen, art, good, count, x + std::max(0, (room - span) / 2), y, room);
}

void drawTopLine(Screen& screen, const Art& art, const Game& game, const SaveColony& colony) {
    screen.tile(art.wood, {0, 0, Screen::kWidth, 7});
    screen.fill({0, 7, Screen::kWidth, 1}, kBlack);
    const auto& save = game.state();
    const auto& nation = save.nations[static_cast<std::size_t>(game.human())];
    const std::string text = colony.name + ".  " + seasonName(art, save.season) + ", " + std::to_string(save.year) +
                             ".  Gold: " + std::to_string(nation.gold) + "$";
    screen.text(art.tiny, 160 - Screen::textWidth(art.tiny, text) / 2, 1, text, {kGreen, kNoColour, kNoColour});
}

void drawTown(Screen& screen, const Art& art, const ColonyArt& colonyArt, const SaveColony& colony,
              const ColonyProduction& production, int selected) {
    screen.setClip(kTown);
    screen.tile(colonyArt.parchment, kTown);
    for (const auto& slot : kSlots) {
        int level = buildingLevel(colony, slot.kind);
        int sprite = level > 0 ? slot.levels[static_cast<std::size_t>(level - 1)] : slot.empty;
        if (slot.kind == BuildingKind::Warehouse) {
            const bool stables = buildingLevel(colony, BuildingKind::Stables) > 0;
            sprite = level > 0 ? (stables ? kWarehouseAndStables : kWarehouse) : (stables ? kStables : slot.empty);
        }
        blitSprite(screen, colonyArt.buildings, sprite, slot.x, slot.y);
    }

    // People at work inside, and what they make there.
    for (const auto& slot : kSlots) {
        int k = 0;
        int good = -1;
        int made = 0;
        for (std::size_t i = 0; i < production.colonists.size(); ++i) {
            const auto& work = production.colonists[i];
            if (!work.building || *work.building != slot.kind) {
                continue;
            }
            const int x = slot.x + 5 + 9 * k;
            const int y = slot.y + 13;
            const int icon = colonistIcon(colony.skills[i]);
            blitSprite(screen, art.icons, icon, x, y);
            if (static_cast<int>(i) == selected) {
                const int w = spriteWidth(art.icons, icon);
                screen.frame({x - 1, y - 1, w + 2, 18}, kSelectGreen);
            }
            good = work.good;
            made += work.amount;
            ++k;
        }
        if (good >= 0 && made > 0) {
            drawGoodsRow(screen, art, good, made, slot.x - 5, slot.y - 2, 20);
        }
    }
    screen.resetClip();
    screen.fill({200, 8, 1, 120}, kBlack);
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

void drawPeople(Screen& screen, const Art& art, const SaveColony& colony, const ColonyProduction& production,
                int selected) {
    const TextColours white{kWhite, kWhite, kWhite};
    const int sol = sonsOfLibertyPercent(colony);
    const int rebels = rebelCount(colony);
    blitSprite(screen, art.icons, kRebelFlag, 2, 132);
    screen.text(art.tiny, 15, 133, std::to_string(sol) + "% (" + std::to_string(rebels) + ")", white);
    const std::string tory =
        std::to_string(100 - sol) + "% (" + std::to_string(colony.population - rebels) + ")";
    screen.text(art.tiny, 102 - Screen::textWidth(art.tiny, tory), 133, tory, white);
    blitSprite(screen, art.icons, kCrown, 104, 132);

    for (int i = 0; i < colony.population; ++i) {
        const int icon = colonistIcon(colony.skills[static_cast<std::size_t>(i)]);
        const int x = 2 + 10 * i;
        blitSprite(screen, art.icons, icon, x, 142);
        if (i == selected) {
            screen.frame({x - 1, 141, spriteWidth(art.icons, icon) + 2, 18}, kSelectGreen);
        }
    }

    // Food eaten, food left over, crosses and bells.
    const int food = production.produced[cargo::kFood];
    int x = 2;
    const int eaten = std::min(food, production.foodEaten);
    if (eaten > 0) {
        x = drawGoodsRow(screen, art, cargo::kFood, eaten, x, 163, 70, 3) + 4;
    }
    if (food > production.foodEaten) {
        x = drawGoodsRow(screen, art, cargo::kFood, food - production.foodEaten, x, 163, 30, 3) + 4;
    }
    x = drawGoodsRow(screen, art, cargo::kCrosses, production.produced[cargo::kCrosses], x, 163, 30, 3) + 4;
    drawGoodsRow(screen, art, cargo::kBells, production.produced[cargo::kBells], x, 163, 40, 3);
}

void drawHarbour(Screen& screen, const Art& art) {
    const std::string text = "No Ships In Port";
    screen.text(art.tiny, 163 - Screen::textWidth(art.tiny, text) / 2, 132, text,
                {kHarbourText, kNoColour, kNoColour});
    for (int i = 0; i < 6; ++i) {
        blitSprite(screen, art.icons, kEmptyHold, 127 + 12 * i, 165);
    }
}

void drawProductionView(Screen& screen, const Art& art, const ColonyProduction& production) {
    // Three rows: raw crops, what is made of them, then lumber, ore and
    // what is made of those. Each good is followed by the shortage of what
    // its workshop wants, crossed out.
    struct Group {
        int good;
        int count;
        bool shortage;
    };
    static constexpr int kRows[3][7] = {
        {cargo::kSugar, cargo::kTobacco, cargo::kCotton, cargo::kFurs, -1, -1, -1},
        {cargo::kRum, cargo::kCigars, cargo::kCloth, cargo::kCoats, -1, -1, -1},
        {cargo::kLumber, cargo::kHammers, cargo::kOre, cargo::kTools, cargo::kMuskets, cargo::kSilver, -1},
    };
    constexpr int kLeft = 213;
    constexpr int kRoom = 87;
    for (int row = 0; row < 3; ++row) {
        std::vector<Group> groups;
        for (const int good : kRows[row]) {
            if (good < 0) {
                break;
            }
            const int made = production.produced[static_cast<std::size_t>(good)];
            const int short_ = production.needed[static_cast<std::size_t>(good)] - made;
            if (made > 0) {
                groups.push_back({good, made, false});
            }
            if (short_ > 0 && production.needed[static_cast<std::size_t>(good)] > 0) {
                groups.push_back({good, short_, true});
            }
        }
        int icons = 0;
        for (const auto& g : groups) {
            icons += g.count;
        }
        if (icons == 0) {
            continue;
        }
        const int y = 134 + 14 * row;
        int x = kLeft;
        for (std::size_t k = 0; k < groups.size(); ++k) {
            const auto& g = groups[k];
            const int icon = goodIcon(g.good);
            const int w = spriteWidth(art.icons, icon);
            const int step = std::max(1, std::min(w + 1, kRoom / icons));
            for (int i = 0; i < g.count; ++i) {
                blitSprite(screen, art.icons, icon, x + i * step, y);
                if (g.shortage) {
                    blitSprite(screen, art.icons, kCrossedOut, x + i * step, y);
                }
            }
            const std::string text = std::to_string(g.count);
            const int tw = Screen::textWidth(art.tiny, text);
            const int tx = x + (w - tw + 1) / 2 + 1;
            screen.fill({tx - 1, y + 1, tw + 1, 7}, kBlack);
            screen.text(art.tiny, tx, y + 2, text, g.shortage ? TextColours{kRed, kRed, kRed} : TextColours{kWhite, kWhite, kWhite});
            x += g.count * step;
            const bool sameGood = k + 1 < groups.size() && groups[k + 1].good == g.good;
            if (!sameGood) {
                x += 2;
            }
        }
    }
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

int unitPresentAt(int x, int y) {
    // The five units in front, 18 pixels apart from x 213 at row 158.
    if (y < 157 || y >= 175 || x < 212) {
        return -1;
    }
    const int k = (x - 212) / 18;
    return k < 5 ? k : -1;
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

void drawWarehouse(Screen& screen, const Art& art, const SaveColony& colony) {
    for (int good = 0; good < kGoodsKinds; ++good) {
        const int centre = 10 + 19 * good;
        const int icon = kGoodsIcon + good;
        blitSprite(screen, art.icons, icon, centre - spriteWidth(art.icons, icon) / 2, 181);
        const std::string amount = std::to_string(colony.stock[static_cast<std::size_t>(good)]);
        screen.text(art.tiny, centre - Screen::textWidth(art.tiny, amount) / 2, 194, amount,
                    {kStockText, kNoColour, kNoColour});
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
    const Rect box{62, 20, 196, 160};
    drawWoodBox(screen, art, box);

    const TextColours green{kGreen, kNoColour, kNoColour};
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
    screen.text(art.tiny, 67, 26, title, green);

    const int lines = static_cast<int>(choices.size()) + 1;
    for (int i = 0; i < lines; ++i) {
        const int y = 36 + 8 * i;
        if (i == highlighted) {
            screen.fill({64, y - 1, 190, 7}, kMenuBar);
        }
        if (i == lines - 1) {
            screen.text(art.tiny, 75, y, "(More)", green);
            break;
        }
        const auto& c = choices[static_cast<std::size_t>(i)];
        screen.text(art.tiny, 75, y, nameOf(c.job, false), green);
        const std::string value = std::to_string(c.amount) + " " + goodName(game, jobOutput(c.job));
        screen.text(art.tiny, 245 - Screen::textWidth(art.tiny, value), y, value, green);
    }
    const std::string help = "(F1 for Help)";
    screen.text(art.tiny, 256 - Screen::textWidth(art.tiny, help), 171, help, {kGold, kNoColour, kNoColour});
}


// "Select An Item To Build": a wooden box with "(No Production)" and a line
// per thing the colony can build, with the hammers and tools still wanted.
void drawBuildList(Screen& screen, const Art& art, const Game& game, const SaveColony& colony, int highlighted) {
    const auto& save = game.state();
    const auto& rules = game.rules();
    const auto options = buildOptions(colony, save.map, rules);
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
    const auto production = colonyProduction(colony, save.map, game.rules(), save.difficulty);

    screen.resetClip();
    screen.clear(kBlack);
    drawTopLine(screen, art, game, colony);
    drawTown(screen, art, colonyArt, colony, production, view.selectedColonist);
    drawArea(screen, art, game, colony, production, view.selectedColonist);
    screen.blit(colonyArt.background, 0, kLowerTop);
    screen.fill({0, kLowerTop, Screen::kWidth, 1}, kBlack);
    drawPeople(screen, art, colony, production, view.selectedColonist);
    drawHarbour(screen, art);
    if (view.panel == ColonyPanel::Make) {
        drawMakePanel(screen, art, game, colony);
    } else if (view.panel == ColonyPanel::Units) {
        drawUnitsPanel(screen, art, game, colony, view.selectedUnit);
    } else {
        drawProductionView(screen, art, production);
    }
    drawPanelButtons(screen, art, view.panel);
    drawWarehouse(screen, art, colony);
    if (view.jobsMenu >= 0 && view.selectedColonist >= 0) {
        drawJobsMenu(screen, art, game, colony, view.selectedColonist, view.jobsMenu);
    }
    if (view.buildList >= 0) {
        drawBuildList(screen, art, game, colony, view.buildList);
    }
}

} // namespace viceroy
