// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/render/report_view.hpp"

#include "viceroy/game/game.hpp"

#include <algorithm>
#include <string>

namespace viceroy {
namespace {

constexpr std::uint8_t kBlack = 0x00;
constexpr std::uint8_t kWhite = 0x0F;
constexpr std::uint8_t kTitleColour = 0x90;
constexpr std::uint8_t kHeadingColour = 0x92;
constexpr std::uint8_t kNameColour = 0x61;
constexpr std::uint8_t kButtonFrame = 0x77;

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
        constexpr int kRegular = 6, kDragoon = 8, kArtillery = 11, kManOWar = 18;
        drawIconGroups(screen, art,
                       {{unitIcon(kRegular), regulars},
                        {unitIcon(kDragoon), dragoons},
                        {unitIcon(kArtillery), artillery},
                        {unitIcon(kManOWar), menOfWar}},
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

    // The OK box in the corner.
    constexpr int kOkX = 286;
    constexpr int kOkY = 184;
    screen.frame({kOkX, kOkY, 30, 14}, kButtonFrame);
    sayCentred(screen, art, kOkX, 30, kOkY + 7 - art.tiny.height / 2, label(art, 46, "OK"), kHeadingColour);
}

} // namespace viceroy
