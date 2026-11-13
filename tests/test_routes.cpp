// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/savegame.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/render/game_view.hpp"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace viceroy;

namespace {

constexpr std::array<int, 8> kDx{0, 1, 1, 1, 0, -1, -1, -1};
constexpr std::array<int, 8> kDy{-1, -1, 0, 1, 1, 1, 0, -1};

} // namespace

VCR_TEST("original files: the route finder brings a colonist across its land") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    SaveGame save = readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {}));
    for (auto& sq : save.map.squares) {
        sq.flags = static_cast<std::uint8_t>(sq.flags & ~(MapSquare::kHasUnit | MapSquare::kHasColony));
        sq.region = static_cast<std::uint8_t>(sq.region | 0xF0);
    }
    Game game(save, art.rules);
    const int index = 0;
    const auto& map = game.state().map;
    std::uint32_t seed = 12345;
    const auto rnd = [&](int n) {
        seed = seed * 1103515245U + 12345U;
        return static_cast<int>((seed >> 16) % static_cast<std::uint32_t>(n));
    };
    const auto run = [&](int type, bool ship, int& tried, int& arrived) {
        for (int trial = 0; trial < 600; ++trial) {
            const int x0 = 1 + rnd(map.width - 2);
            const int y0 = 1 + rnd(map.height - 2);
            const int x1 = 1 + rnd(map.width - 2);
            const int y1 = 1 + rnd(map.height - 2);
            const auto ok = [&](int x, int y) {
                return ship ? map.at(x, y).isWater() && (map.at(x, y).region & 15) == 1 : !map.at(x, y).isWater();
            };
            if (!ok(x0, y0) || !ok(x1, y1) || (!ship && landRegion(map, x0, y0) != landRegion(map, x1, y1)) ||
                (x0 == x1 && y0 == y1)) {
                continue;
            }
            auto& u = game.state().units[index];
            u.type = type;
            u.nation = 3;
            u.x = x0;
            u.y = y0;
            u.raw[9] = static_cast<std::uint8_t>(x1);
            u.raw[10] = static_cast<std::uint8_t>(y1);
            u.raw[11] = 0xFF;
            ++tried;
            for (int step = 0; step < 300; ++step) {
                const int d = game.routeStep(index, 3);
                if (d < 0 || d >= 8) {
                    break;
                }
                u.x += kDx[static_cast<std::size_t>(d)];
                u.y += kDy[static_cast<std::size_t>(d)];
                VCR_CHECK(map.at(u.x, u.y).isWater() == ship);
                if (u.x == x1 && u.y == y1) {
                    ++arrived;
                    break;
                }
            }
        }
    };
    for (const auto& [type, ship] : {std::pair{0, false}, std::pair{5, false}, std::pair{13, true}}) {
        int tried = 0;
        int arrived = 0;
        run(type, ship, tried, arrived);
        std::printf("routes: type %d: %d of %d arrived\n", type, arrived, tried);
        VCR_CHECK(tried > 20);
        VCR_CHECK(arrived * 10 >= tried * 9);
    }
}

VCR_TEST("original files: Go To steps as the original's (measured)") {
    // A unit given Go To in COLONY00.SAV and left to the original for one
    // turn: where it ended, its last step and the thirds it used.
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    const SaveGame save = readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {}));
    struct Case {
        int unit, type, toX, toY, x, y, last, used;
    };
    const Case cases[] = {
        {61, 5, 32, 48, 29, 48, 4, 12},  {61, 0, 32, 48, 27, 45, 3, 3},   {61, 0, 36, 35, 27, 43, 1, 6},
        {61, 0, 44, 56, 27, 44, 2, 9},   {61, 0, 20, 30, 27, 43, 1, 6},   {61, 0, 38, 40, 27, 43, 1, 6},
        {53, -1, 22, 24, 30, 26, 6, 24}, {53, -1, 30, 31, 30, 31, 6, 24}, {53, 13, 50, 40, 41, 29, 4, 12},
    };
    for (const auto& c : cases) {
        Game game(save, art.rules);
        auto& u = game.state().units[static_cast<std::size_t>(c.unit)];
        if (c.type >= 0) {
            u.type = c.type;
        }
        u.orders = 3;
        u.movesUsed = 0;
        u.raw[9] = static_cast<std::uint8_t>(c.toX);
        u.raw[10] = static_cast<std::uint8_t>(c.toY);
        u.raw[11] = 0xFF;
        for (int i = 0; i < 20 && game.movesLeft(c.unit) > 0; ++i) {
            const int d = game.routeStep(c.unit, u.nation);
            if (d < 0 || d >= 8 || game.move(c.unit, kSteps[static_cast<std::size_t>(d)]) != MoveResult::Moved) {
                break;
            }
        }
        VCR_CHECK(u.x == c.x && u.y == c.y);
        VCR_CHECK(u.raw[11] == c.last);
        VCR_CHECK(u.movesUsed == c.used);
    }
}
