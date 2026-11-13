// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/savegame.hpp"
#include "viceroy/game/colony.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"

#include <string>

using namespace viceroy;

namespace {

constexpr int kHuman = 3;
constexpr int kComputer = 0;

// Made-up fathers: five of each category, every one with weight 1.
Ruleset congressRules() {
    Ruleset r;
    r.terrain.resize(26);
    r.units.resize(19);
    r.units[17] = {"Frigate", 16, 6, 16, 16, 4, 99, 4, 4, 0, 4, 0};
    r.cargo.resize(16);
    for (int f = 0; f < Game::kFoundingFathers; ++f) {
        r.fathers.push_back({"Father " + std::to_string(f), f / 5, {1, 1, 1}});
    }
    return r;
}

SaveGame congressSave() {
    SaveGame s;
    s.year = 1494;
    s.humanNation = kHuman;
    s.map.width = 12;
    s.map.height = 10;
    s.map.squares.resize(120);
    for (int n = 0; n < 4; ++n) {
        s.players[static_cast<std::size_t>(n)].control = n == kHuman ? 0 : 1;
        auto& raw = s.nations[static_cast<std::size_t>(n)].raw;
        raw[0x12] = 0xFF; // nobody up for election
        raw[0x13] = 0xFF;
    }
    for (int f = 0; f < Game::kFoundingFathers; ++f) {
        s.header[57 + static_cast<std::size_t>(f)] = 0xFF;
    }
    s.tribesAndMore.assign(static_cast<std::size_t>(kTribes) * kTribeRecordSize + 727, 0);
    s.tail.assign(1502, 0);
    return s;
}

// The player's nation has every father but this one, so it alone is up
// for election, and gets it.
void joinOnly(Game& g, int father) {
    auto& raw = g.state().nations[kHuman].raw;
    for (int f = 0; f < Game::kFoundingFathers; ++f) {
        if (f != father) {
            raw[7 + static_cast<std::size_t>(f / 8)] |= static_cast<std::uint8_t>(1U << (f % 8));
        }
    }
    g.addCongressBells(kHuman, 1000);
    g.chooseFather(father / 5);
}

} // namespace

VCR_TEST("congress: the bells for the next father") {
    SaveGame s = congressSave();
    Game g(s, congressRules());
    // The player at the easiest level: 6 x 8, half of it for the first.
    VCR_CHECK(g.bellsForNextFather(kHuman) == 24);
    g.state().nations[kHuman].raw[0x14] = 2;
    VCR_CHECK(g.bellsForNextFather(kHuman) == 3 * 48 + 1);
    // The hardest level in 1700: 14 x 8 for the player, half as much again
    // from 1600, 1650 and 1700.
    g.state().difficulty = 4;
    g.state().year = 1700;
    g.state().nations[kHuman].raw[0x14] = 1;
    VCR_CHECK(g.bellsForNextFather(kHuman) == 2 * 378 + 1);
    // The computer's nations need fewer the harder the level.
    g.state().year = 1500;
    VCR_CHECK(g.bellsForNextFather(kComputer) == (10 * 8 + 1) / 2);
}

VCR_TEST("congress: a computer nation elects a father and he joins") {
    SaveGame s = congressSave();
    Game g(s, congressRules());
    g.addCongressBells(kComputer, 10);
    const int father = g.fatherInElection(kComputer);
    VCR_CHECK(father >= 0);
    VCR_CHECK(g.congressBells(kComputer) == 10);
    VCR_CHECK(!g.fatherElectionWaiting());
    g.addCongressBells(kComputer, 46); // 56 for the first at the easiest level
    VCR_CHECK(g.hasFather(kComputer, father));
    VCR_CHECK(g.fatherCount(kComputer) == 1);
    VCR_CHECK(g.congressBells(kComputer) == 0);
    VCR_CHECK(g.fatherInElection(kComputer) == -1);
    VCR_CHECK(g.state().header[57 + static_cast<std::size_t>(father)] == kComputer);
    VCR_CHECK(g.takeNotices().empty());
}

VCR_TEST("congress: the player chooses the next father") {
    SaveGame s = congressSave();
    Game g(s, congressRules());
    g.addCongressBells(kHuman, 5);
    VCR_CHECK(g.fatherElectionWaiting());
    auto notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices[0].section == "WHICHFREEDOM");
    for (int c = 0; c < Game::kFatherCategories; ++c) {
        const int f = g.fatherCandidates()[static_cast<std::size_t>(c)];
        VCR_CHECK(f >= 0 && f / 5 == c);
    }
    // While the player has not chosen, the bells still count.
    g.addCongressBells(kHuman, 30);
    VCR_CHECK(g.congressBells(kHuman) == 35);
    const int chosen = g.fatherCandidates()[2];
    g.chooseFather(2);
    // 35 bells are enough for the first: he joins at once.
    VCR_CHECK(g.hasFather(kHuman, chosen));
    VCR_CHECK(g.congressBells(kHuman) == 0);
    notices = g.takeNotices();
    VCR_CHECK(notices.size() == 2 && notices[0].section == "FREEDOM" && notices[1].section == "CONGRESSHALL");
    VCR_CHECK(notices[0].strings[0] == "Father " + std::to_string(chosen));
}

VCR_TEST("congress: what some fathers do when they join") {
    {
        // La Salle: a stockade in colonies of three or more.
        SaveGame s = congressSave();
        SaveColony big;
        big.nation = kHuman;
        big.population = 3;
        SaveColony small = big;
        small.population = 2;
        s.colonies = {big, small};
        Game g(s, congressRules());
        joinOnly(g, 9);
        VCR_CHECK((g.state().colonies[0].buildings & 1) != 0);
        VCR_CHECK((g.state().colonies[1].buildings & 1) == 0);
    }
    {
        // John Paul Jones: a frigate sails for the New World.
        SaveGame s = congressSave();
        s.nations[kHuman].raw[50] = 7;
        s.nations[kHuman].raw[51] = 9;
        Game g(s, congressRules());
        joinOnly(g, 14);
        VCR_CHECK(g.unitCount() == 1);
        const auto& u = g.unit(0);
        VCR_CHECK(u.type == 17 && u.nation == kHuman);
        VCR_CHECK(u.x == kEuropeBase + kHuman + 4 && u.y == u.x);
        VCR_CHECK(u.raw[9] == 7 && u.raw[10] == 9);
    }
    {
        // Pocahontas: the tribes calm down.
        SaveGame s = congressSave();
        setTribeTension(s, 2, kHuman, 60);
        Game g(s, congressRules());
        joinOnly(g, 16);
        VCR_CHECK(tribeTension(g.state(), 2, kHuman) == 0);
    }
    {
        // Las Casas: converts become free colonists.
        SaveGame s = congressSave();
        SaveUnit convert;
        convert.nation = kHuman;
        convert.profession = 0x1B;
        s.units.push_back(convert);
        SaveColony c;
        c.nation = kHuman;
        c.population = 1;
        c.skills[0] = 0x1B;
        s.colonies.push_back(c);
        Game g(s, congressRules());
        joinOnly(g, 24);
        VCR_CHECK(g.unit(0).profession == 0x1C);
        VCR_CHECK(g.state().colonies[0].skills[0] == 0x1C);
    }
}

namespace {

// A colony on plains with one colonist at the given job, and a map of
// plains around it.
struct Workshop {
    SaveMap map;
    SaveColony colony;
    Ruleset rules;
};

Workshop workshop(int jobRow, int skill, BuildingKind building) {
    Workshop w;
    w.map.width = 5;
    w.map.height = 5;
    w.map.squares.resize(25);
    for (auto& sq : w.map.squares) {
        sq.terrain = 2;
    }
    w.rules.terrain.resize(30);
    w.rules.terrain[2].yield[4] = 2; // furs
    w.colony.x = 2;
    w.colony.y = 2;
    w.colony.population = 1;
    w.colony.jobs[0] = static_cast<std::uint8_t>(jobRow);
    w.colony.skills[0] = static_cast<std::uint8_t>(skill);
    setBuildingLevel(w.colony, building, 1);
    return w;
}

NationEffects with(std::initializer_list<int> fathers, int tax = 0) {
    NationEffects e;
    for (const int f : fathers) {
        e.fathers |= 1U << f;
    }
    e.tax = tax;
    return e;
}

int bells(const Workshop& w, const NationEffects& e, bool tories = true) {
    return colonyProduction(w.colony, w.map, w.rules, 0, tories, e).produced[cargo::kBells];
}

} // namespace

VCR_TEST("congress: fathers and the printing press add bells") {
    Workshop w = workshop(job::kStatesman, skill::kFreeColonist, BuildingKind::TownHall);
    const int plain = bells(w, {});
    VCR_CHECK(plain >= 2);
    VCR_CHECK(bells(w, with({15})) == plain + plain / 2);                // Jefferson
    VCR_CHECK(bells(w, with({17}, 50)) == plain + plain * 50 / 100);     // Paine at 50% tax
    VCR_CHECK(bells(w, with({18})) == plain);                            // Bolivar: not for the player
    VCR_CHECK(bells(w, with({18}), false) == plain + (1 + 3) / 5);       // but for a computer nation
    setBuildingLevel(w.colony, BuildingKind::PrintingPress, 1);
    VCR_CHECK(bells(w, {}) == plain + plain / 2);
    setBuildingLevel(w.colony, BuildingKind::PrintingPress, 2);
    VCR_CHECK(bells(w, {}) == 2 * plain);
    const int jefferson = plain + plain / 2;
    VCR_CHECK(bells(w, with({15})) == 2 * jefferson);
}

VCR_TEST("congress: Penn's preachers and Hudson's furs") {
    Workshop w = workshop(job::kPreacher, skill::kFreeColonist, BuildingKind::Church);
    const auto crosses = [&](const NationEffects& e) {
        return colonyProduction(w.colony, w.map, w.rules, 0, true, e).colonists[0].amount;
    };
    const int plain = crosses({});
    VCR_CHECK(plain > 0);
    VCR_CHECK(crosses(with({21})) == plain + plain / 2);

    Workshop f = workshop(4, skill::kFreeColonist, BuildingKind::Church);
    f.colony.squares[0] = 0; // works the square to the north
    const auto furs = [&](const NationEffects& e) {
        return colonyProduction(f.colony, f.map, f.rules, 0, true, e).colonists[0].amount;
    };
    VCR_CHECK(furs({}) == 2);
    VCR_CHECK(furs(with({8})) == 4);
}

VCR_TEST("congress: Magellan's ships and Bolivar's rebels") {
    SaveGame s = congressSave();
    SaveUnit ship;
    ship.nation = kHuman;
    ship.type = 17;
    s.units.push_back(ship);
    Game g(s, congressRules());
    const int plain = g.movementAllowance(0);
    VCR_CHECK(plain == 6 * 3);
    g.state().nations[kHuman].raw[7] |= 1U << 5; // Magellan
    VCR_CHECK(g.movementAllowance(0) == plain + 3);

    NationEffects bolivar = with({18});
    VCR_CHECK(bolivar.libertyExtra(true) == 20);
    VCR_CHECK(bolivar.libertyExtra(false) == 0);
    SaveColony c;
    c.population = 4;
    c.rebelDividend = 1;
    c.rebelDivisor = 2;
    VCR_CHECK(sonsOfLibertyPercent(c, 20) == 70);
    VCR_CHECK(rebelCount(c, 20) == 3);
    c.rebelDividend = 2;
    VCR_CHECK(sonsOfLibertyPercent(c, 20) == 100);
}
