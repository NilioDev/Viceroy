// SPDX-License-Identifier: GPL-3.0-or-later
//
// The reports of the REPORTS menu, drawn as the original draws them.
#pragma once

#include "viceroy/render/game_view.hpp"
#include "viceroy/render/screen.hpp"

namespace viceroy {

class Game;

// A row of one icon standing for `count` things, `have` of them drawn,
// squeezed into `width` pixels as the original's bell bar is (the number of
// those drawn on a black box at the left when they are packed tight).
void drawIconBar(Screen& screen, const Art& art, int icon, int count, int have, int x, int y, int width);

// Groups of icons side by side, each icon standing for one thing, squeezed
// into `width` pixels with `gap` pixels between the groups. With `numbers`
// each group shows its count on a black box; without, only a group packed
// tight does.
struct IconGroup {
    int icon = 0;
    int count = 0;
};
void drawIconGroups(Screen& screen, const Art& art, const std::vector<IconGroup>& groups, int x, int y, int width,
                    int gap, bool numbers);

// F3: the Continental Congress report for the nation.
void drawCongressReport(Screen& screen, const Art& art, const Game& game, int nation);

} // namespace viceroy
