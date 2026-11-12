// SPDX-License-Identifier: GPL-3.0-or-later
//
// Starting a new game: turning a map file (AMER2.MP, the map of America)
// into the map of a game. See docs/rules/new-game.md.
#pragma once

#include "viceroy/data/savegame.hpp"
#include "viceroy/rules/ruleset.hpp"

#include <array>
#include <cstdint>
#include <string>

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

// The dice of a new game: a simple generator, seeded from the clock by the
// caller (or fixed in tests).
class NewGameRandom {
public:
    explicit NewGameRandom(std::uint32_t seed) : state_(seed | 1U) {}
    // 0 to n - 1.
    int below(int n);

private:
    std::uint32_t state_;
};

// The native settlements of a new game on the map of America: each of the
// original's 56 sites (in its order and tribes) moves by up to two squares
// at random onto land without hills or mountains, not next to a settlement
// placed before it. A site with no such square is left out. Tribe levels
// (from @TRIBES) give the size. See docs/rules/new-game.md.
std::vector<SaveSettlement> placeAmericaSettlements(const SaveMap& map, const std::vector<Tribe>& tribes,
                                                    NewGameRandom& random);

// One brave for each settlement, standing on it.
std::vector<SaveUnit> settlementBraves(const std::vector<SaveSettlement>& settlements);

// What the player picks when starting a game.
struct NewGameSetup {
    int humanNation = 3;
    int difficulty = 0;
    std::array<std::string, kEuropeanNations> leaders;   // @LEADERNAME, the human's as typed
    std::array<std::string, kEuropeanNations> countries; // @COLONYNAME
    std::uint32_t seed = 1;
};

// A whole new game on the map of America (the file AMER2.MP), as the
// original sets it up in 1492: the map, the four nations with a ship, a
// soldier and a pioneer each, the market, and the natives. Parts of the
// save the original keeps that Viceroy does not understand yet are left
// empty. See docs/rules/new-game.md.
SaveGame newAmericaGame(std::span<const std::uint8_t> mapFile, const Ruleset& rules, const NewGameSetup& setup);

} // namespace viceroy
