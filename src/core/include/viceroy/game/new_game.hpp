// SPDX-License-Identifier: GPL-3.0-or-later
//
// Starting a new game: turning a map file (AMER2.MP, the map of America)
// into the map of a game. See docs/rules/new-game.md.
#pragma once

#include "viceroy/data/savegame.hpp"

#include <span>

namespace viceroy {

// Reads a .MP map file: width, height, a 16-bit 4, then the terrain, mask
// and path layers. Throws FormatError.
SaveMap readMapFile(std::span<const std::uint8_t> file);

// What the original does to a map file when a game starts: forests in the
// file's second numbering become ordinary forest, hills and mountains lose
// their trees, the top and bottom rows turn to arctic and the outer columns
// to sea lane. Set map.resourceSeed first: the sea without fish is marked
// on the way (markSeaWithoutFish).
void prepareStartingMap(SaveMap& map);

// Sets mask bit 2 on the squares of the resource pattern at sea that are too
// far from land for fish (no land within the 5 × 5 square around them,
// leaving out its corners). Uses map.resourceSeed.
void markSeaWithoutFish(SaveMap& map);

} // namespace viceroy
