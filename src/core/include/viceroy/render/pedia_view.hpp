// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "viceroy/render/game_view.hpp"

#include <string>
#include <vector>

namespace viceroy {

// The Colonizopedia ("ENCYCLOPEDIA OF COLONIZATION"): an index of names for
// each of its six parts, and a page for each name with the text of
// PEDIA.TXT. The parts, in the order of the menu and of @PEDIA.
namespace pedia {
inline constexpr int kCargo = 0;    // @CARGOn, by row of @CARGO
inline constexpr int kUnit = 1;     // @UNITn, by row of @UNIT
inline constexpr int kTerrain = 2;  // @TERRAINn, by terrain number (0-15, 24-28)
inline constexpr int kSkill = 3;    // @JOBn, by row of @JOB
inline constexpr int kBuilding = 4; // @BUILDINGn, by row of @BUILDING
inline constexpr int kFather = 5;   // @FATHERn, by row of @FATHERS
inline constexpr int kParts = 6;
} // namespace pedia

struct PediaEntry {
    int id = 0; // the n of the page's section
    std::string name;
};

// The names of a part as its index lists them: alphabetical, without the
// rows the original leaves out (seen: the Teacher, the second and third
// Town Hall, the Capitol and its expansion).
std::vector<PediaEntry> pediaEntries(const Art& art, int part);

// The page of a map square: its hills or mountains, forest or ground.
int pediaTerrainOf(const MapSquare& square);

// The name of a terrain number as the index gives it ("Boreal Forest").
std::string pediaTerrainName(const Art& art, int terrain);

// The lines of a page's text as the original breaks them: the name, then
// the paragraphs ("" for an empty line); braces mark the gold words.
std::vector<std::string> pediaPageLines(const Art& art, int part, int id);

struct PediaState {
    int part = pedia::kCargo;
    int selected = 0; // in pediaEntries
    int page = -1;    // the id of the page shown, or -1 for the index
};

// The index: 24 names a column, 100 pixels apart.
inline constexpr int kPediaRows = 24;
// The entry of the index under a point, -1 for none, kPediaExit for
// "(Exit)".
inline constexpr int kPediaExit = -2;
int pediaIndexHit(const Art& art, const PediaState& state, int x, int y);

struct ColonyArt;

// nation: the player's, whose colour the units of the pages wear;
// colonyArt: the buildings' pictures, which the pages of skills and
// buildings show when given.
void drawPedia(Screen& screen, const Art& art, const PediaState& state, int nation,
               const ColonyArt* colonyArt = nullptr);

} // namespace viceroy
