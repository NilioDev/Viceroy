// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/game/game.hpp"

#include <algorithm>

using namespace viceroy;

namespace {

constexpr int kDutch = 3;
constexpr int kFrench = 1;

// A 3 × 3 map of one terrain worth +50% to a defender, the Dutch attacker at
// (1, 0) and the French defender at (1, 1). The unit table has the rows the
// fights need, with the original's attack and combat values.
Game battle(int attackerType, int defenderType, int defenderCombat = -1) {
    SaveGame s;
    s.year = 1600;
    s.humanNation = kDutch;
    s.map.width = 3;
    s.map.height = 3;
    s.map.squares.resize(9);
    SaveUnit a;
    a.x = 1;
    a.y = 0;
    a.type = attackerType;
    a.nation = kDutch;
    a.profession = 0x1C;
    SaveUnit d = a;
    d.y = 1;
    d.type = defenderType;
    d.nation = kFrench;
    s.units = {a, d};
    Ruleset rules;
    rules.units.resize(13);
    const auto set = [&](int row, const char* name, int movement, int attack, int combat) {
        auto& u = rules.units[static_cast<std::size_t>(row)];
        u.name = name;
        u.movement = movement;
        u.attack = attack;
        u.combat = combat;
    };
    set(unit_type::kColonist, "Colonists", 1, 0, 1);
    set(unit_type::kSoldier, "Soldiers", 1, 2, 2);
    set(unit_type::kDragoon, "Dragoons", 4, 3, 3);
    set(11, "Artillery", 1, 7, 5);
    if (defenderCombat >= 0) {
        rules.units[static_cast<std::size_t>(defenderType)].combat = defenderCombat;
    }
    rules.terrain.resize(21);
    rules.terrain[0].defenseBonus = 2; // +50%, like a forest
    return Game(std::move(s), rules);
}

std::vector<int> labels(const CombatSide& side) {
    std::vector<int> out;
    for (const auto& b : side.bonuses) {
        out.push_back(b.label);
    }
    return out;
}

} // namespace

VCR_TEST("combat: the analysis of artillery attacking a dragoon in the open (measured)") {
    using namespace combat_label;
    Game g = battle(11, unit_type::kDragoon);
    const auto a = g.analyseAttack(0, 1);
    VCR_CHECK(a.attacker.base == 7);
    VCR_CHECK(labels(a.attacker) == std::vector<int>{kAttackBonus, kArtilleryInOpen});
    VCR_CHECK(a.defender.base == 3);
    VCR_CHECK(labels(a.defender) == std::vector<int>{kTerrain});
    VCR_CHECK(a.defender.bonuses[0].percent == 50 && a.defender.bonuses[0].terrainPicture);
    // 7 × (100 + 50 - 75) against 3 × (100 + 50), in hundredths.
    VCR_CHECK(a.attacker.strength() == 525);
    VCR_CHECK(a.defender.strength() == 450);
}

VCR_TEST("combat: artillery has no penalty against a fortified unit (measured)") {
    using namespace combat_label;
    Game g = battle(11, unit_type::kDragoon);
    g.state().units[1].profession = 0x15; // a veteran
    g.state().units[1].orders = SaveUnit::kFortified;
    const auto a = g.analyseAttack(0, 1);
    VCR_CHECK(labels(a.attacker) == std::vector<int>{kAttackBonus});
    VCR_CHECK(labels(a.defender) == std::vector<int>{kVeteran, kTerrain, kFortified});
}

VCR_TEST("combat: damaged artillery is weaker, and a second defeat destroys it (measured)") {
    Game g = battle(11, unit_type::kDragoon);
    // With no moves left the attacker fights at nothing and loses.
    g.state().units[0].movesUsed = g.movementAllowance(0);
    VCR_CHECK(g.attack(0, 1) == CombatOutcome::DefenderWon);
    VCR_CHECK(g.isDamagedArtillery(0));
    VCR_CHECK(g.unit(0).type == 11);
    auto notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices[0].section == "ARTILLERY");
    VCR_CHECK(notices[0].strings.at(1) == "Artillery");
    VCR_CHECK(g.analyseAttack(0, 1).attacker.base == 5);
    VCR_CHECK(g.analyseAttack(1, 0).defender.base == 3);

    g.state().units[0].movesUsed = g.movementAllowance(0);
    g.attack(0, 1);
    notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices[0].section == "ARTILLERY2");
    VCR_CHECK(g.unitCount() == 1);
    VCR_CHECK(g.unit(0).nation == kFrench);
}

VCR_TEST("combat: a routed dragoon becomes a soldier and stays fortified (measured)") {
    // A defender of no strength always loses.
    Game g = battle(unit_type::kSoldier, unit_type::kDragoon, 0);
    g.state().units[1].orders = SaveUnit::kFortified;
    VCR_CHECK(g.attack(0, 1) == CombatOutcome::AttackerWon);
    VCR_CHECK(g.unit(1).type == unit_type::kSoldier);
    VCR_CHECK(g.unit(1).orders == SaveUnit::kFortified);
    VCR_CHECK(g.movesLeft(0) == 0);
    const auto notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices[0].section == "DEMOTE");
    VCR_CHECK(notices[0].strings.at(1) == "Dragoons" && notices[0].strings.at(2) == "Soldiers");
    VCR_CHECK(notices[0].adjectives == (std::vector<std::pair<int, int>>{{0, kFrench}}));
}

VCR_TEST("combat: a beaten colonist is captured (measured)") {
    Game g = battle(unit_type::kSoldier, unit_type::kColonist, 0);
    VCR_CHECK(g.attack(0, 1) == CombatOutcome::AttackerWon);
    VCR_CHECK(g.unit(1).nation == kDutch);
    VCR_CHECK(g.unit(1).x == 1 && g.unit(1).y == 0);
    VCR_CHECK(g.unit(1).orders == SaveUnit::kNoOrders);
    const auto notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices[0].section == "COLONISTCAPTURE");
}

VCR_TEST("combat: breaking a treaty ends the peace on both sides (measured)") {
    Game g = battle(unit_type::kSoldier, unit_type::kDragoon);
    g.state().nations[kDutch].raw[52 + kFrench] = 0x40;
    g.state().nations[kFrench].raw[52 + kDutch] = 0x40;
    VCR_CHECK(g.atPeace(kDutch, kFrench));
    g.declareWar(kDutch, kFrench);
    VCR_CHECK(!g.atPeace(kDutch, kFrench));
    VCR_CHECK(!g.atPeace(kFrench, kDutch));
}

VCR_TEST("combat: the numbers a veteran soldier and a dragoon in forest roll on (observed)") {
    // Observed in the original on Discoverer: 80 against 36.
    Game g = battle(unit_type::kSoldier, unit_type::kDragoon);
    g.state().units[0].profession = 0x15;
    g.state().turn = 120;
    const auto s = g.combatStrengths(0, 1);
    VCR_CHECK(s.attack == 80);
    VCR_CHECK(s.defense == 36);
    // On Conquistador the human attacker gets nothing extra.
    g.state().difficulty = 4;
    VCR_CHECK(g.combatStrengths(0, 1).attack == 36);
    // Two thirds of a move left: two thirds of the attack.
    g.state().units[0].movesUsed = 1;
    VCR_CHECK(g.combatStrengths(0, 1).attack == 24);
}

VCR_TEST("combat: early on the easy levels a human defender is attacked at half strength") {
    Game g = battle(unit_type::kSoldier, unit_type::kDragoon);
    g.state().units[0].nation = kFrench;
    g.state().units[1].nation = kDutch;
    g.state().difficulty = 1;
    g.state().turn = 10;
    auto s = g.combatStrengths(0, 1);
    VCR_CHECK(s.attack == 12);  // 16 x 3/2, halved
    VCR_CHECK(s.defense == 39); // 24 x 6/4 + 4 - 1
    g.state().turn = 80;
    VCR_CHECK(g.combatStrengths(0, 1).attack == 24);
}

VCR_TEST("combat: artillery in the open fights at a quarter unless the defender is dug in") {
    Game g = battle(11, unit_type::kDragoon);
    g.state().difficulty = 2;
    g.state().turn = 120;
    VCR_CHECK(g.combatStrengths(0, 1).attack == 21); // (56 x 3/2 + 2) / 4
    g.state().units[1].orders = SaveUnit::kFortify;
    VCR_CHECK(g.combatStrengths(0, 1).attack == 86);
    VCR_CHECK(g.combatStrengths(0, 1).defense == 36); // fortifying is not fortified yet
    g.state().units[1].orders = SaveUnit::kFortified;
    VCR_CHECK(g.combatStrengths(0, 1).defense == 48);
}

VCR_TEST("combat: the roll wins at or below the attack") {
    VCR_CHECK(Game::attackerWins(80, 36, 80));
    VCR_CHECK(!Game::attackerWins(80, 36, 81));
    VCR_CHECK(!Game::attackerWins(0, 36, 1));
}

VCR_TEST("combat: with George Washington every win hardens a soldier (observed)") {
    Game g = battle(unit_type::kDragoon, unit_type::kSoldier);
    g.state().nations[kDutch].raw[8] = 0x08; // father 11
    g.promote(0, 80, 24);
    VCR_CHECK(g.unit(0).profession == 0x15);
    const auto notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices[0].section == "VETERAN");
    VCR_CHECK(notices[0].strings == std::vector<std::string>{"Dragoons"});
    // A veteran goes no further before independence.
    g.promote(0, 80, 24);
    VCR_CHECK(g.unit(0).profession == 0x15);
    VCR_CHECK(g.takeNotices().empty());
}

VCR_TEST("combat: servants and criminals move up one rank, experts never") {
    Game g = battle(unit_type::kSoldier, unit_type::kSoldier);
    g.state().nations[kDutch].raw[8] = 0x08;
    g.state().units[0].profession = 0x1A;
    g.promote(0, 80, 24);
    VCR_CHECK(g.unit(0).profession == 0x19);
    g.promote(0, 80, 24);
    VCR_CHECK(g.unit(0).profession == 0x1C);
    g.state().units[0].profession = 0; // an expert farmer
    g.promote(0, 80, 24);
    VCR_CHECK(g.unit(0).profession == 0);
    g.state().units[0].type = unit_type::kColonist;
    g.state().units[0].profession = 0x1C;
    g.promote(0, 80, 24);
    VCR_CHECK(g.unit(0).profession == 0x1C);
}

VCR_TEST("combat: without Washington the opponent's share is the chance to harden") {
    Game g = battle(unit_type::kSoldier, unit_type::kSoldier);
    int hardened = 0;
    for (int i = 0; i < 2000; ++i) {
        g.state().units[0].profession = 0x1C;
        g.promote(0, 75, 25); // 25 in 100
        hardened += g.unit(0).profession == 0x15 ? 1 : 0;
    }
    VCR_CHECK(hardened > 400 && hardened < 600);
    // Against nothing there is nothing to gain.
    g.state().units[0].profession = 0x1C;
    g.promote(0, 80, 0);
    VCR_CHECK(g.unit(0).profession == 0x1C);
}
