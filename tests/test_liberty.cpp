// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/game/colony.hpp"
#include "viceroy/game/game.hpp"

using namespace viceroy;

namespace {

SaveColony colonyAt(int rebels, int total, int population) {
    SaveColony c;
    c.rebelDividend = rebels;
    c.rebelDivisor = total;
    c.population = population;
    return c;
}

// One turn of membership, as the original's saves before and after show it.
bool gathers(int rebels, int total, int population, int bells, int wantRebels, int wantTotal) {
    SaveColony c = colonyAt(rebels, total, population);
    gatherBells(c, bells);
    return c.rebelDividend == wantRebels && c.rebelDivisor == wantTotal;
}

Ruleset farmRules() {
    Ruleset r;
    r.terrain.resize(21);
    r.terrain[2].yield[job::kFarmer] = 4;  // plains: 5 food for a colonist
    r.terrain[10].yield[5] = 3;            // its forest: 6 lumber
    r.terrain[17].yield[job::kFisherman] = 3; // ocean
    return r;
}

} // namespace

VCR_TEST("liberty: bells gathered in a turn (measured)") {
    VCR_CHECK(gathers(148, 1271, 10, 4, 150, 1272));
    VCR_CHECK(gathers(107, 292, 2, 1, 106, 292));
    VCR_CHECK(gathers(64, 896, 7, 1, 64, 896));
    VCR_CHECK(gathers(63, 896, 7, 1, 64, 896));       // the wear uses last turn's count
    VCR_CHECK(gathers(128, 640, 5, 1, 126, 640));     // a fifth of the share: 5 x 128 / 640 = 1
    VCR_CHECK(gathers(229, 1146, 9, 1, 227, 1147));
    VCR_CHECK(gathers(1229, 1229, 10, 1, 1206, 1230));
    VCR_CHECK(gathers(1500, 1082, 9, 1, 1084, 1084)); // never more rebels than the total
    VCR_CHECK(gathers(0, 594, 5, 1, 1, 595));
}

VCR_TEST("liberty: rebels and tories are counted as the colony screen shows them") {
    VCR_CHECK(rebelCount(colonyAt(64, 1271, 10)) == 0);   // 5%
    VCR_CHECK(rebelCount(colonyAt(107, 292, 2)) == 1);    // 36%
    VCR_CHECK(rebelCount(colonyAt(576, 1271, 10)) == 4);  // 45%
    VCR_CHECK(rebelCount(colonyAt(260, 1271, 10)) == 2);  // 20%
}

VCR_TEST("liberty: tories cost 1 for every 10 - difficulty of them") {
    VCR_CHECK(workModifier(colonyAt(64, 1271, 10), 0).penalty == 1);
    VCR_CHECK(workModifier(colonyAt(260, 1271, 10), 0).penalty == 0);
    VCR_CHECK(workModifier(colonyAt(0, 1536, 12), 4).penalty == 2);
    VCR_CHECK(workModifier(colonyAt(768, 1536, 12), 4).penalty == 1);
    VCR_CHECK(workModifier(colonyAt(576, 1271, 10), 3).penalty == 0);
}

VCR_TEST("liberty: the bonus climbs one step a turn") {
    SaveColony c = colonyAt(1084, 1084, 9);
    updateLibertyBonus(c);
    VCR_CHECK(c.libertyBonus == 1);
    updateLibertyBonus(c);
    VCR_CHECK(c.libertyBonus == 2);
    c.rebelDividend = 100;
    updateLibertyBonus(c);
    VCR_CHECK(c.libertyBonus == 1);
}

VCR_TEST("liberty: what the bonus and the penalty do to work on the land (measured)") {
    const Ruleset r = farmRules();
    MapSquare plains;
    plains.terrain = 2;
    MapSquare forest;
    forest.terrain = 10;
    MapSquare ocean;
    ocean.terrain = 25;
    constexpr int kColonist = skill::kFreeColonist;
    for (int bonus = 0; bonus <= 2; ++bonus) {
        const WorkModifier m{bonus, 0};
        VCR_CHECK(squareYield(r, plains, job::kFarmer, kColonist, m) == 5 + bonus);
        VCR_CHECK(squareYield(r, plains, job::kFarmer, 0, m) == 7 + 2 * bonus);
        VCR_CHECK(squareYield(r, forest, 5, 5, m) == 12 + 4 * bonus);
        VCR_CHECK(squareYield(r, ocean, job::kFisherman, kColonist, m) == 4 + bonus);
    }
    VCR_CHECK(squareYield(r, ocean, job::kFisherman, 8, {}) == 6);
    VCR_CHECK(squareYield(r, ocean, job::kFisherman, 8, {2, 0}) == 10);
    // The penalty comes off the end, expert or not.
    VCR_CHECK(squareYield(r, plains, job::kFarmer, kColonist, {0, 1}) == 4);
    VCR_CHECK(squareYield(r, forest, 5, 5, {0, 1}) == 11);
    VCR_CHECK(squareYield(r, ocean, job::kFisherman, 8, {0, 1}) == 5);
    // Open water with land on fewer than 3 sides.
    VCR_CHECK(squareYield(r, ocean, job::kFisherman, kColonist, {}, false) == 2);
}

VCR_TEST("liberty: fishing needs land on 3 sides") {
    SaveMap map;
    map.width = 3;
    map.height = 3;
    map.squares.resize(9);
    for (auto& sq : map.squares) {
        sq.terrain = 25;
    }
    VCR_CHECK(landAround(map, 1, 1) == 0);
    map.at(0, 0).terrain = 2;
    map.at(2, 2).terrain = 2;
    VCR_CHECK(landAround(map, 1, 1) == 2);
    map.at(2, 0).terrain = 2;
    VCR_CHECK(landAround(map, 1, 1) == 3);
}

VCR_TEST("liberty: a new colony starts at 0 of 200, a colonist joining adds 100") {
    SaveMap map;
    Ruleset rules;
    SaveColony c;
    c.population = 1;
    startLiberty(c, map, rules, 0);
    VCR_CHECK(c.rebelDividend == 1 && c.rebelDivisor == 199);
    c.population = 2;
    joinLiberty(c, map, rules, 0);
    VCR_CHECK(c.rebelDividend == 2 && c.rebelDivisor == 299);
    SaveColony d = colonyAt(150, 199, 2);
    joinLiberty(d, map, rules, 0);
    VCR_CHECK(d.rebelDividend == 147 && d.rebelDivisor == 299);
}

VCR_TEST("liberty: reaching 50% is reported after the turn") {
    SaveGame s;
    s.year = 1600;
    s.humanNation = 3;
    s.map.width = 3;
    s.map.height = 3;
    s.map.squares.resize(9);
    SaveColony c = colonyAt(330, 640, 5);
    c.x = 1;
    c.y = 1;
    c.nation = 3;
    c.name = "Fort Orange";
    s.colonies.push_back(c);
    s.nations[3].crossesNeeded = 1000;
    Game g(std::move(s), Ruleset{});
    g.endTurn();
    VCR_CHECK(g.state().colonies[0].libertyBonus == 1);
    const auto notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1);
    VCR_CHECK(notices[0].section == "REBELMAJORITY");
    VCR_CHECK(notices[0].numbers == std::vector<int>{50});
    VCR_CHECK(notices[0].colony == 0);
}

VCR_TEST("resources: what they add to work on the land (measured)") {
    Ruleset r;
    r.terrain.resize(21);
    r.terrain[2].yield[job::kFarmer] = 4;     // plains
    r.terrain[3].yield[3] = 3;                // prairie: cotton
    r.terrain[12].yield[5] = 3;               // conifer: lumber
    r.terrain[10].yield[4] = 3;               // mixed forest: furs
    r.terrain[17].yield[job::kFisherman] = 3; // ocean
    MapSquare plains;
    plains.terrain = 2;
    MapSquare prairie;
    prairie.terrain = 3;
    MapSquare conifer;
    conifer.terrain = 12;
    MapSquare mixed;
    mixed.terrain = 10;
    MapSquare ocean;
    ocean.terrain = 25;
    constexpr int kColonist = skill::kFreeColonist;
    // Grain: 5 -> 7, the expert 7 -> 11, with +2 15.
    VCR_CHECK(squareYield(r, plains, job::kFarmer, kColonist, {}, true, Resource::Grain) == 7);
    VCR_CHECK(squareYield(r, plains, job::kFarmer, 0, {}, true, Resource::Grain) == 11);
    VCR_CHECK(squareYield(r, plains, job::kFarmer, 0, {2, 0}, true, Resource::Grain) == 15);
    // Fish: 4 -> 7, the expert 12, with +2 16.
    VCR_CHECK(squareYield(r, ocean, job::kFisherman, kColonist, {}, true, Resource::Fish) == 7);
    VCR_CHECK(squareYield(r, ocean, job::kFisherman, 8, {}, true, Resource::Fish) == 12);
    VCR_CHECK(squareYield(r, ocean, job::kFisherman, 8, {2, 0}, true, Resource::Fish) == 16);
    // Timber: 6 -> 10, the expert 20, with +2 28.
    VCR_CHECK(squareYield(r, conifer, 5, kColonist, {}, true, Resource::Timber) == 10);
    VCR_CHECK(squareYield(r, conifer, 5, 5, {2, 0}, true, Resource::Timber) == 28);
    // Beaver: the expert 12, with +2 16.
    VCR_CHECK(squareYield(r, mixed, 4, 4, {}, true, Resource::Beaver) == 12);
    VCR_CHECK(squareYield(r, mixed, 4, 4, {2, 0}, true, Resource::Beaver) == 16);
    // Cotton doubles after the bonus: the expert 12, with +2 20.
    VCR_CHECK(squareYield(r, prairie, 3, kColonist, {}, true, Resource::Cotton) == 6);
    VCR_CHECK(squareYield(r, prairie, 3, 3, {}, true, Resource::Cotton) == 12);
    VCR_CHECK(squareYield(r, prairie, 3, 3, {2, 0}, true, Resource::Cotton) == 20);
    // A resource does nothing for another job.
    VCR_CHECK(squareYield(r, prairie, 3, kColonist, {}, true, Resource::Grain) == 3);
}
