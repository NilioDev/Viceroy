// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/savegame.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/resource.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/render/game_view.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>

using namespace viceroy;

VCR_TEST("original files: a unit explores a lost city rumour (read from the original)") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    const SaveGame original = readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {}));
    constexpr int kDutch = 3, kSpain = 2;
    // A rumour with a free land square beside it.
    int rx = -1, ry = -1, nx = -1, ny = -1, direction = -1;
    {
        const Game probe(original, art.rules);
        const auto& map = original.map;
        for (int y = 1; y < map.height - 1 && direction < 0; ++y) {
            for (int x = 1; x < map.width - 1 && direction < 0; ++x) {
                if (!rumourAt(map, x, y) || !probe.unitsAt(x, y).empty()) {
                    continue;
                }
                for (std::size_t d = 0; d < kSteps.size(); ++d) {
                    const int bx = x - kSteps[d].dx;
                    const int by = y - kSteps[d].dy;
                    if (map.inside(bx, by) && map.at(bx, by).isLand() && probe.unitsAt(bx, by).empty() &&
                        !probe.colonyAt(bx, by) && !probe.settlementAt(bx, by) && !rumourAt(map, bx, by)) {
                        rx = x, ry = y, nx = bx, ny = by, direction = static_cast<int>(d);
                        break;
                    }
                }
            }
        }
    }
    VCR_CHECK(direction >= 0);
    if (direction < 0) {
        return;
    }
    // A colonist of the nation steps onto it.
    const auto explorerOf = [&](int turn, int nation, int type) {
        SaveGame save = original;
        save.turn = turn;
        SaveUnit u = save.units[0];
        for (const auto& other : save.units) {
            if (other.nation == kDutch && other.type == 0) {
                u = other;
                break;
            }
        }
        u.x = nx;
        u.y = ny;
        u.nation = nation;
        u.type = type;
        u.movesUsed = 0;
        u.orders = 0;
        save.units.push_back(u);
        return save;
    };
    std::map<std::string, int> seen;
    for (int turn = 0; turn < 600; ++turn) {
        Game game(explorerOf(turn, kDutch, 0), art.rules);
        const int index = game.unitCount() - 1;
        const int units = game.unitCount();
        const int gold = game.state().nations[kDutch].gold;
        const auto result = game.move(index, kSteps[static_cast<std::size_t>(direction)]);
        VCR_CHECK(!rumourAt(game.state().map, rx, ry)); // explored, it is gone
        auto notices = game.takeNotices();
        VCR_CHECK(!notices.empty());
        if (notices.empty()) {
            continue;
        }
        const auto& n = notices[0];
        ++seen[n.section];
        if (n.section == "LOSTCITY5") {
            VCR_CHECK(result == MoveResult::Lost);
            VCR_CHECK(game.unitCount() == units - 1);
        } else {
            VCR_CHECK(result == MoveResult::Moved);
        }
        if (n.section == "LOSTCITY3" || n.section == "LOSTCITY7") {
            VCR_CHECK(n.numbers[0] > 0);
            VCR_CHECK(game.state().nations[kDutch].gold == gold + n.numbers[0]);
        }
        if (n.section == "LOSTCITY2") {
            VCR_CHECK(game.unitCount() == units + 1);
            VCR_CHECK(game.unit(units).type == 10 && game.unit(units).profession * 100 == n.numbers[1]);
        }
        if (n.section == "LOSTCITY9") {
            VCR_CHECK(game.unitCount() == units + 1);
            VCR_CHECK(n.countries.size() == 1 && n.countries[0].second == kDutch);
        }
        if (n.section == "LOSTCITY1") {
            VCR_CHECK(notices.size() == 9 && notices[8].section == "LOSTCITY0");
            VCR_CHECK(game.recruitFree(0) == units);
            VCR_CHECK(game.state().nations[kDutch].gold == gold);
        }
        if (n.section == "LOSTCITY4") {
            // The first vanishing of a nation is burial mounds; searched.
            VCR_CHECK(game.burialWaiting());
            game.answerBurial(true);
            VCR_CHECK(!game.burialWaiting());
            const auto found = game.takeNotices();
            VCR_CHECK(!found.empty() && found[0].section.rfind("BURIAL", 0) == 0);
            ++seen[found.empty() ? "" : found[0].section];
        }
        if (n.section == "LOSTCITY6") {
            VCR_CHECK(game.state().nations[kDutch].gold == gold && game.unitCount() == units);
        }
    }
    VCR_CHECK(seen["LOSTCITY3"] > 0 && seen["LOSTCITY4"] > 0 && seen["LOSTCITY6"] > 0 && seen["LOSTCITY7"] > 0);
    VCR_CHECK(seen["LOSTCITY9"] > 0);
    {
        // A computer player hears nothing of it.
        Game game(explorerOf(5, kSpain, 0), art.rules);
        game.move(game.unitCount() - 1, kSteps[static_cast<std::size_t>(direction)]);
        VCR_CHECK(game.takeNotices().empty());
        VCR_CHECK(!rumourAt(game.state().map, rx, ry));
    }
    {
        // A seasoned scout with de Soto never vanishes nor finds nothing.
        for (int turn = 0; turn < 100; ++turn) {
            SaveGame save = explorerOf(turn, kDutch, 5);
            save.units.back().profession = 0x16;
            save.nations[kDutch].raw[7] |= 0x80; // Hernando de Soto (bit 7 of the founding fathers)
            Game game(save, art.rules);
            game.move(game.unitCount() - 1, kSteps[static_cast<std::size_t>(direction)]);
            for (const auto& n : game.takeNotices()) {
                VCR_CHECK(n.section != "LOSTCITY5" && n.section != "LOSTCITY6" && n.section != "LOSTCITY8");
            }
        }
    }
}
