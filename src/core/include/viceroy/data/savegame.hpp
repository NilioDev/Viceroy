// SPDX-License-Identifier: GPL-3.0-or-later
//
// Savegames of the original game (COLONY00.SAV to COLONY09.SAV).
// See docs/formats/savegame.md. Only the parts that are understood are
// decoded; everything else is kept as raw bytes so that a save can later be
// written back unchanged.
#pragma once

#include "viceroy/data/madspack.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace viceroy {

struct MapSquare {
    std::uint8_t terrain = 0; // 0-7 open land, 8-15 forest of the same, 24 arctic, 25 ocean, 26 sea lane
    std::uint8_t relief = 0;  // hills, mountains and rivers; see docs/formats/savegame.md
    std::uint8_t flags = 0;   // the "mask" layer: units, colony, road, plowed, ...
    std::uint8_t region = 0;  // the "path" layer
    std::uint8_t seen = 0;    // the "seen" layer: who has explored the square
};

struct SaveMap {
    int width = 0;
    int height = 0;
    std::vector<MapSquare> squares; // row by row

    const MapSquare& at(int x, int y) const {
        return squares[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
    }
};

struct SaveGame {
    int year = 0;
    int season = 0; // 0 spring, 1 autumn
    int turn = 0;
    int difficulty = 0;
    int tribeCount = 0; // number of native settlements
    int unitCount = 0;
    int colonyCount = 0;
    SaveMap map;
    Bytes raw; // the whole file, for the parts not decoded yet
};

SaveGame readSaveGame(std::span<const std::uint8_t> file);

} // namespace viceroy
