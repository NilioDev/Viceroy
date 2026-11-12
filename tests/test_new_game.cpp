// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/game/new_game.hpp"
#include "viceroy/game/resource.hpp"

using namespace viceroy;

namespace {

Bytes mapFile(int w, int h, std::uint8_t terrain) {
    Bytes b{static_cast<std::uint8_t>(w), 0, static_cast<std::uint8_t>(h), 0, 4, 0};
    b.resize(6 + 3 * static_cast<std::size_t>(w * h), 0);
    for (int i = 0; i < w * h; ++i) {
        b[6 + static_cast<std::size_t>(i)] = terrain;
    }
    return b;
}

} // namespace

VCR_TEST("new game: a map file is read and checked") {
    auto file = mapFile(10, 8, 25);
    const auto map = readMapFile(file);
    VCR_CHECK(map.width == 10 && map.height == 8);
    VCR_CHECK(map.at(3, 3).terrain == 25);
    file.resize(20);
    bool threw = false;
    try {
        (void)readMapFile(file);
    } catch (const FormatError&) {
        threw = true;
    }
    VCR_CHECK(threw);
}

VCR_TEST("new game: the starting map is prepared as the original does") {
    auto file = mapFile(20, 20, 16 + 4); // forest of grassland, second numbering
    file[6 + 5 * 20 + 5] = static_cast<std::uint8_t>((5 << 5) | 21); // forest on mountains
    auto map = readMapFile(file);
    prepareStartingMap(map);
    VCR_CHECK(map.at(8, 8).terrain == 12);
    VCR_CHECK(map.at(5, 5).terrain == 5 && map.at(5, 5).mountains());
    VCR_CHECK(map.at(7, 0).terrain == 24 && map.at(7, 19).terrain == 24);
    VCR_CHECK(map.at(0, 7).terrain == 26 && map.at(1, 7).terrain == 26 && map.at(19, 7).terrain == 26);
    VCR_CHECK(map.at(2, 7).terrain == 12);
    VCR_CHECK((map.at(8, 8).region & 0xF0) == 0xF0);
}

VCR_TEST("new game: open sea far from land has no fish") {
    auto map = readMapFile(mapFile(64, 64, 25));
    map.at(32, 32).terrain = 4;
    map.resourceSeed = 78;
    markSeaWithoutFish(map);
    for (int y = 1; y < 63; ++y) {
        for (int x = 1; x < 63; ++x) {
            if (!onResourcePattern(x, y, 78)) {
                VCR_CHECK((map.at(x, y).flags & 4) == 0);
                continue;
            }
            const int dx = x > 32 ? x - 32 : 32 - x;
            const int dy = y > 32 ? y - 32 : 32 - y;
            const bool nearLand = dx <= 2 && dy <= 2 && dx + dy <= 3;
            VCR_CHECK(((map.at(x, y).flags & 4) != 0) == !nearLand);
        }
    }
}
