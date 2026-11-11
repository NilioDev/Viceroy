// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/data/savegame.hpp"
#include "viceroy/game_files.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace viceroy;

namespace {

using V = std::vector<std::uint8_t>;

void put16(V& d, std::size_t at, unsigned v) {
    d[at] = static_cast<std::uint8_t>(v);
    d[at + 1] = static_cast<std::uint8_t>(v >> 8);
}

// A made-up save: 3×2 map, one colony, two units, one settlement.
V sampleSave() {
    const std::size_t before = 158 + 4 * 52 + 24 + 202 + 2 * 28 + 18 + 4 * 316 + 8 * 78 + 727;
    V d(before + 4 * 6 + 1502, 0);
    std::memcpy(d.data(), "COLONIZE", 9);
    put16(d, 12, 3);
    put16(d, 14, 2);
    put16(d, 26, 1600);
    put16(d, 28, 1);
    put16(d, 30, 42);
    put16(d, 42, 1);
    put16(d, 44, 2);
    put16(d, 46, 1);
    d[54] = 3;
    const std::uint8_t tiles[6] = {24, 25, 0xA2, 0x48, 26, 24}; // arctic, ocean, plains+mountains, forest+minor river...
    for (std::size_t i = 0; i < 6; ++i) {
        d[before + i] = tiles[i];
        d[before + 6 + i] = static_cast<std::uint8_t>(i);  // mask
        d[before + 12 + i] = 7;                            // path
        d[before + 18 + i] = 0xF0;                         // seen
    }
    return d;
}

V readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return V(std::istreambuf_iterator<char>(in), {});
}

bool rejects(const V& d) {
    try {
        readSaveGame(d);
    } catch (const FormatError&) {
        return true;
    }
    return false;
}

} // namespace

VCR_TEST("savegame: header and map layers") {
    const auto save = readSaveGame(sampleSave());
    VCR_CHECK(save.year == 1600);
    VCR_CHECK(save.season == 1);
    VCR_CHECK(save.turn == 42);
    VCR_CHECK(save.difficulty == 3);
    VCR_CHECK(save.colonies.size() == 1 && save.units.size() == 2 && save.settlements.size() == 1);
    VCR_CHECK(save.map.width == 3 && save.map.height == 2);
    VCR_CHECK(save.map.at(0, 0).terrain == 24);
    VCR_CHECK(save.map.at(2, 0).terrain == 2);
    VCR_CHECK(save.map.at(2, 0).relief == 5);
    VCR_CHECK(save.map.at(0, 1).terrain == 8);
    VCR_CHECK(save.map.at(0, 1).relief == 2);
    VCR_CHECK(save.map.at(2, 1).flags == 5);
    VCR_CHECK(save.map.at(1, 1).region == 7);
    VCR_CHECK(save.map.at(1, 1).seen == 0xF0);
}

VCR_TEST("savegame: a save is written back unchanged") {
    const auto bytes = sampleSave();
    VCR_CHECK(writeSaveGame(readSaveGame(bytes)) == bytes);
    auto save = readSaveGame(bytes);
    save.year = 1650;
    save.units[1].x = 2;
    const auto again = readSaveGame(writeSaveGame(save));
    VCR_CHECK(again.year == 1650 && again.units[1].x == 2);
}

VCR_TEST("savegame: lying headers are errors") {
    auto bad = sampleSave();
    bad[0] = 'X';
    VCR_CHECK(rejects(bad));
    auto bigMap = sampleSave();
    put16(bigMap, 12, 5000);
    VCR_CHECK(rejects(bigMap));
    auto manyUnits = sampleSave();
    put16(manyUnits, 44, 60000);
    VCR_CHECK(rejects(manyUnits));
    auto empty = sampleSave();
    put16(empty, 14, 0);
    VCR_CHECK(rejects(empty));
    VCR_CHECK(rejects(V(100, 0)));
}

VCR_TEST("original files: every savegame has a sane map") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    int found = 0;
    for (int i = 0; i < 10; ++i) {
        const auto path = findFileNoCase(dir, "COLONY0" + std::to_string(i) + ".SAV");
        if (!path) {
            continue;
        }
        ++found;
        const auto save = readSaveGame(readAll(*path));
        VCR_CHECK(save.year >= 1492 && save.year <= 1900);
        VCR_CHECK(save.map.width == 58 && save.map.height == 72);
        // The top and bottom rows are arctic, every other square is a known terrain.
        for (int x = 0; x < save.map.width; ++x) {
            VCR_CHECK(save.map.at(x, 0).terrain == 24);
            VCR_CHECK(save.map.at(x, save.map.height - 1).terrain == 24);
        }
        for (const auto& sq : save.map.squares) {
            VCR_CHECK(sq.terrain <= 26);
        }
        // Writing the save back gives the very same bytes.
        const auto bytes = readAll(*path);
        VCR_CHECK(writeSaveGame(save) == bytes);
        for (const auto& unit : save.units) {
            // Units off the map (in Europe, at sea between the two) have
            // equal coordinates above the map.
            VCR_CHECK(save.map.inside(unit.x, unit.y) || (unit.x == unit.y && unit.x >= 200));
            VCR_CHECK(unit.type >= 0 && unit.type < 23);
            VCR_CHECK(unit.nation < 12);
        }
        for (const auto& colony : save.colonies) {
            VCR_CHECK(save.map.at(colony.x, colony.y).flags & MapSquare::kHasColony);
            VCR_CHECK(colony.nation < kEuropeanNations);
        }
    }
    VCR_CHECK(found > 0);
}
