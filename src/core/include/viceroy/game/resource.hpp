// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the map has a resource (fish, ore, a beaver, ...). The savegame does
// not list them: they lie on a fixed pattern of squares, one pattern for
// open ground, hills, mountains and water and the same one four squares
// further east for forests. Where the pattern lies depends on a seed kept
// with the game. Bit 2 of a square's mask marks a pattern square
// of the sea without fish, or a mountain with silver instead of ore. See
// docs/rules/map-drawing.md.
#pragma once

#include "viceroy/data/savegame.hpp"

namespace viceroy {

enum class Resource {
    None,
    Silver,    // mountains with mask bit 2
    Oasis,     // desert, scrub
    Grain,     // plains
    Cotton,    // prairie
    Tobacco,   // grassland
    Sugar,     // savannah
    Minerals,  // swamp, wetland, rain forest
    Fish,      // ocean
    Beaver,    // mixed forest
    Deer,      // broadleaf and boreal forest
    Timber,    // conifer and tropical forest
    Ore,       // mountains
    HillsOre,  // hills
};

// True when (x, y) is a square of the pattern for open ground in a game
// with this seed (SaveMap::resourceSeed). A forest square uses the pattern
// at (x - 4, y).
bool onResourcePattern(int x, int y, int seed);

// The resource on a square of the map, or Resource::None.
Resource resourceAt(const SaveMap& map, int x, int y);

} // namespace viceroy
