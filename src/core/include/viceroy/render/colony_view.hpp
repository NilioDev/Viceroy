// SPDX-License-Identifier: GPL-3.0-or-later
//
// Draws a colony's screen the way the original does: the buildings of the
// town on parchment (top left), the squares around it (top right), its
// people, the harbour, what it produces, and the warehouse along the
// bottom. Positions were measured on the original; see
// docs/rules/colony-screen.md.
#pragma once

#include "viceroy/game/game.hpp"
#include "viceroy/render/game_view.hpp"

namespace viceroy {

struct ColonyArt {
    SpriteSheet buildings; // BUILDING.SS
    IndexedImage parchment; // PARCH.SS
    IndexedImage background; // COLONY.PIK: the lower part of the screen
    IndexedImage europe;     // EUROPE.PIK: the home port
};

ColonyArt loadColonyArt(const std::filesystem::path& folder);

struct ColonyViewState {
    int colony = 0;
    int selectedColonist = 0; // -1 for none
    int jobsMenu = -1;        // the highlighted line of the jobs menu, -1 when closed
};

void drawColonyScreen(Screen& screen, const Art& art, const ColonyArt& colonyArt, const Game& game,
                      const ColonyViewState& view);

} // namespace viceroy
