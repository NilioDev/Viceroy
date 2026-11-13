// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/game/game.hpp"

using namespace viceroy;

namespace {

Ruleset crossRules() {
    Ruleset r;
    r.terrain.resize(21);
    for (auto& t : r.terrain) {
        t.movementCost = 1;
    }
    r.units.resize(14);
    r.units[unit_type::kColonist] = {"Colonists", 101, 1, 0, 1, 0, 1, 1, 0, 0, 0, 0};
    return r;
}

// The Dutch with no colony, a ship-less map and crosses towards `needed`.
Game crossGame(int crosses, int needed) {
    SaveGame s;
    s.year = 1492;
    s.humanNation = 3;
    s.map.width = 3;
    s.map.height = 3;
    s.map.squares.resize(9);
    for (auto& sq : s.map.squares) {
        sq.terrain = 2;
    }
    auto& dutch = s.nations[3];
    dutch.crosses = crosses;
    dutch.crossesNeeded = needed;
    dutch.recruits = {0x1C, 0x1C, 0x1C};
    return Game(std::move(s), crossRules());
}

} // namespace

VCR_TEST("crosses: two a turn without a colony") {
    Game g = crossGame(0, 10);
    g.endTurn();
    VCR_CHECK(g.state().nations[3].crosses == 2);
    g.endTurn();
    VCR_CHECK(g.state().nations[3].crosses == 4);
    VCR_CHECK(g.unitCount() == 0);
    VCR_CHECK(g.takeNotices().empty());
}

VCR_TEST("crosses: enough crosses bring a colonist from the docks") {
    Game g = crossGame(7, 9);
    g.endTurn();
    const auto& dutch = g.state().nations[3];
    VCR_CHECK(dutch.crosses == 0);
    VCR_CHECK(dutch.crossesNeeded == 9);
    VCR_CHECK(g.unitCount() == 1);
    const auto notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices[0].section == "UNREST" && notices[0].value == 0x1C);
}

VCR_TEST("crosses: nothing comes while the number needed is unknown") {
    Game g = crossGame(50, 0);
    g.endTurn();
    VCR_CHECK(g.unitCount() == 0);
    VCR_CHECK(g.state().nations[3].crosses == 52);
}

VCR_TEST("docks: who comes next follows the original's draw") {
    // Experts come from the shift register in byte 68 of the nation record;
    // professions 1-4, 18, 19, 23 and 25 up are never drawn.
    Game g = crossGame(0, 100);
    auto& dutch = g.state().nations[3];
    dutch.raw[68] = 0x13;
    dutch.raw[69] = 5;
    int experts = 0;
    for (int i = 0; i < 300; ++i) {
        dutch.gold = 100000;
        dutch.recruitCount = 0;
        const int before = dutch.raw[68];
        VCR_CHECK(g.recruit(0) >= 0);
        const int p = dutch.recruits[0];
        const bool unskilled = p == 0x19 || p == 0x1A || p == 0x1C;
        VCR_CHECK(unskilled || (p < 25 && p != 1 && p != 2 && p != 3 && p != 4 && p != 18 && p != 19 && p != 23));
        if (!unskilled) {
            ++experts;
            VCR_CHECK(dutch.raw[68] != before);
        }
    }
    VCR_CHECK(experts > 0);
    // With William Brewster neither criminals nor servants come.
    dutch.raw[7 + 20 / 8] |= 1U << (20 % 8);
    for (int i = 0; i < 200; ++i) {
        dutch.gold = 100000;
        dutch.recruitCount = 0;
        g.recruit(1);
        VCR_CHECK(dutch.recruits[1] != 0x19 && dutch.recruits[1] != 0x1A);
    }
}
