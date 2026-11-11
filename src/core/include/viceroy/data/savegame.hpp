// SPDX-License-Identifier: GPL-3.0-or-later
//
// Savegames of the original game (COLONY00.SAV to COLONY09.SAV).
// See docs/formats/savegame.md. The parts that are understood are decoded;
// every record also keeps its raw bytes, so that writeSaveGame() gives back
// the same file byte for byte when nothing was changed.
#pragma once

#include "viceroy/data/madspack.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace viceroy {

// Nations as the save numbers them: the four European powers, then the
// eight tribes.
inline constexpr int kEuropeanNations = 4;
inline constexpr int kNoNation = 0xFF;

struct MapSquare {
    std::uint8_t terrain = 0; // 0-7 open land, 8-15 forest of the same, 24 arctic, 25 ocean, 26 sea lane
    std::uint8_t relief = 0;  // hills, mountains and rivers; see docs/formats/savegame.md
    std::uint8_t flags = 0;   // the "mask" layer: units, colony, road, plowed, ...
    std::uint8_t region = 0;  // the "path" layer
    std::uint8_t seen = 0;    // the "seen" layer: who has explored the square

    // Mask bits.
    static constexpr std::uint8_t kHasUnit = 0x01;
    static constexpr std::uint8_t kHasColony = 0x02;
    static constexpr std::uint8_t kRoad = 0x08;
    static constexpr std::uint8_t kPlowed = 0x40;

    bool isLand() const { return terrain <= 24; }
    bool isWater() const { return terrain == 25 || terrain == 26; }
    bool isForest() const { return terrain >= 8 && terrain < 24; }
    bool hills() const { return relief == 1 || relief == 3; }
    bool mountains() const { return relief == 5; }
    bool minorRiver() const { return relief == 2 || relief == 3; }
    bool majorRiver() const { return relief == 6; }
    bool river() const { return minorRiver() || majorRiver(); }
    // Explored by a European nation (0-3).
    bool seenBy(int nation) const { return (seen >> (4 + nation)) & 1u; }
};

struct SaveMap {
    int width = 0;
    int height = 0;
    std::vector<MapSquare> squares; // row by row

    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }
    const MapSquare& at(int x, int y) const {
        return squares[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
    }
    MapSquare& at(int x, int y) {
        return squares[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
    }
};

struct SavePlayer {
    std::string name;    // the leader
    std::string country; // the name of the new land
    int control = 0;     // 0 human, 1 computer, 2 withdrawn
    std::array<std::uint8_t, 52> raw{};
};

struct SaveNation {
    int taxRate = 0;
    int gold = 0;
    std::array<std::uint8_t, 316> raw{};
};

struct SaveColony {
    int x = 0;
    int y = 0;
    std::string name;
    int nation = 0;
    int population = 0;
    std::array<std::uint8_t, 202> raw{};
};

struct SaveUnit {
    int x = 0;
    int y = 0;
    int type = 0;       // row of @UNIT in NAMES.TXT
    int nation = 0;     // 0-3 European, 4-11 tribes
    int movesUsed = 0;  // in thirds: a road move costs 1
    int orders = 0;     // row of @ORDERS
    int profession = 0; // for colonists: their skill (row of the expert list)
    int holds = 0;      // cargo holds in use
    int nextInTransport = -1;
    int prevInTransport = -1;
    std::array<std::uint8_t, 28> raw{};

    // Orders.
    static constexpr int kNoOrders = 0;
    static constexpr int kSentry = 1;
    static constexpr int kFortify = 5;
    static constexpr int kFortified = 6;
};

struct SaveSettlement {
    int x = 0;
    int y = 0;
    int nation = 0; // 4-11
    int population = 0;
    std::array<std::uint8_t, 18> raw{};
};

struct SaveGame {
    int year = 0;
    int season = 0; // 0 spring, 1 autumn
    int turn = 0;
    int difficulty = 0;
    int activeUnit = -1;
    int humanNation = 0;
    SaveMap map;
    std::array<SavePlayer, kEuropeanNations> players;
    std::array<SaveNation, kEuropeanNations> nations;
    std::vector<SaveColony> colonies;
    std::vector<SaveUnit> units;
    std::vector<SaveSettlement> settlements;

    // Blocks still kept as they are.
    std::array<std::uint8_t, 158> header{};
    std::array<std::uint8_t, 24> other{};
    Bytes tribesAndMore; // 8 × 78 + 727 bytes
    std::array<Bytes, 4> layers; // terrain, mask, path, seen as read
    Bytes tail;          // what follows the map
};

SaveGame readSaveGame(std::span<const std::uint8_t> file);

// Writes a save the original game can read. Decoded fields are written over
// the raw bytes they came from.
Bytes writeSaveGame(const SaveGame& save);

} // namespace viceroy
