// SPDX-License-Identifier: GPL-3.0-or-later
//
// Draws the home port in Europe the way the original does: the harbour
// picture, ships on their way in either direction, the ship in port and its
// holds, the docks, the market along the bottom, and the Recruit, Purchase
// and Train buttons. Positions were measured on the original; see
// docs/rules/europe.md.
#pragma once

#include "viceroy/game/game.hpp"
#include "viceroy/render/colony_view.hpp"

namespace viceroy {

struct EuropeViewState {
    int ship = -1;     // the ship in port that is selected, -1 for none
    int good = 0;      // the market box with the cursor
};

// The buttons on the right, top to bottom.
inline constexpr Rect kEuropeButtons[3]{{281, 89, 37, 9}, {281, 100, 37, 9}, {281, 111, 37, 9}};

void drawEuropeScreen(Screen& screen, const Art& art, const ColonyArt& colonyArt, const Game& game,
                      const EuropeViewState& view);

} // namespace viceroy
