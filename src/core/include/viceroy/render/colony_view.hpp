// SPDX-License-Identifier: GPL-3.0-or-later
//
// Draws a colony's screen the way the original does: the buildings of the
// town on parchment (top left), the squares around it (top right), its
// people, the harbour, what it produces, and the warehouse along the
// bottom. Positions were measured on the original; see
// docs/rules/colony-screen.md.
#pragma once

#include "viceroy/game/colony.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/render/game_view.hpp"

#include <array>
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
    bool foreign = false;     // another nation's colony that a scout looks round: no changes
    int selectedOutside = -1; // a unit standing outside the colony, picked in the town
    int outsideMenu = -1;     // the highlighted line of the menu of jobs outside, -1 when closed
    bool outsideFromMore = false; // that menu came from "(More)": its first line goes back
    int selectedGood = 0;         // the warehouse's framed good, as in the original (food at first)
    int townLabel = -1;           // the place in the town last pointed at, named in a label; -1 for none
};

// What is under a point of the colony screen, for clicks: a colonist (in
// a building or on a square), a building that has a job, a free square
// around the colony, or the place outside the colony (by the stockade),
// with the unit standing there when one is hit.
struct ColonyHit {
    enum class Kind { None, Colonist, Building, Square, Outside, CustomHouse };
    Kind kind = Kind::None;
    int colonist = -1;
    int job = -1;    // the building's job
    int square = -1; // 0-7
    int unit = -1;   // a unit outside
};
ColonyHit colonyHitAt(const Game& game, const SaveColony& colony, int x, int y, const Art* art = nullptr);

// The place in the town (0-14, in the order of the original's places) under
// the pointer, or -1: its name is shown while the pointer is over the town.
int townPlaceAt(const SaveGame& save, const SaveColony& colony, int x, int y);

// The units standing outside the colony in its town, left to right.
std::vector<int> unitsOutside(const Game& game, const SaveColony& colony);

// The menu of jobs outside the colony: its box, for clicks.
struct OutsideMenuBox {
    int x, y, w, h;
    int firstLine; // y of the first line
};
OutsideMenuBox outsideMenuBox(int lines);
// The jobs menu of a colonist: its first line is "(CLEAR SPECIALTY)" for an
// expert (read from the original), then the jobs, then "(More)".
int jobsMenuClearLine(const SaveColony& colony, int colonist);
// Where a kind of building stands in a colony's town (each colony shuffles
// the spots of each size, read from the original).
std::array<int, 2> townSpot(const SaveGame& save, const SaveColony& colony, BuildingKind kind);

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
