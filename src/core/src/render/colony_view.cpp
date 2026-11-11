// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/render/colony_view.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/game/colony.hpp"
#include "viceroy/game_files.hpp"

#include <algorithm>
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
    const int rebels = (colony.population * sol + 50) / 100;
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
    blitSprite(screen, art.icons, kButtonProduction, 304, 133);
    blitSprite(screen, art.icons, kButtonUnits, 303, 147);
    blitSprite(screen, art.icons, kButtonBuild, 303, 162);
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

void drawJobsMenu(Screen& screen, const Art& art, const Game& game, const SaveColony& colony, int colonist,
                  int highlighted) {
    const auto& save = game.state();
    const auto choices = jobChoices(colony, save.map, game.rules(), save.colonies, colonist);
    const Rect box{40, 20, 150, 14 + 8 * static_cast<int>(choices.size())};
    screen.fill(box, kBlack);
    screen.frame(box, 0x95);
    for (std::size_t i = 0; i < choices.size(); ++i) {
        const auto& c = choices[i];
        const int y = box.y + 5 + 8 * static_cast<int>(i);
        const bool selected = static_cast<int>(i) == highlighted;
        const bool expert = colony.skills[static_cast<std::size_t>(colonist)] == c.job;
        if (selected) {
            screen.fill({box.x + 3, y - 1, box.w - 6, 8}, kGreen);
        }
        std::string name = "Job";
        if (const auto& jobs = game.rules().jobs; static_cast<std::size_t>(c.job) < jobs.size()) {
            name = jobs[static_cast<std::size_t>(c.job)].name;
        }
        const std::uint8_t colour = selected ? kBlack : c.amount > 0 ? (expert ? 0x95 : kWhite) : 0x07;
        screen.text(art.tiny, box.x + 8, y, name, {colour, kNoColour, kNoColour});
        screen.text(art.tiny, box.x + box.w - 24, y, std::to_string(c.amount), {colour, kNoColour, kNoColour});
    }
}

} // namespace

ColonyArt loadColonyArt(const std::filesystem::path& folder) {
    ColonyArt art;
    art.buildings = readSpriteSheet(readFile(folder, "BUILDING.SS"));
    const auto parchment = readSpriteSheet(readFile(folder, "PARCH.SS"));
    if (!parchment.sprites.empty()) {
        art.parchment = parchment.sprites.front().image;
    }
    art.background = readPicture(readFile(folder, "COLONY.PIK")).image;
    return art;
}

void drawColonyScreen(Screen& screen, const Art& art, const ColonyArt& colonyArt, const Game& game,
                      const ColonyViewState& view) {
    const auto& save = game.state();
    if (view.colony < 0 || static_cast<std::size_t>(view.colony) >= save.colonies.size()) {
        return;
    }
    const auto& colony = save.colonies[static_cast<std::size_t>(view.colony)];
    const auto production = colonyProduction(colony, save.map, game.rules());

    screen.resetClip();
    screen.clear(kBlack);
    drawTopLine(screen, art, game, colony);
    drawTown(screen, art, colonyArt, colony, production, view.selectedColonist);
    drawArea(screen, art, game, colony, production, view.selectedColonist);
    screen.blit(colonyArt.background, 0, kLowerTop);
    screen.fill({0, kLowerTop, Screen::kWidth, 1}, kBlack);
    drawPeople(screen, art, colony, production, view.selectedColonist);
    drawHarbour(screen, art);
    drawProductionView(screen, art, production);
    drawWarehouse(screen, art, colony);
    if (view.jobsMenu >= 0 && view.selectedColonist >= 0) {
        drawJobsMenu(screen, art, game, colony, view.selectedColonist, view.jobsMenu);
    }
}

} // namespace viceroy
