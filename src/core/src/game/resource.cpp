// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/game/resource.hpp"

namespace viceroy {

namespace {

constexpr int floorDiv(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

constexpr int floorMod(int a, int b) {
    return ((a % b) + b) % b;
}

} // namespace

// Measured on the original (152 resources, every one on the pattern): the
// map is cut into blocks of 4 × 4 squares. Each block holds two pattern
// squares, (a, b) and (a + 2, b + 2) with the column taken modulo 4. The
// row b is 0 or 1 by the parity of the block, and the column a steps along
// the diagonals of blocks in a sequence of eight.
bool onResourcePattern(int x, int y, int seed) {
    // The seed moves the pattern 4 × (seed + 7) squares to the west; the
    // pattern repeats every 64 squares along a row. Fitted on five games.
    x += floorMod(4 * (seed + 7), 64);
    const int bx = floorDiv(x, 4);
    const int by = floorDiv(y, 4);
    int a = floorMod(x, 4);
    int b = floorMod(y, 4);
    if (b >= 2) {
        a = (a + 2) % 4;
        b -= 2;
    }
    if (b != floorMod(bx + by + 1, 2)) {
        return false;
    }
    // bx - b - by is odd here; e counts the diagonals of block pairs.
    const int e = floorMod(floorDiv(bx - b - by + 9, 2), 8);
    const int step = 2 * (e & 1) + ((e >> 1) & 1) + 2 * ((e >> 2) & 1);
    return a == floorMod(step + by, 4);
}

Resource resourceAt(const SaveMap& map, int x, int y) {
    if (!map.inside(x, y)) {
        return Resource::None;
    }
    const auto& sq = map.at(x, y);
    constexpr std::uint8_t kNoFishOrSilver = 0x04;
    const bool marked = (sq.flags & kNoFishOrSilver) != 0;
    if (sq.terrain == 25) {
        return onResourcePattern(x, y, map.resourceSeed) && !marked ? Resource::Fish : Resource::None;
    }
    if (!sq.isLand() || sq.terrain == 24) {
        return Resource::None;
    }
    if (sq.mountains()) {
        return onResourcePattern(x, y, map.resourceSeed) ? (marked ? Resource::DepletedSilver : Resource::Silver) : Resource::None;
    }
    if (marked) {
        // Read from the original: a used-up resource (minerals that were
        // mined out) is gone; only silver leaves something behind.
        return Resource::None;
    }
    if (sq.hills()) {
        return onResourcePattern(x, y, map.resourceSeed) ? Resource::HillsOre : Resource::None;
    }
    const int ground = sq.terrain & 7;
    if (sq.isForest()) {
        if (!onResourcePattern(x - 4, y, map.resourceSeed)) {
            return Resource::None;
        }
        static constexpr Resource kForest[8] = {Resource::Deer,   Resource::Oasis,  Resource::Beaver,
                                                Resource::Deer,   Resource::Timber, Resource::Timber,
                                                Resource::Minerals, Resource::Minerals};
        return kForest[ground];
    }
    if (!onResourcePattern(x, y, map.resourceSeed)) {
        return Resource::None;
    }
    // Tundra (0) and marsh (6) never showed one; grain and minerals are
    // guesses.
    static constexpr Resource kOpen[8] = {Resource::Grain,   Resource::Oasis, Resource::Grain,    Resource::Cotton,
                                          Resource::Tobacco, Resource::Sugar, Resource::Minerals, Resource::Minerals};
    return kOpen[ground];
}

bool rumourAt(const SaveMap& map, int x, int y) {
    // Read from the original: the squares of a pattern set by the resource
    // seed, on land that is not arctic, where nobody stands.
    if (!map.inside(x, y) || map.resourceSeed == 0) {
        return false;
    }
    const auto& sq = map.at(x, y);
    if (!sq.isLand() || sq.terrain == 24 || (sq.region >> 4) != 0x0F) {
        return false;
    }
    const int v = ((y >> 2) * 19 + (x >> 2) * 17 + map.resourceSeed + 8) & 31;
    return v - ((x & 3) << 2) == (y & 3);
}

} // namespace viceroy
