// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/render/report_view.hpp"

#include "viceroy/game/colony.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"

#include "viceroy/data/text_table.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>

namespace viceroy {
namespace {

constexpr std::uint8_t kBlack = 0x00;
constexpr std::uint8_t kWhite = 0x0F;
constexpr std::uint8_t kTitleColour = 0x90;
constexpr std::uint8_t kHeadingColour = 0x92;
constexpr std::uint8_t kNameColour = 0x61;
constexpr std::uint8_t kButtonFrame = 0x77;
constexpr int kOcean = 25;
constexpr int kSeaLane = 26;

const IndexedImage* icon(const Art& art, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= art.icons.sprites.size()) {
        return nullptr;
    }
    return &art.icons.sprites[static_cast<std::size_t>(index)].image;
}

int iconWidth(const Art& art, int index) {
    const auto* img = icon(art, index);
    return img ? img->width : 0;
}

int iconHeight(const Art& art, int index) {
    const auto* img = icon(art, index);
    return img ? img->height : 0;
}

void drawIcon(Screen& screen, const Art& art, int index, int x, int y) {
    if (const auto* img = icon(art, index)) {
        screen.blit(*img, x, y);
    }
}

// An icon with a black shadow two pixels to its left (read from the
// original).
void drawShadowedIcon(Screen& screen, const Art& art, int index, int x, int y) {
    const auto* img = icon(art, index);
    if (img == nullptr) {
        return;
    }
    constexpr int kShadowDx = 2;
    for (int j = 0; j < img->height; ++j) {
        for (int i = 0; i < img->width; ++i) {
            if (img->pixels[static_cast<std::size_t>(j * img->width + i)] != kTransparent) {
                screen.pixel(x - kShadowDx + i, y + j, kBlack);
            }
        }
    }
    screen.blit(*img, x, y);
}

void say(Screen& screen, const Art& art, int x, int y, const std::string& text, std::uint8_t colour) {
    screen.text(art.tiny, x, y, text, {colour, colour, colour});
}

// Read from the original: text centred in the `width` pixels from x.
void sayCentred(Screen& screen, const Art& art, int x, int width, int y, const std::string& text,
                std::uint8_t colour) {
    // The original measures text one pixel short of the glyphs' widths.
    const int w = Screen::textWidth(art.tiny, text) - 1;
    say(screen, art, std::max(0, x + width / 2 - w / 2), y, text, colour);
}

// A count in white on a black box, two pixels below y.
void drawNumber(Screen& screen, const Art& art, int n, int x, int y) {
    if (n <= 0) {
        return;
    }
    const std::string text = std::to_string(n);
    const int w = Screen::textWidth(art.tiny, text) - 1;
    y += 2;
    screen.fill({x, y, w + 2, 7}, kBlack);
    say(screen, art, x + 1, y + 1, text, kWhite);
}

std::string label(const Art& art, int row, const char* fallback) {
    return textLine(art.labelText, "MISC", row, fallback);
}

// The report's picture with its title centred at the top.
void drawReportGround(Screen& screen, const Art& art, int picture, const std::string& title) {
    const auto& ground = art.reports.size() >= static_cast<std::size_t>(picture)
                             ? art.reports[static_cast<std::size_t>(picture - 1)]
                             : art.congressReport;
    if (ground.palette) {
        // Measured: the picture's colours replace all but the first 16.
        screen.setColours(*ground.palette, 16, 255);
    }
    screen.blit(ground.image, 0, 0);
    sayCentred(screen, art, 0, Screen::kWidth, 5, title, kTitleColour);
}

// The OK box in the corner.
void drawOk(Screen& screen, const Art& art) {
    constexpr int kOkX = 286;
    constexpr int kOkY = 184;
    screen.frame({kOkX, kOkY, 30, 14}, kButtonFrame);
    sayCentred(screen, art, kOkX, 30, kOkY + 7 - art.tiny.height / 2, label(art, 46, "OK"), kHeadingColour);
}

} // namespace

void drawIconBar(Screen& screen, const Art& art, int iconIndex, int count, int have, int x, int y, int width) {
    // Read from the original: the icons that stand for all `count` are
    // spaced to fill the width, closer than a pixel apart halving the
    // count until they fit; the pixels left over are handed out along the
    // row. Only `have` of them are drawn.
    if (count <= 0 || have <= 0) {
        return;
    }
    const int iconW = iconWidth(art, iconIndex);
    const int room = width - iconW;
    int step = 1;
    if (count > 1) {
        step = std::clamp(room / (count - 1), 1, iconW + 1);
    }
    const int span = (count - 1) * step;
    int shift = 0;
    while ((span >> shift) > room) {
        ++shift;
    }
    const int leftover = width - ((span >> shift) + iconW);
    const int shown = have >> shift;
    const int all = count >> shift;
    int px = x;
    int spare = 0;
    for (int i = 0; i < shown; ++i) {
        drawIcon(screen, art, iconIndex, px, y + 1);
        px += step;
        spare += leftover;
        while (all > 0 && spare >= all) {
            spare -= all;
            ++px;
        }
    }
    if (step == 1 && have > 1) {
        drawNumber(screen, art, have, x + 2, y);
    }
}

void drawIconGroups(Screen& screen, const Art& art, const std::vector<IconGroup>& all, int x, int y, int width,
                    int gap, bool numbers) {
    std::vector<IconGroup> groups;
    for (const auto& g : all) {
        if (g.count > 0) {
            groups.push_back(g);
        }
    }
    const int n = static_cast<int>(groups.size());
    if (n == 0) {
        return;
    }
    // Read from the original: first the gaps shrink, then the icons
    // overlap, until every group of more than one has a pixel per icon.
    int multi = 0;
    int extra = 0;
    int widths = 0;
    for (const auto& g : groups) {
        if (g.count > 1) {
            ++multi;
            extra += g.count - 1;
        }
        widths += iconWidth(art, g.icon);
    }
    int overlap = 0;
    int room = 0;
    for (;;) {
        const int drawn = std::max(0, widths - n * overlap);
        room = width - drawn - (n - 1) * gap;
        if (room >= multi) {
            break;
        }
        if (gap > 0) {
            --gap;
        } else if (drawn > 0) {
            ++overlap;
        } else {
            room = multi;
            break;
        }
    }
    int step = 0;
    int shift = 0;
    if (extra != 0) {
        step = room / extra;
        if (step == 0) {
            do {
                ++shift;
            } while ((extra >> shift) > room);
        }
    }
    int px = x;
    for (const auto& g : groups) {
        const int iconW = iconWidth(art, g.icon);
        const int groupStep = step == 0 ? 1 : std::min(iconW + 1, step);
        const int startX = px;
        const int shown = g.count >> shift;
        for (int i = 0; i < shown; ++i) {
            drawIcon(screen, art, g.icon, px, y);
            if (shown - 1 > i) {
                px += groupStep;
            }
        }
        if (numbers || (groupStep == 1 && g.count > 1)) {
            drawNumber(screen, art, g.count, startX + 2, y);
        }
        px += iconW - overlap + gap;
    }
}

void drawCongressReport(Screen& screen, const Art& art, const Game& game, int nation) {
    const auto& save = game.state();
    const auto& rules = game.rules();
    if (art.congressReport.palette) {
        screen.setColours(*art.congressReport.palette, 0, 255);
    }
    screen.blit(art.congressReport.image, 0, 0);
    sayCentred(screen, art, 0, Screen::kWidth, 5, label(art, 37, "CONTINENTAL CONGRESS ACTIVITIES"), kTitleColour);

    const int lineHeight = art.tiny.height + 2;
    constexpr int kLeft = 4;
    constexpr int kWidth = 300;
    int y = 25;

    // Read from the original: the father up for election and the bells
    // toward him.
    std::string line = label(art, 112, "Next Continental Congress Session") + ": ";
    const int father = game.fatherInElection(nation);
    if (father >= 0 && static_cast<std::size_t>(father) < rules.fathers.size()) {
        line += "(" + rules.fathers[static_cast<std::size_t>(father)].name + ")";
    }
    say(screen, art, kLeft, y, line, kHeadingColour);
    y += lineHeight;
    constexpr int kBell = 0x3E; // icons counted from 0 here, from 1 in the original
    const int needed = game.bellsForNextFather(nation);
    drawIconBar(screen, art, kBell, needed, std::min(game.congressBells(nation), needed), kLeft, y, kWidth);
    y += iconHeight(art, kBell) + 2;

    // The rebels and tories among the nation's people.
    int people = game.nationPeople(nation);
    people = std::max(people, 1);
    constexpr std::size_t kRebelPercent = 96;
    const int percent = save.header[kRebelPercent] | (save.header[kRebelPercent + 1] << 8);
    const int rebels = people * percent / 100;
    y += 12;
    line = label(art, 69, "Rebel") + " " + label(art, 71, "Sentiment") + ": " + std::to_string(percent) + "%   " +
           label(art, 70, "Tory") + " " + label(art, 71, "Sentiment") + ": " + std::to_string(100 - percent) + "%";
    say(screen, art, kLeft, y, line, kHeadingColour);
    y += lineHeight;
    constexpr int kRebelIcon = 0x7B;
    constexpr int kToryIcon = 0x7C;
    drawIconGroups(screen, art, {{kRebelIcon, rebels}, {kToryIcon, people - rebels}}, kLeft, y, kWidth, 4, false);
    y += iconHeight(art, kRebelIcon) + 14;

    // The King's expeditionary force: regulars, dragoons, artillery and
    // men-of-war (header words 106, 108, 112 and 110).
    const auto word = [&](std::size_t at) { return save.header[at] | (save.header[at + 1] << 8); };
    const auto unitIcon = [&](int type) {
        return static_cast<std::size_t>(type) < rules.units.size() ? rules.units[static_cast<std::size_t>(type)].icon - 1
                                                                    : 0;
    };
    const int regulars = word(106);
    const int dragoons = word(108);
    const int menOfWar = word(110);
    const int artillery = word(112);
    if (regulars + dragoons + menOfWar + artillery != 0) {
        line = textLine(art.names, "NATIONALITY", nation, "") + " " + label(art, 85, "Expeditionary Force") + ": ";
        say(screen, art, kLeft, y, line, kHeadingColour);
        y += lineHeight;
        constexpr int kRegular = 6, kDragoon = 8;
        drawIconGroups(screen, art,
                       {{unitIcon(kRegular), regulars},
                        {unitIcon(kDragoon), dragoons},
                        {unitIcon(kArtillery), artillery},
                        {unitIcon(unit_type::kManOWar), menOfWar}},
                       kLeft, y, kWidth, 4, true);
        y += iconHeight(art, kRebelIcon) + 14;
    }

    // The fathers in congress, four to a line.
    say(screen, art, kLeft, y, label(art, 89, "Founding Fathers") + ": ", kHeadingColour);
    y += lineHeight;
    int column = 0;
    int nx = kLeft;
    for (int f = 0; f < Game::kFoundingFathers && static_cast<std::size_t>(f) < rules.fathers.size(); ++f) {
        if (!game.hasFather(nation, f)) {
            continue;
        }
        say(screen, art, nx, y, rules.fathers[static_cast<std::size_t>(f)].name, kNameColour);
        nx += 78;
        if (++column == 4) {
            column = 0;
            y += lineHeight;
            nx = kLeft;
        }
    }

    drawOk(screen, art);
}

void drawReligionReport(Screen& screen, const Art& art, const Game& game, int nation) {
    drawReportGround(screen, art, 2, label(art, 30, "RELIGIOUS ADVISER REPORT"));
    // Read from the original: the crosses toward the next immigrant, one
    // icon each, in a bar 300 pixels wide.
    const auto& n = game.state().nations[static_cast<std::size_t>(nation)];
    constexpr int kCross = 0x38;
    drawIconBar(screen, art, kCross, n.crossesNeeded, std::min(n.crosses, n.crossesNeeded), 10, 25, 300);
    drawOk(screen, art);
}

namespace {

// Read from the original: where a unit is, as the reports name it.
// `heading` names a sea lane as the home port (a ship bound for Europe).
std::string placeName(const Art& art, const Game& game, int x, int y, int nation, bool heading) {
    const auto& map = game.state().map;
    if (x < 0 || y < 0 || x >= map.width || y >= map.height) {
        if (nation >= 0 && nation < kEuropeanNations && x - kEuropeBase - nation == 4 * kPortStage) {
            return textLine(art.names, "HOMEPORT", nation, "");
        }
        return label(art, 60, "High Seas");
    }
    if (heading && map.at(x, y).terrain == kSeaLane && nation >= 0 && nation < kEuropeanNations) {
        return textLine(art.names, "HOMEPORT", nation, "");
    }
    if (const auto colony = game.colonyAt(x, y)) {
        return game.state().colonies[static_cast<std::size_t>(*colony)].name;
    }
    return "(" + std::to_string(x) + ", " + std::to_string(y) + ")";
}

constexpr int kNavalRows = 7;

// The units the Naval Adviser lists: the nation's ships, and its land
// units at sea (aboard).
std::vector<int> navalUnits(const Game& game, int nation) {
    std::vector<int> out;
    const auto& map = game.state().map;
    for (int i = 0; i < game.unitCount(); ++i) {
        const auto& u = game.unit(i);
        if (u.nation != nation) {
            continue;
        }
        const bool ship = u.type >= unit_type::kCaravel && u.type <= unit_type::kManOWar;
        if (!ship) {
            // The original looks at the terrain under the unit; units off
            // the map (in Europe) are left out here.
            if (u.x < 0 || u.y < 0 || u.x >= map.width || u.y >= map.height) {
                continue;
            }
            const int t = map.at(u.x, u.y).terrain;
            if (t != kOcean && t != kSeaLane) {
                continue;
            }
        }
        out.push_back(i);
    }
    return out;
}

} // namespace

int navalReportPages(const Game& game, int nation) {
    const int n = static_cast<int>(navalUnits(game, nation).size());
    return std::max(1, (n + kNavalRows - 1) / kNavalRows);
}

void drawNavalReport(Screen& screen, const Art& art, const Game& game, int nation, int page) {
    drawReportGround(screen, art, 7, label(art, 52, "NAVAL ADVISER REPORT"));
    // Read from the original: four columns 80 pixels wide from x 2 (the
    // last 76), heads at y 27, and a table of seven rows of 20 pixels.
    constexpr int kLeft = 2;
    constexpr int kColumn = 80;
    constexpr int kTop = 42;
    constexpr std::uint8_t kLine = 0x77;
    constexpr std::uint8_t kInk = 0x61;
    sayCentred(screen, art, kLeft, kColumn, 27, label(art, 61, "Ship"), kHeadingColour);
    sayCentred(screen, art, kLeft + kColumn, kColumn, 27, label(art, 62, "Cargo"), kHeadingColour);
    sayCentred(screen, art, kLeft + 2 * kColumn, kColumn, 27, label(art, 63, "Location"), kHeadingColour);
    sayCentred(screen, art, kLeft + 3 * kColumn, 76, 27, label(art, 64, "Destination"), kHeadingColour);
    for (int i = 1; i <= 3; ++i) {
        screen.fill({kLeft + kColumn * i, 25, 1, 156}, kLine);
    }
    for (int i = 0; i < 8; ++i) {
        screen.fill({kLeft, 20 * (i + 2), 313, 1}, kLine);
    }
    const auto units = navalUnits(game, nation);
    const auto& rules = game.rules();
    for (int row = 0; row < kNavalRows; ++row) {
        const std::size_t at = static_cast<std::size_t>(page * kNavalRows + row);
        if (at >= units.size()) {
            break;
        }
        const int index = units[at];
        const auto& u = game.unit(index);
        const int y = kTop + 20 * row;
        const bool ship = u.type >= unit_type::kCaravel && u.type <= unit_type::kManOWar;
        // A land unit aboard stands in the cargo column.
        const int x = ship ? kLeft : kLeft + 86;
        drawUnitAt(screen, art, game, index, x, y);
        for (int k = 0; k < u.holds; ++k) {
            const int good = holdGood(u, k);
            const int icon = (holdAmount(u, k) < 100 ? 0x26 : 0x16) + good;
            drawIcon(screen, art, icon, kLeft + 86 + 12 * k, y + 3);
        }
        const std::string name = static_cast<std::size_t>(u.type) < rules.units.size()
                                     ? rules.units[static_cast<std::size_t>(u.type)].name
                                     : "";
        say(screen, art, x + 24, y + 6, name, kInk);
        sayCentred(screen, art, kLeft + 2 * kColumn, kColumn, y + 6, placeName(art, game, u.x, u.y, u.nation, false),
                   kInk);
        std::string where;
        if (u.orders == 2 || u.orders == 3 || u.orders == 11) {
            where = placeName(art, game, u.raw[9], u.raw[10], nation, true);
        } else if (u.x < 0 || u.y < 0 || u.x >= game.state().map.width || u.y >= game.state().map.height) {
            const int stage = (u.x - kEuropeBase - (u.nation & 3)) / 4;
            if (stage == 0 || stage == 1) {
                const auto& players = game.state().players;
                if (static_cast<std::size_t>(nation) < players.size()) {
                    where = players[static_cast<std::size_t>(nation)].country;
                }
            } else if (stage == 3 || stage == 4) {
                where = textLine(art.names, "HOMEPORT", nation, "");
            }
        } else {
            continue;
        }
        sayCentred(screen, art, kLeft + 3 * kColumn, 76, y + 6, where, kInk);
    }
    drawOk(screen, art);
}

namespace {

// The figures Jan de Witt's reports give for a nation, as the original's
// census counts them each turn (read from the original).
struct NationFigures {
    int colonies = 0;
    int averageColony = 0;
    int population = 0;
    int military = 0; // in eighths, as the original keeps it
    int naval = 0;
    int merchant = 0;
};

NationFigures nationFigures(const Game& game, int nation) {
    // The census block after the tribe records (see euro_ai.cpp).
    constexpr std::size_t kExtra = 8 * 78;
    constexpr std::size_t kColonyCount = kExtra + 16;
    constexpr std::size_t kPeople = kExtra + 28;
    constexpr std::size_t kHoldsAll = kExtra + 36;
    constexpr std::size_t kAttack = kExtra + 44;
    constexpr std::size_t kByType = kExtra + 64;
    constexpr std::size_t kAveragePeople = kExtra + 556;
    constexpr std::size_t kBlockEnd = kExtra + 727;
    NationFigures f;
    const auto& block = game.state().tribesAndMore;
    if (nation < 0 || nation >= kEuropeanNations || block.size() < kBlockEnd) {
        return f;
    }
    const auto n = static_cast<std::size_t>(nation);
    const auto word = [&](std::size_t at) { return static_cast<int>(block[at] | (block[at + 1] << 8)); };
    f.colonies = block[kColonyCount + n];
    f.averageColony = static_cast<std::int16_t>(word(kAveragePeople + 2 * n));
    f.population = block[kPeople + n];
    f.military = static_cast<std::int16_t>(word(kAttack + 2 * n));
    // Privateers and frigates, 8 each.
    constexpr std::size_t kTypes = 19;
    f.naval = (block[kByType + kTypes * n + 16] + block[kByType + kTypes * n + 17]) * 8;
    f.merchant = block[kHoldsAll + n];
    return f;
}

} // namespace

void drawForeignReport(Screen& screen, const Art& art, const Game& game, int nation) {
    drawReportGround(screen, art, 8, "");
    sayCentred(screen, art, 0, Screen::kWidth, 2, label(art, 93, "FOREIGN AFFAIRS REPORT"), kTitleColour);
    // Read from the original: a block 45 pixels high for each nation,
    // ruled off at its top.
    constexpr std::uint8_t kLine = 0x77;
    constexpr std::uint8_t kInk = 0x91;
    constexpr std::uint8_t kPeace = 0x0F;
    constexpr std::uint8_t kWar = 0x0C;
    constexpr int kLeft = 2;
    constexpr int kTop = 13;
    constexpr int kBlock = 45;
    for (int n = 0; n < kEuropeanNations; ++n) {
        screen.fill({0, kTop + kBlock * n - 3, Screen::kWidth, 1}, kLine);
    }
    const auto& save = game.state();
    const int step = art.tiny.height + 1;
    const int withdrawn = static_cast<std::int16_t>(save.header[98] | (save.header[99] << 8));
    constexpr int kDeWitt = 4;
    const bool figures = game.hasFather(nation, kDeWitt);
    const auto text = [&](int x, int y, const std::string& s, std::uint8_t colour) {
        say(screen, art, x, y, s, colour);
        return x + Screen::textWidth(art.tiny, s);
    };
    for (int n = 0; n < kEuropeanNations; ++n) {
        int y = kTop + kBlock * n;
        const auto& nr = save.nations[static_cast<std::size_t>(n)].raw;
        const std::string leader =
            static_cast<std::size_t>(n) < save.players.size() ? save.players[static_cast<std::size_t>(n)].name : "";
        int x = text(kLeft, y, leader + label(art, 94, "'s") + " ", kHeadingColour);
        std::string people = textLine(art.names, "NATIONALITY", n, "") + ": ";
        const bool free = (nr[0] & 0x04) != 0;
        if (free) {
            people = label(art, 191, "Free") + " " + people;
        }
        text(x, y, people, kNameColour);
        y += step;
        if (n == withdrawn) {
            y += step;
            sayCentred(screen, art, 0, Screen::kWidth, y, label(art, 190, "(Withdrawn from New World)"), kInk);
            continue;
        }
        if (figures) {
            const auto f = nationFigures(game, n);
            const auto figure = [&](int fx, int row, int value) {
                text(fx, y, label(art, row, "") + ": " + std::to_string(value), kInk);
            };
            figure(kLeft, 95, f.colonies);
            figure(80, 97, f.averageColony);
            figure(160, 96, f.population);
            y += step;
            figure(kLeft, 98, f.military >> 3);
            figure(80, 99, f.naval);
            figure(160, 100, f.merchant);
        }
        y += step;
        // The nations it has met, with war or peace.
        bool any = false;
        int column = kLeft;
        for (int m = 0; m < kEuropeanNations; ++m) {
            if (m == n || (game.relation(n, m) & 0x20) == 0 || m == withdrawn) {
                continue;
            }
            any = true;
            const int end = text(column, y, textLine(art.names, "COUNTRY", m, "") + ": ", kInk);
            const bool peace = (game.relation(n, m) & 0x40) != 0;
            text(end, y, peace ? label(art, 102, "Peace") : label(art, 101, "War"), peace ? kPeace : kWar);
            column = column < 80 ? 80 : column + 80;
        }
        if (any) {
            y += step;
        }
        if (!free) {
            const int population = nationFigures(game, n).population;
            const int rebels = population * nr[0x19] / 100;
            text(kLeft, y, label(art, 86, "Rebels") + ": " + std::to_string(rebels), kInk);
            text(80, y, label(art, 87, "Tories") + ": " + std::to_string(population - rebels), kInk);
        }
    }
    drawOk(screen, art);
}

namespace {

constexpr int kProfessions = 29; // the expert list; 28 is a colony's free colonist
constexpr int kLaborLeft = 2;
constexpr int kLaborTop = 25;
constexpr int kLaborColumn = 105;
constexpr int kLaborRow = 18;

// A profession's name in the plural (the second field of @JOB).
std::string pluralName(const Art& art, int profession) {
    if (const auto* section = art.names.find("JOB")) {
        if (profession >= 0 && static_cast<std::size_t>(profession) < section->lines.size()) {
            const auto fields = splitFields(section->lines[static_cast<std::size_t>(profession)]);
            if (fields.size() > 1) {
                return fields[1];
            }
        }
    }
    return "";
}

// The picture of a profession in this report (measured): as a colonist in
// a colony, but Jesuit missionaries show the missionary unit's picture and
// Indian converts their own.
int laborIcon(int profession) {
    constexpr int kJesuitIcon = 105;
    constexpr int kConvertIcon = 66;
    if (profession == 24) {
        return kJesuitIcon;
    }
    return profession == 27 ? kConvertIcon : colonistIcon(profession);
}

bool personUnit(int type) { return type >= 0 && type <= 5; }

// Read from the original: the people of each profession, in the colonies
// (by their skill) and outside them; a colony's free colonists (28) count
// with the units' (19).
std::array<int, kProfessions> laborCounts(const Game& game, int nation) {
    std::array<int, kProfessions> counts{};
    for (const auto& c : game.state().colonies) {
        if (c.nation != nation) {
            continue;
        }
        for (int k = 0; k < c.population && k < kMaxColonists; ++k) {
            const int skill = static_cast<std::int8_t>(c.skills[static_cast<std::size_t>(k)]);
            if (skill >= 0 && skill < kProfessions) {
                ++counts[static_cast<std::size_t>(skill)];
            }
        }
    }
    for (int i = 0; i < game.unitCount(); ++i) {
        const auto& u = game.unit(i);
        if (u.nation == nation && personUnit(u.type) && u.profession >= 0 && u.profession < kProfessions) {
            ++counts[static_cast<std::size_t>(u.profession)];
        }
    }
    counts[19] += counts[28];
    return counts;
}

// Where each profession stands in the grid: three columns of 8, 9 and the
// rest; teachers (18), units' free colonists (19) and veteran dragoons (23)
// are left out, and a colony's free colonists (28) stand for 19.
struct LaborPlace {
    int profession;
    int x;
    int y;
};
std::vector<LaborPlace> laborPlaces() {
    std::vector<LaborPlace> out;
    int x = kLaborLeft;
    int y = kLaborTop;
    int column = 0;
    int row = 0;
    for (int p = 0; p < kProfessions; ++p) {
        if (p == 18 || p == 19 || p == 23) {
            continue;
        }
        out.push_back({p == 28 ? 19 : p, x, y});
        y += kLaborRow;
        if (++row >= (column == 0 ? 8 : 9) && column < 2) {
            row = 0;
            ++column;
            y = kLaborTop;
            x += kLaborColumn;
        }
    }
    return out;
}

} // namespace

int laborReportHit(int x, int y) {
    // Read from the original: the OK box's corner closes the report.
    if (x >= 286 && y >= 184) {
        return -1;
    }
    int hit = -1;
    for (const auto& place : laborPlaces()) {
        if (x >= place.x && x < place.x + kLaborColumn && y >= place.y && y < place.y + kLaborRow) {
            hit = place.profession;
        }
    }
    return hit;
}

void drawLaborReport(Screen& screen, const Art& art, const Game& game, int nation, int zoom) {
    drawReportGround(screen, art, 4, label(art, 49, "LABOR ADVISER REPORT"));
    const int sub = art.tiny.height + 6;
    const auto counts = laborCounts(game, nation);
    if (zoom < 0) {
        sayCentred(screen, art, 0, Screen::kWidth, sub, label(art, 56, "(Click on item to zoom)"), 0x91);
        for (const auto& place : laborPlaces()) {
            drawShadowedIcon(screen, art, laborIcon(place.profession), place.x + 2, place.y - 1);
            say(screen, art, place.x + 12, place.y + 1, pluralName(art, place.profession), kHeadingColour);
            say(screen, art, place.x + 39, place.y + art.tiny.height + 2,
                std::to_string(counts[static_cast<std::size_t>(place.profession)]), kNameColour);
        }
        drawOk(screen, art);
        return;
    }
    // One profession: where its people are, and how many in each colony.
    const int match = zoom == 19 ? 28 : zoom;
    const auto& save = game.state();
    std::vector<int> inColony(save.colonies.size());
    int colonies = 0;
    int onMap = 0;
    int europe = 0;
    for (std::size_t c = 0; c < save.colonies.size(); ++c) {
        const auto& colony = save.colonies[c];
        if (colony.nation != nation) {
            continue;
        }
        for (int k = 0; k < colony.population && k < kMaxColonists; ++k) {
            if (static_cast<std::int8_t>(colony.skills[static_cast<std::size_t>(k)]) == match) {
                ++colonies;
                ++inColony[c];
            }
        }
    }
    for (int i = 0; i < game.unitCount(); ++i) {
        const auto& u = game.unit(i);
        if (u.nation != nation || !personUnit(u.type) || u.profession != match) {
            continue;
        }
        if (!save.map.inside(u.x, u.y)) {
            ++europe;
        } else if (const auto colony = game.colonyAt(u.x, u.y)) {
            ++colonies;
            ++inColony[static_cast<std::size_t>(*colony)];
        } else {
            ++onMap;
        }
    }
    const std::string name = pluralName(art, zoom);
    sayCentred(screen, art, 0, Screen::kWidth, sub, "(" + name + ")", 0x91);
    drawShadowedIcon(screen, art, laborIcon(zoom), 4, 24);
    say(screen, art, 14, 30, name + ": " + std::to_string(counts[static_cast<std::size_t>(zoom)]), kHeadingColour);
    int y = kLaborTop;
    const int figures[3] = {europe, onMap, colonies};
    for (int i = 0; i < 3; ++i) {
        say(screen, art, 160, y, label(art, 53 + i, "") + ": ", kHeadingColour);
        say(screen, art, 255, y, std::to_string(figures[i]), kHeadingColour);
        y += art.tiny.height + 1;
    }
    // The colonies with any, three to a line, at most ten lines.
    y = kLaborTop + 40;
    int x = kLaborLeft;
    int column = 0;
    int lines = 0;
    for (std::size_t c = 0; c < save.colonies.size() && lines < 10; ++c) {
        if (inColony[c] == 0) {
            continue;
        }
        say(screen, art, x, y, save.colonies[c].name + ": " + std::to_string(inColony[c]), kNameColour);
        x += kLaborColumn;
        if (++column == 3) {
            column = 0;
            y += 9;
            x = kLaborLeft;
            ++lines;
        }
    }
    drawOk(screen, art);
}

namespace {

constexpr int kEconomyRows = 17; // colonies to a page of cargo in port

std::vector<int> nationColonies(const Game& game, int nation) {
    std::vector<int> out;
    const auto& colonies = game.state().colonies;
    for (std::size_t c = 0; c < colonies.size(); ++c) {
        if (colonies[c].nation == nation) {
            out.push_back(static_cast<int>(c));
        }
    }
    return out;
}

std::string cargoName(const Game& game, int good) {
    const auto& cargo = game.rules().cargo;
    return static_cast<std::size_t>(good) < cargo.size() ? cargo[static_cast<std::size_t>(good)].name : "";
}

// A trade figure as the original writes it: from 10000 on in thousands
// with a K.
std::string bigFigure(const Art& art, int value) {
    if (value >= 10000) {
        return std::to_string(value / 1000) + label(art, 57, "K");
    }
    return std::to_string(value);
}

void drawEuropeanTrade(Screen& screen, const Art& art, const Game& game, int nation) {
    drawReportGround(screen, art, 5, label(art, 50, "ECONOMIC ADVISER REPORT"));
    sayCentred(screen, art, 0, Screen::kWidth, art.tiny.height + 6, label(art, 206, "European Trade"), 0x91);
    constexpr std::uint8_t kLine = 0x77;
    constexpr std::uint8_t kGain = 0x0A;
    constexpr std::uint8_t kLoss = 0x0C;
    screen.fill({67, 25, 1, 137}, kLine);
    const auto rightAt = [&](int end, int y, const std::string& text, std::uint8_t colour) {
        say(screen, art, end - (Screen::textWidth(art.tiny, text) - 1), y, text, colour);
    };
    say(screen, art, 76, 25, label(art, 58, "Tons"), kHeadingColour);
    rightAt(144, 25, label(art, 59, "Gold"), kHeadingColour);
    say(screen, art, 170, 25, label(art, 203, "Bid Price"), kHeadingColour);
    say(screen, art, 220, 25, label(art, 204, "Ask Price"), kHeadingColour);
    for (int i = 0; i < 17; ++i) {
        screen.fill({2, 33 + 8 * i, 311, 1}, kLine);
    }
    const auto& n = game.state().nations[static_cast<std::size_t>(nation)];
    for (int g = 0; g < kGoodsKinds; ++g) {
        const int y = 33 + 8 * g + 2;
        const auto ug = static_cast<std::size_t>(g);
        say(screen, art, 2, y, cargoName(game, g), kHeadingColour);
        const int tons = n.tonsTraded[ug];
        rightAt(91, y, bigFigure(art, std::abs(tons)), tons < 0 ? kLoss : kGain);
        const int gold = n.tradeGold[ug];
        rightAt(144, y, bigFigure(art, std::abs(gold)) + "$", gold < 0 ? kLoss : kGain);
        rightAt(199, y, std::to_string(bidPrice(n, g)) + "$", kNameColour);
        rightAt(250, y, std::to_string(askPrice(game.rules(), n, g)) + "$", kNameColour);
    }
    drawOk(screen, art);
}

void drawCargoInPort(Screen& screen, const Art& art, const Game& game, int nation, int page) {
    drawReportGround(screen, art, 5, label(art, 50, "ECONOMIC ADVISER REPORT"));
    sayCentred(screen, art, 0, Screen::kWidth, art.tiny.height + 6, label(art, 207, "Cargo in Port"), 0x91);
    constexpr std::uint8_t kLine = 0x77;
    screen.fill({87, 25, 1, 154}, kLine);
    for (int g = 0; g < kGoodsKinds; ++g) {
        drawIcon(screen, art, 0x16 + g, 90 + 14 * g, 27);
        screen.fill({101 + 14 * g, 25, 1, 154}, kLine);
    }
    for (int i = 0; i < 18; ++i) {
        screen.fill({2, 42 + 8 * i, 310, 1}, kLine);
    }
    const auto colonies = nationColonies(game, nation);
    for (int row = 0; row < kEconomyRows; ++row) {
        const std::size_t at = static_cast<std::size_t>(page * kEconomyRows + row);
        if (at >= colonies.size()) {
            break;
        }
        const auto& colony = game.state().colonies[static_cast<std::size_t>(colonies[at])];
        const int y = 42 + 8 * row + 2;
        say(screen, art, 2, y, colony.name, kHeadingColour);
        for (int g = 0; g < kGoodsKinds; ++g) {
            // Read from the original: the figures stand right, 4 pixels a
            // digit; from 1000 on in thousands with a K, in yellow.
            const int amount = colony.stock[static_cast<std::size_t>(g)];
            std::uint8_t colour = amount >= 100 ? kHeadingColour : amount > 0 ? kNameColour : kBlack;
            int x = 85 + 14 * g;
            std::string text = std::to_string(amount);
            if (amount < 1000) {
                x += 4;
            }
            if (amount < 100) {
                x += 4;
            }
            if (amount < 10) {
                x += 4;
            }
            if (amount >= 1000) {
                text = std::to_string(amount / 1000) + " " + label(art, 57, "K");
                colour = 0x0E;
                x += 4;
            }
            say(screen, art, x, y, text, colour);
        }
    }
    drawOk(screen, art);
}

} // namespace

int economyReportPages(const Game& game, int nation) {
    const int n = static_cast<int>(nationColonies(game, nation).size());
    return 1 + (n + kEconomyRows - 1) / kEconomyRows;
}

void drawEconomyReport(Screen& screen, const Art& art, const Game& game, int nation, int page) {
    if (page == 0) {
        drawEuropeanTrade(screen, art, game, nation);
    } else {
        drawCargoInPort(screen, art, game, nation, page - 1);
    }
}

namespace {

constexpr int kColonyRows = 9; // colonies to a page of the Colony Adviser

void drawColonyAdviserGround(Screen& screen, const Art& art, int subtitleRow, const char* subtitle) {
    drawReportGround(screen, art, 6, label(art, 51, "COLONY ADVISER REPORT"));
    sayCentred(screen, art, 0, Screen::kWidth, art.tiny.height + 6, label(art, subtitleRow, subtitle), 0x91);
}

// The colony's picture and name at the start of its row.
void drawColonyRowHead(Screen& screen, const Art& art, const SaveColony& colony, int y) {
    drawColonyPicture(screen, art, colony, 4, y);
    // The colony's size on it, as on the map.
    const std::string size = std::to_string(colony.population);
    screen.text(art.tiny, 13 - Screen::textWidth(art.tiny, size) / 2, y + 7, size, {kWhite, kBlack, kBlack});
    say(screen, art, 25, y + 7, colony.name, kHeadingColour);
}

void drawGarrisons(Screen& screen, const Art& art, const Game& game, int nation, int page) {
    drawColonyAdviserGround(screen, art, 208, "Military Garrisons");
    const auto colonies = nationColonies(game, nation);
    const auto& rules = game.rules();
    for (int row = 0; row < kColonyRows; ++row) {
        const std::size_t at = static_cast<std::size_t>(page * kColonyRows + row);
        if (at >= colonies.size()) {
            break;
        }
        const auto& colony = game.state().colonies[static_cast<std::size_t>(colonies[at])];
        const int y = 20 + 17 * row;
        drawColonyRowHead(screen, art, colony, y);
        // Read from the original: the armed land units there, spread over
        // 210 pixels from x 110 (at most 18 apart), none past x 300.
        std::vector<int> armed;
        for (const int i : game.unitsAt(colony.x, colony.y)) {
            const int type = game.unit(i).type;
            if (type >= 0 && static_cast<std::size_t>(type) < rules.units.size() &&
                rules.units[static_cast<std::size_t>(type)].attack >= 1 &&
                !(type >= unit_type::kCaravel && type <= unit_type::kManOWar)) {
                armed.push_back(i);
            }
        }
        const int step = std::clamp(210 / std::max<int>(1, static_cast<int>(armed.size())), 1, 18);
        int x = 110;
        for (const int i : armed) {
            if (x > 300) {
                break;
            }
            drawUnitAt(screen, art, game, i, x, y, true);
            x += step;
        }
    }
    drawOk(screen, art);
}

void drawSonsOfLiberty(Screen& screen, const Art& art, const Game& game, int nation, int page) {
    drawColonyAdviserGround(screen, art, 209, "Sons of Liberty");
    const auto colonies = nationColonies(game, nation);
    const auto& save = game.state();
    constexpr int kRebelIcon = 0x7B;
    constexpr int kBellIcon = 0x3E;
    constexpr int kStatesmanJob = 17;
    for (int row = 0; row < kColonyRows; ++row) {
        const std::size_t at = static_cast<std::size_t>(page * kColonyRows + row);
        if (at >= colonies.size()) {
            break;
        }
        const auto& colony = save.colonies[static_cast<std::size_t>(colonies[at])];
        const int y = 20 + 17 * row;
        drawColonyRowHead(screen, art, colony, y);
        drawIcon(screen, art, kRebelIcon, 110, y + 2);
        say(screen, art, 112 + iconWidth(art, kRebelIcon) + 3, y + 7,
            std::to_string(sonsOfLibertyPercent(colony)) + "%", kHeadingColour);
        // The printing press or newspaper.
        const int press = buildingLevel(colony, BuildingKind::PrintingPress);
        if (press >= 2) {
            say(screen, art, 153, y + 7, textLine(art.names, "BUILDING", 20, "Newspaper"), kHeadingColour);
        } else if (press == 1) {
            say(screen, art, 153, y + 7, label(art, 202, "Press"), kHeadingColour);
        }
        drawIcon(screen, art, kBellIcon, 210, y + 2);
        const auto made =
            colonyProduction(colony, save.map, game.rules(), save.difficulty, true, nationEffects(save, colony.nation));
        say(screen, art, 212 + iconWidth(art, kBellIcon) + 3, y + 7, std::to_string(made.produced[cargo::kBells]),
            kHeadingColour);
        // The colonists working as statesmen.
        int x = 250;
        for (int k = 0; k < colony.population && k < kMaxColonists; ++k) {
            if (colony.jobs[static_cast<std::size_t>(k)] == kStatesmanJob) {
                drawShadowedIcon(screen, art,
                                 colonistIcon(static_cast<std::int8_t>(colony.skills[static_cast<std::size_t>(k)])), x,
                                 y + 1);
                x += 12;
            }
        }
    }
    drawOk(screen, art);
}

int colonyAdviserPagesEach(const Game& game, int nation) {
    const int n = static_cast<int>(nationColonies(game, nation).size());
    return std::max(1, (n + kColonyRows - 1) / kColonyRows);
}

} // namespace

int colonyAdviserPages(const Game& game, int nation) { return 2 * colonyAdviserPagesEach(game, nation); }

void drawColonyAdviser(Screen& screen, const Art& art, const Game& game, int nation, int page) {
    // Read from the original: the garrisons first, then the Sons of Liberty.
    const int each = colonyAdviserPagesEach(game, nation);
    if (page < each) {
        drawGarrisons(screen, art, game, nation, page);
    } else {
        drawSonsOfLiberty(screen, art, game, nation, page - each);
    }
}

namespace {

// Read from the original: text with a black edge one pixel to its right
// and below.
void sayEdged(Screen& screen, const Art& art, int x, int y, const std::string& text, std::uint8_t colour) {
    say(screen, art, x + 1, y, text, kBlack);
    say(screen, art, x, y + 1, text, kBlack);
    say(screen, art, x + 1, y + 1, text, kBlack);
    say(screen, art, x, y, text, colour);
}

} // namespace

void drawIndianReport(Screen& screen, const Art& art, const Game& game, int nation) {
    drawReportGround(screen, art, 1, label(art, 29, "INDIAN ADVISER REPORT"));
    const auto& save = game.state();
    const auto& block = save.tribesAndMore;
    const auto tribeByte = [&](int tribe, std::size_t at) -> std::uint8_t {
        const auto i = static_cast<std::size_t>(tribe) * kTribeRecordSize + at;
        return i < block.size() ? block[i] : 0;
    };
    constexpr std::size_t kFlags = 3;
    constexpr std::uint8_t kGone = 0x80;
    constexpr std::size_t kMusketStock = 7;
    constexpr std::size_t kHorseHerds = 8;
    constexpr int kArmedBrave = 20;
    constexpr int kMountedWarrior = 22;
    constexpr int kMoodIcon = 0x71; // + the tension level; 3 for a tribe gone
    constexpr int kColumn = 56;
    const auto* levels = art.names.find("LEVELS");
    const auto levelField = [&](int level, std::size_t field) {
        const auto row = static_cast<std::size_t>(level);
        const auto fields = levels != nullptr && row < levels->lines.size() ? splitFields(levels->lines[row])
                                                                            : std::vector<std::string>{};
        return field < fields.size() ? fields[field] : std::string();
    };
    int top = 25;
    for (int tribe = 0; tribe < kTribes; ++tribe) {
        const bool gone = (tribeByte(tribe, kFlags) & kGone) != 0;
        const auto& relations = save.nations[static_cast<std::size_t>(nation)].raw;
        const auto relationAt = static_cast<std::size_t>(52 + kEuropeanNations + tribe);
        const bool met = relationAt < relations.size() && (relations[relationAt] & 0x20) != 0;
        if (!met && !gone) {
            continue;
        }
        const int level = gone ? 3 : tensionLevel(tribeTension(save, tribe, nation));
        drawIcon(screen, art, kMoodIcon + level, 10, top);
        // Read from the original: each tribe in its colour, the Sioux in a
        // brighter red than on the map.
        constexpr int kSioux = 6;
        const std::uint8_t colour = tribe == kSioux ? 0x0C : nationColour(art, kEuropeanNations + tribe);
        const auto t = static_cast<std::size_t>(tribe);
        const std::string name = t < art.rules.tribes.size() ? art.rules.tribes[t].name : "";
        const int y = top + 3;
        top += 21;
        if (gone) {
            sayEdged(screen, art, 30, y, name + ": " + label(art, 130, "Extinct"), colour);
            continue;
        }
        sayEdged(screen, art, 30, y, name + ": ", colour);
        const int tribeLevelNow = tribeLevel(save, tribe);
        const std::string levelName = levelField(tribeLevelNow, 0);
        sayEdged(screen, art, 311 - Screen::textWidth(art.tiny, levelName), y, levelName, colour);
        int villages = 0;
        int missions = 0;
        for (const auto& s : save.settlements) {
            if (tribeOf(s) == tribe) {
                ++villages;
                missions += missionOf(s) == nation ? 1 : 0;
            }
        }
        int muskets = static_cast<std::int8_t>(tribeByte(tribe, kMusketStock));
        for (const auto& u : save.units) {
            if (u.nation == kEuropeanNations + tribe && (u.type == kArmedBrave || u.type == kMountedWarrior)) {
                ++muskets;
            }
        }
        muskets *= 50;
        const int herds = static_cast<std::int8_t>(tribeByte(tribe, kHorseHerds));
        // The second line in black only.
        const int line = y + art.tiny.height + 2;
        int x = 40;
        say(screen, art, x, line, std::to_string(villages) + " " + levelField(tribeLevelNow, villages == 1 ? 1 : 2),
            kBlack);
        x += kColumn;
        if (missions != 0) {
            say(screen, art, x, line, std::to_string(missions) + " " + label(art, missions == 1 ? 27 : 28, "Missions"),
                kBlack);
        }
        x += kColumn;
        if (muskets != 0) {
            say(screen, art, x, line, std::to_string(muskets) + " " + cargoName(game, cargo::kMuskets), kBlack);
        }
        x += kColumn;
        if (herds != 0) {
            say(screen, art, x, line, std::to_string(herds) + " " + label(art, 45, "Horse Herds"), kBlack);
        }
    }
    drawOk(screen, art);
}

void drawReport(Screen& screen, const Art& art, const Game& game, int nation, int report, int page) {
    switch (report) {
    case 2:
        drawReligionReport(screen, art, game, nation);
        break;
    case 3:
        drawCongressReport(screen, art, game, nation);
        break;
    case 4:
        drawLaborReport(screen, art, game, nation, page - 1);
        break;
    case 5:
        drawEconomyReport(screen, art, game, nation, page);
        break;
    case 6:
        drawColonyAdviser(screen, art, game, nation, page);
        break;
    case 7:
        drawNavalReport(screen, art, game, nation, page);
        break;
    case 8:
        drawForeignReport(screen, art, game, nation);
        break;
    case 9:
        drawIndianReport(screen, art, game, nation);
        break;
    default:
        break;
    }
}

} // namespace viceroy
