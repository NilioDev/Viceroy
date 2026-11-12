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

#include <vector>

namespace viceroy {

struct ColonyArt {
    SpriteSheet buildings; // BUILDING.SS
    IndexedImage parchment; // PARCH.SS
    IndexedImage background; // COLONY.PIK: the lower part of the screen
    IndexedImage europe;     // EUROPE.PIK: the home port
};

ColonyArt loadColonyArt(const std::filesystem::path& folder);

// What the panel right of the harbour shows; the three buttons beside it
// switch between them.
enum class ColonyPanel { Production, Units, Make };

struct ColonyViewState {
    int colony = 0;
    int selectedColonist = 0; // -1 for none
    int jobsMenu = -1;        // the highlighted line of the jobs menu, -1 when closed
    ColonyPanel panel = ColonyPanel::Production;
    int buildList = -1;       // the highlighted line of the list of things to build, -1 when closed
    int selectedUnit = -1;    // the unit clicked on the Units Present panel, -1 for none
};

// Where the panel's parts are, for clicks.
struct PanelButton {
    int x, y, w, h;
};
inline constexpr PanelButton kPanelButtons[3] = {{303, 132, 15, 14}, {303, 147, 15, 14}, {303, 162, 15, 14}};
inline constexpr PanelButton kChangeButton{270, 138, 29, 9};
inline constexpr PanelButton kBuyButton{216, 138, 17, 9};

// The list of things to build: the box and the first line of items (the
// line above them is "(No Production)").
struct BuildListBox {
    int x, y, w, h;
    int firstLine; // y of "(No Production)"
};
BuildListBox buildListBox(int items);

void drawColonyScreen(Screen& screen, const Art& art, const ColonyArt& colonyArt, const Game& game,
                      const ColonyViewState& view);

// The land units in a colony as the Units Present panel shows them: the
// last on the square first.
std::vector<int> unitsPresent(const Game& game, const SaveColony& colony);
// Which of the five units in front of the Units Present panel is at a
// point on the screen, or -1.
int unitPresentAt(int x, int y);

} // namespace viceroy
