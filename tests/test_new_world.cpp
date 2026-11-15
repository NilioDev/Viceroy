// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/new_game.hpp"
#include "viceroy/game/new_world.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/rules/ruleset.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace viceroy;

namespace {

std::vector<std::uint8_t> readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

} // namespace

VCR_TEST("new world: the original's dice") {
    // The C library's generator, as the original draws from it.
    OriginalRandom r(1);
    VCR_CHECK(r.next() == 41);
    VCR_CHECK(r.next() == 18467);
    OriginalRandom d(12345);
    for (int i = 0; i < 1000; ++i) {
        const int v = d.between(3, 7);
        VCR_CHECK(v >= 3 && v <= 7);
    }
}

VCR_TEST("new world: a map is made") {
    OriginalRandom r(12345);
    const auto world = generateNewWorld({}, 3, r);
    VCR_CHECK(world.map.width == 58 && world.map.height == 72);
    int land = 0;
    for (const auto& sq : world.map.squares) {
        land += sq.isWater() ? 0 : 1;
    }
    VCR_CHECK(land > 600);
    // Dev aid: VICEROY_NEWWORLD_DUMP="seed p0 p1 p2 p3 p4 human out" writes
    // the terrain, mask and path layers and the start squares.
    if (const char* dump = std::getenv("VICEROY_NEWWORLD_DUMP")) {
        std::istringstream in(dump);
        std::uint32_t seed = 0;
        NewWorldSettings s;
        int human = 0;
        std::string out;
        in >> seed >> s.value[0] >> s.value[1] >> s.value[2] >> s.value[3] >> s.value[4] >> human >> out;
        OriginalRandom d(seed);
        const auto w = generateNewWorld(s, human, d);
        std::ofstream f(out, std::ios::binary);
        for (int layer = 0; layer < 3; ++layer) {
            for (const auto& sq : w.map.squares) {
                const int v = layer == 0 ? sq.terrain | (sq.relief << 5) : layer == 1 ? sq.flags : sq.region;
                f.put(static_cast<char>(v));
            }
        }
        for (const auto& st : w.start) {
            f.put(static_cast<char>(st[0]));
            f.put(static_cast<char>(st[1]));
        }
    }
}

VCR_TEST("original files: a new world's natives and tables") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const auto rules = readRuleset(parseTextFile(readAll(*findFileNoCase(dir, "NAMES.TXT"))));
    // Dev aid: VICEROY_NEWWORLD_GAME="seed nativeSeed p0 p1 p2 p3 p4 human
    // difficulty out" writes the route tables, site values, settlements,
    // braves and tribe records.
    std::string spec = "12345 777 1 1 1 1 1 3 2 -";
    if (const char* env = std::getenv("VICEROY_NEWWORLD_GAME")) {
        spec = env;
    }
    std::istringstream in(spec);
    std::uint32_t seed = 0;
    std::uint32_t nativeSeed = 0;
    NewWorldSettings s;
    int human = 0;
    int difficulty = 0;
    std::string out;
    in >> seed >> nativeSeed >> s.value[0] >> s.value[1] >> s.value[2] >> s.value[3] >> s.value[4] >> human >>
        difficulty >> out;
    OriginalRandom r(seed);
    auto world = generateNewWorld(s, human, r);
    SaveGame save;
    save.map = world.map;
    save.difficulty = difficulty;
    save.humanNation = human;
    for (int n = 0; n < kEuropeanNations; ++n) {
        save.players[static_cast<std::size_t>(n)].control = n == human ? 0 : 1;
    }
    save.tail.assign(1502, 0);
    save.tribesAndMore.assign(8 * 78 + 727, 0);
    Game g(save, rules);
    g.buildRouteTables();
    save = g.state();
    computeSiteValues(save.map, rules);
    OriginalRandom nr(nativeSeed);
    initTribes(save, rules, nr);
    placeNewWorldNatives(save, rules, nr);
    VCR_CHECK(save.settlements.size() >= 8);
    if (out != "-") {
        std::ofstream f(out, std::ios::binary);
        f.write(reinterpret_cast<const char*>(save.tail.data()), 604);
        for (const auto& sq : save.map.squares) {
            f.put(static_cast<char>(sq.seen));
        }
        f.put(static_cast<char>(save.settlements.size()));
        for (const auto& t : save.settlements) {
            f.write(reinterpret_cast<const char*>(t.raw.data()), 4);
        }
        f.put(static_cast<char>(save.units.size()));
        for (const auto& u : save.units) {
            f.put(static_cast<char>(u.x));
            f.put(static_cast<char>(u.y));
            f.put(static_cast<char>(u.nation));
            f.put(static_cast<char>(u.raw[6]));
        }
        f.write(reinterpret_cast<const char*>(save.tribesAndMore.data()), 8 * 78);
    }
}

VCR_TEST("original files: games in new worlds start and play") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const auto rules = readRuleset(parseTextFile(readAll(*findFileNoCase(dir, "NAMES.TXT"))));
    OriginalRandom dice(99);
    for (int round = 0; round < 12; ++round) {
        NewWorldSettings s;
        for (int& v : s.value) {
            v = dice.between(0, 3);
        }
        NewGameSetup setup;
        setup.humanNation = round % 4;
        setup.difficulty = round % 5;
        setup.seed = static_cast<std::uint32_t>(1000 + round * 77);
        const auto save = newWorldGame(s, rules, setup);
        VCR_CHECK(save.settlements.size() >= 8);
        VCR_CHECK(save.units.size() >= 16);
        Game g(save, rules);
        for (int turn = 0; turn < 3; ++turn) {
            g.endTurn();
        }
        // Every nation's ship is still on the map or on its way.
        VCR_CHECK(g.unitCount() >= 16);
    }
}
