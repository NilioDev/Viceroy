// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/game/resource.hpp"

using namespace viceroy;

// Squares seen with a resource in the original (COLONY00 has seed 185,
// COLONY09 seed 119).
VCR_TEST("resources: the pattern matches the original in two games") {
    VCR_CHECK(onResourcePattern(5, 26, 185));  // fish
    VCR_CHECK(onResourcePattern(6, 21, 185));  // fish
    VCR_CHECK(onResourcePattern(4, 15, 185));  // ore
    VCR_CHECK(onResourcePattern(6 - 4, 12, 185)); // timber in a forest
    VCR_CHECK(!onResourcePattern(5, 27, 185));
    VCR_CHECK(!onResourcePattern(6, 22, 185));
    VCR_CHECK(onResourcePattern(8, 33, 119));
    VCR_CHECK(onResourcePattern(11, 38, 119));
    VCR_CHECK(onResourcePattern(18, 3, 119));
    VCR_CHECK(!onResourcePattern(8, 33, 185));
}

VCR_TEST("resources: two per block of 4 x 4, repeating every 64 squares") {
    for (int seed : {0, 119, 185}) {
        for (int by = 0; by < 16; by += 4) {
            for (int bx = 0; bx < 64; bx += 4) {
                int count = 0;
                for (int y = by; y < by + 4; ++y) {
                    for (int x = bx; x < bx + 4; ++x) {
                        count += onResourcePattern(x, y, seed) ? 1 : 0;
                        VCR_CHECK(onResourcePattern(x, y, seed) == onResourcePattern(x + 64, y, seed));
                    }
                }
                VCR_CHECK(count == 2);
            }
        }
    }
}

VCR_TEST("resources: what a square holds") {
    SaveMap map;
    map.width = map.height = 64;
    map.resourceSeed = 185;
    map.squares.resize(64 * 64);
    auto& ocean = map.at(5, 26);
    ocean.terrain = 25;
    VCR_CHECK(resourceAt(map, 5, 26) == Resource::Fish);
    ocean.flags = 0x04; // no fish here
    VCR_CHECK(resourceAt(map, 5, 26) == Resource::None);
    auto& mountain = map.at(4, 15);
    mountain.terrain = 3;
    mountain.relief = 5;
    VCR_CHECK(resourceAt(map, 4, 15) == Resource::Ore);
    mountain.flags = 0x04;
    VCR_CHECK(resourceAt(map, 4, 15) == Resource::Silver);
    map.at(6, 12).terrain = 12; // conifer forest
    VCR_CHECK(resourceAt(map, 6, 12) == Resource::Timber);
    map.at(6, 12).terrain = 4; // the same square cleared
    VCR_CHECK(resourceAt(map, 6, 12) == Resource::None);
    map.at(5, 27).terrain = 25;
    VCR_CHECK(resourceAt(map, 5, 27) == Resource::None);
}
