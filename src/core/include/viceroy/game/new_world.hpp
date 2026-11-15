// SPDX-License-Identifier: GPL-3.0-or-later
//
// The maps of Start a Game in NEW WORLD: made up at random the way the
// original makes them. See docs/rules/new-world.md.
#pragma once

#include "viceroy/data/savegame.hpp"
#include "viceroy/game/natives.hpp"
#include "viceroy/rules/ruleset.hpp"

#include <array>
#include <cstdint>

namespace viceroy {

// The five settings of a new world. The first four are the rows of the
// Customize screen: land mass (0 small, 1 moderate, 2 large), land form
// (0 archipelago, 1 normal, 2 continents), temperature (0 cool, 1
// temperate, 2 warm) and climate (0 arid, 1 normal, 2 wet); the fifth
// sets how much hill country there is. Start a Game in NEW WORLD deals
// each from 0 to 3, Customize starts them all at 1.
struct NewWorldSettings {
    std::array<int, 5> value{1, 1, 1, 1, 1};
};

struct NewWorld {
    SaveMap map; // terrain, mask (the Pacific, sea without fish) and path (regions) layers
    // The square each nation's ship starts on and sails to from Europe.
    std::array<std::array<int, 2>, 4> start{};
    // How many squares each land region has (index 1 to 15).
    std::array<int, 16> landRegionSizes{};
};

// Makes a new world of 58 × 72 squares. humanNation picks who starts in
// one of the two middle rows.
NewWorld generateNewWorld(const NewWorldSettings& settings, int humanNation, OriginalRandom& random);

// The eight tribes' records at the start of a game: no capital yet, the
// level from @TRIBES, nothing in store, and a tension toward each nation
// of 0 to 14 (two more per difficulty level toward the human player).
// save.tribesAndMore must hold the records.
void initTribes(SaveGame& save, const Ruleset& rules, OriginalRandom& random);

// The native settlements of a new world and a brave for each: a capital
// per tribe far from the others, then more settlements in the 5 × 5 parts
// of the map around them. Marks them on the map (mask and path layers).
void placeNewWorldNatives(SaveGame& save, const Ruleset& rules, OriginalRandom& random);

// What each land square is worth as a colony site to the computer players
// (0 to 15, the low four bits of the seen layer), from the terrain,
// resources and rivers around it and whether it lies by the open sea.
void computeSiteValues(SaveMap& map, const Ruleset& rules);

} // namespace viceroy
