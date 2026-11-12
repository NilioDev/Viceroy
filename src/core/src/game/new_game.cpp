// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/game/new_game.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/game/resource.hpp"

#include <cstdlib>

namespace viceroy {

namespace {

constexpr int kMaxSide = 256;
constexpr std::uint8_t kArctic = 24;
constexpr std::uint8_t kOcean = 25;
constexpr std::uint8_t kSeaLane = 26;
constexpr std::uint8_t kNoFish = 0x04;

int u16(std::span<const std::uint8_t> b, std::size_t at) {
    return b[at] | (b[at + 1] << 8);
}

} // namespace

SaveMap readMapFile(std::span<const std::uint8_t> file) {
    if (file.size() < 6) {
        throw FormatError("map file too short");
    }
    SaveMap map;
    map.width = u16(file, 0);
    map.height = u16(file, 2);
    if (map.width < 3 || map.height < 3 || map.width > kMaxSide || map.height > kMaxSide) {
        throw FormatError("map file with impossible size");
    }
    const auto layer = static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height);
    if (file.size() < 6 + 3 * layer) {
        throw FormatError("map file ends early");
    }
    map.squares.resize(layer);
    for (std::size_t i = 0; i < layer; ++i) {
        auto& sq = map.squares[i];
        const auto t = file[6 + i];
        sq.terrain = t & 0x1Fu;
        sq.relief = static_cast<std::uint8_t>(t >> 5);
        sq.flags = file[6 + layer + i];
        sq.region = file[6 + 2 * layer + i];
    }
    return map;
}

void prepareStartingMap(SaveMap& map) {
    for (auto& sq : map.squares) {
        if (sq.terrain >= 16 && sq.terrain < 24) {
            sq.terrain = static_cast<std::uint8_t>(sq.terrain - 8);
        }
        if (sq.isForest() && (sq.hills() || sq.mountains())) {
            sq.terrain &= 7;
        }
        // Nobody has been anywhere yet.
        sq.region = static_cast<std::uint8_t>(0xF0 | (sq.region & 0x0F));
        sq.seen = 0;
    }
    for (int x = 0; x < map.width; ++x) {
        for (int y : {0, map.height - 1}) {
            map.at(x, y).terrain = kArctic;
            map.at(x, y).relief = 0;
        }
    }
    for (int y = 1; y < map.height - 1; ++y) {
        map.at(1, y).terrain = kSeaLane;
        map.at(1, y).relief = 0;
    }
    // The original marks the sea without fish after the arctic rows and the
    // second column but before the outermost columns turn to sea lane: those
    // keep the mark where they were ocean.
    markSeaWithoutFish(map);
    for (int y = 1; y < map.height - 1; ++y) {
        for (int x = 0; x < map.width; ++x) {
            auto& sq = map.at(x, y);
            if (x == 0 || x == map.width - 1) {
                sq.terrain = kSeaLane;
                sq.relief = 0;
            }
        }
    }
}

void markSeaWithoutFish(SaveMap& map) {
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            auto& sq = map.at(x, y);
            if (sq.terrain != kOcean || !onResourcePattern(x, y, map.resourceSeed)) {
                continue;
            }
            bool land = false;
            for (int dy = -2; dy <= 2 && !land; ++dy) {
                for (int dx = -2; dx <= 2 && !land; ++dx) {
                    if (std::abs(dx) + std::abs(dy) <= 3 && map.inside(x + dx, y + dy) &&
                        map.at(x + dx, y + dy).isLand() && map.at(x + dx, y + dy).terrain != kArctic) {
                        land = true;
                    }
                }
            }
            if (!land) {
                sq.flags |= kNoFish;
            }
        }
    }
}

} // namespace viceroy
