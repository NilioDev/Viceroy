// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/savegame.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/render/game_view.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace viceroy;

namespace {

constexpr std::size_t kExtra = static_cast<std::size_t>(kTribes) * kTribeRecordSize;

Ruleset aiRules() {
    Ruleset r;
    r.terrain.resize(26);
    for (auto& t : r.terrain) {
        t.movementCost = 1;
    }
    r.units.resize(19);
    r.units[unit_type::kColonist] = {"Colonists", 101, 1, 0, 1, 0, 1, 1, 0, 0, 0, 0};
    r.units[unit_type::kSoldier] = {"Soldiers", 102, 1, 2, 2, 0, 1, 1, 0, 0, 0, 0};
    r.units[unit_type::kDragoon] = {"Dragoons", 105, 4, 3, 3, 0, 1, 1, 0, 0, 0, 0};
    r.units[unit_type::kCaravel] = {"Caravel", 6, 4, 0, 2, 2, 99, 4, 4, 0, 4, 0};
    r.units[14] = {"Merchantman", 7, 5, 0, 6, 4, 99, 4, 4, 0, 4, 0};
    r.units[17] = {"Frigate", 16, 6, 16, 16, 4, 99, 4, 4, 0, 4, 0};
    r.cargo.resize(16);
    return r;
}

// A small world: water to the west, land to the east, every nation played
// by the computer but the Dutch.
SaveGame aiSave() {
    SaveGame s;
    s.year = 1600;
    s.turn = 10;
    s.humanNation = 3;
    s.map.width = 12;
    s.map.height = 10;
    s.map.squares.resize(120);
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 12; ++x) {
            auto& sq = s.map.at(x, y);
            sq.terrain = x < 4 ? 25 : 2;
            sq.region = x < 4 ? 0xF1 : 0xF2; // nobody there; the ocean and region 2
        }
    }
    for (int n = 0; n < 4; ++n) {
        s.players[static_cast<std::size_t>(n)].control = n == 3 ? 0 : 1;
        auto& nation = s.nations[static_cast<std::size_t>(n)];
        nation.prices.fill(2);
        nation.crossesNeeded = 8;
        nation.raw[50] = 3;
        nation.raw[51] = 5;
    }
    s.tribesAndMore.assign(kExtra + 727, 0);
    s.tail.assign(1502, 0);
    return s;
}

SaveUnit unitAt(int x, int y, int type, int nation) {
    SaveUnit u;
    u.x = x;
    u.y = y;
    u.type = type;
    u.nation = nation;
    u.raw[6] = 0xFF;
    return u;
}

} // namespace

VCR_TEST("european ai: the count of a nation's units, people and colonies") {
    SaveGame s = aiSave();
    constexpr int kFrench = 1;
    s.units.push_back(unitAt(6, 4, unit_type::kSoldier, kFrench));
    s.units.push_back(unitAt(7, 4, unit_type::kColonist, kFrench));
    s.units.push_back(unitAt(2, 4, unit_type::kCaravel, kFrench));
    s.units.push_back(unitAt(8, 4, unit_type::kColonist, 2)); // a Spanish colonist
    SaveColony c;
    c.x = 9;
    c.y = 6;
    c.nation = kFrench;
    c.population = 3;
    s.colonies.push_back(c);
    Game g(s, aiRules());
    g.nationCensus(kFrench);
    const auto& b = g.state().tribesAndMore;
    const auto at = [&](std::size_t off) { return int{b[kExtra + off]}; };
    VCR_CHECK(at(12 + kFrench) == 3);                          // units
    VCR_CHECK(at(16 + kFrench) == 1);                          // colonies
    VCR_CHECK(at(20 + kFrench) == 1);                          // free colonists
    VCR_CHECK(at(24 + kFrench) == 3);                          // people in colonies
    VCR_CHECK(at(28 + kFrench) == 5);                          // people: 2 units and 3 in the colony
    VCR_CHECK(at(36 + kFrench) == 2 && at(40 + kFrench) == 1); // the caravel's holds
    VCR_CHECK(at(52 + kFrench) == 0);                          // no armed ship
    VCR_CHECK(at(64 + 19 * kFrench + unit_type::kSoldier) == 1);
    VCR_CHECK(at(44 + 2 * kFrench) == 16);      // the soldier's attack, 2 x 8
    VCR_CHECK(at(156 + 2) == 0x02);             // a Spanish unit in region 2
    VCR_CHECK(at(172 + 16 * kFrench + 2) == 2); // two French land units there
    VCR_CHECK(at(236 + 16 * kFrench + 2) == 1); // and the colony
    VCR_CHECK(at(556 + 2 * kFrench) == 3);      // three people a colony
}

VCR_TEST("european ai: armed ships of others near a colony are noticed") {
    SaveGame s = aiSave();
    constexpr int kFrench = 1;
    SaveColony c;
    c.x = 4;
    c.y = 4;
    c.nation = kFrench;
    c.population = 2;
    s.colonies.push_back(c);
    s.map.at(4, 4).flags |= MapSquare::kHasColony;
    s.map.at(4, 4).region = static_cast<std::uint8_t>(kFrench << 4 | 2);
    // A Dutch frigate two squares off is too far: the route's cost, in
    // thirds of a move, must be 5 at most.
    s.units.push_back(unitAt(2, 4, 17, 3));
    {
        Game g(s, aiRules());
        g.nationCensus(kFrench);
        VCR_CHECK((g.state().colonies[0].raw[0x1B] & 0x03) == 0);
    }
    s.units[0].x = 3; // next to the colony
    Game g(s, aiRules());
    g.nationCensus(kFrench);
    VCR_CHECK((g.state().colonies[0].raw[0x1B] & 0x03) == 0x02);
}

VCR_TEST("european ai: how many more colonies a nation wants") {
    SaveGame s = aiSave();
    Game g(s, aiRules());
    // No colony yet: it wants one, as the original says, with weight 8.
    VCR_CHECK(g.aiColonyNeed(0) == 8);
}

VCR_TEST("european ai: a nation without ships buys a caravel and it sails") {
    SaveGame s = aiSave();
    constexpr int kEnglish = 0;
    s.nations[kEnglish].gold = 0;
    Game g(s, aiRules());
    g.europeanTurn(kEnglish);
    int caravels = 0;
    for (const auto& u : g.state().units) {
        if (u.nation == kEnglish && u.type == unit_type::kCaravel) {
            ++caravels;
            VCR_CHECK(europePlace(u) == EuropePlace::ToNewWorld);
            VCR_CHECK(u.raw[9] == 3 && u.raw[10] == 5); // bound for where its ships come back
        }
    }
    VCR_CHECK(caravels == 1);
    // The Crown made up the price, and it was paid.
    VCR_CHECK(g.state().nations[kEnglish].gold == 0);
}

VCR_TEST("european ai: the Crown's gold for a computer nation") {
    SaveGame s = aiSave();
    constexpr int kSpanish = 2;
    s.turn = 40;
    s.year = 1650;
    s.difficulty = 2;
    s.nations[kSpanish].gold = 0;
    s.units.push_back(unitAt(2, 2, 14, kSpanish)); // a merchantman, so no ship is bought
    SaveColony c;
    c.x = 8;
    c.y = 8;
    c.nation = kSpanish;
    c.population = 1;
    s.colonies.push_back(c);
    s.colonies.push_back(c);
    s.colonies[1].x = 10;
    Game g(s, aiRules());
    g.europeanTurn(kSpanish);
    // (1650 - 1500) / 50 + 2 colonies = 5, times the level 2, times 4.
    VCR_CHECK(g.state().nations[kSpanish].gold == 40);
}

VCR_TEST("european ai: the player's nation and withdrawn ones take no computer turn") {
    SaveGame s = aiSave();
    s.players[1].control = 2;
    Game g(s, aiRules());
    g.europeanTurn(3);
    g.europeanTurn(1);
    VCR_CHECK(g.state().units.empty());
}

VCR_TEST("original files: a nation's count matches the save (measured)") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    // Saved during the player's turn, after the player's count: it gives
    // every byte the original keeps, the region flags too.
    for (const char* name : {"COLONY03.SAV", "COLONY05.SAV", "COLONY09.SAV"}) {
        const auto file = findFileNoCase(dir, name);
        if (!file) {
            continue;
        }
        std::ifstream in(*file, std::ios::binary);
        const SaveGame save = readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {}));
        Game g(save, art.rules);
        g.nationCensus(save.humanNation);
        VCR_CHECK(g.state().tribesAndMore == save.tribesAndMore);
    }
}
