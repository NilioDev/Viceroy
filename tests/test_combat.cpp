// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/game/colony.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game/game.hpp"

#include <algorithm>
#include <functional>

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

namespace {
// The French colony at (1, 1) with `population` colonists (expert ore
// miners) and nobody on its square; another French colony far away makes
// up the rest of their 16 colonists. France has 1000 gold.
Game colonyBattle(int population, int muskets = 0, int colonistCombat = -1) {
    Game g = battle(unit_type::kSoldier, unit_type::kColonist, colonistCombat);
    auto& s = g.state();
    s.units.pop_back();
    s.units[0].profession = 0x15;
    s.turn = 120;
    SaveColony c;
    c.x = 1;
    c.y = 1;
    c.name = "Martinique";
    c.nation = kFrench;
    c.population = population;
    for (int i = 0; i < population; ++i) {
        c.skills[static_cast<std::size_t>(i)] = 6;
    }
    c.stock[15] = muskets;
    c.rebelDividend = 40;
    s.colonies.push_back(c);
    SaveColony other = c;
    other.x = 2;
    other.y = 2;
    other.population = 16 - population;
    s.colonies.push_back(other);
    s.map.at(1, 1).flags |= MapSquare::kHasColony;
    s.nations[kFrench].gold = 1000;
    s.nations[kDutch].gold = 100;
    return g;
}
} // namespace

VCR_TEST("combat: a colony with nobody on its square is defended by a colonist (measured)") {
    using namespace combat_label;
    Game g = colonyBattle(1);
    VCR_CHECK(g.colonyUndefended(0));
    const auto a = g.analyseColonyAttack(0, 0, 0);
    VCR_CHECK(a.defender.unit == -1 && a.defender.colony == 0 && !a.defender.armed);
    VCR_CHECK(a.defender.base == 1);
    VCR_CHECK(a.defender.bonuses.size() == 1);
    VCR_CHECK(a.defender.bonuses[0].label == kColony && a.defender.bonuses[0].percent == 50);
    VCR_CHECK(a.defender.bonuses[0].colonyPicture);
    // 8 x 6/4 = 12, halved for a colonist: 6 against the veteran's 80.
    const auto s = g.colonyStrengths(0, 0, 0);
    VCR_CHECK(s.attack == 80);
    VCR_CHECK(s.defense == 6);
}

VCR_TEST("combat: with Paul Revere 50 muskets arm the colony's defender with +1 (measured)") {
    Game g = colonyBattle(1, 50);
    VCR_CHECK(!g.analyseColonyAttack(0, 0, 0).defender.armed); // no Revere
    g.state().nations[kFrench].raw[8] |= 1U << 4;               // Paul Revere
    g.state().colonies[0].stock[15] = 49;
    VCR_CHECK(!g.analyseColonyAttack(0, 0, 0).defender.armed);
    g.state().colonies[0].stock[15] = 50;
    const auto a = g.analyseColonyAttack(0, 0, 0);
    VCR_CHECK(a.defender.armed);
    VCR_CHECK(a.defender.bonuses.size() == 2 && a.defender.bonuses[0].points == 1);
    VCR_CHECK(g.colonyStrengths(0, 0, 0).defense == 12); // 16 x 6/4, halved
}

VCR_TEST("combat: taking a colony plunders its share of the owner's gold (measured)") {
    // A defender of no strength always loses.
    Game g = colonyBattle(1, 0, 0);
    VCR_CHECK(g.attackColony(0, 0, 0) == CombatOutcome::AttackerWon);
    const auto& c = g.state().colonies[0];
    VCR_CHECK(c.nation == kDutch);
    VCR_CHECK(c.rebelDividend == 0);
    VCR_CHECK(c.population == 1);
    VCR_CHECK(g.unit(0).x == 1 && g.unit(0).y == 1);
    VCR_CHECK(g.movesLeft(0) == 0);
    VCR_CHECK(g.state().nations[kFrench].gold == 938); // 1000 x 1/16 = 62
    VCR_CHECK(g.state().nations[kDutch].gold == 162);
    const auto notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices[0].section == "CAPTURED");
    VCR_CHECK(notices[0].strings.at(2) == "Martinique");
    VCR_CHECK(notices[0].numbers == std::vector<int>{62});
    VCR_CHECK(notices[0].adjectives == (std::vector<std::pair<int, int>>{{0, kDutch}}));
}

VCR_TEST("combat: Quebec's share of 16 colonists is 11 (measured: 687 of 1000)") {
    Game g = colonyBattle(11, 0, 0);
    VCR_CHECK(g.attackColony(0, 0, 3) == CombatOutcome::AttackerWon);
    VCR_CHECK(g.state().nations[kFrench].gold == 313);
}

VCR_TEST("combat: units in a colony get its bonus instead of the terrain's") {
    using namespace combat_label;
    Game g = colonyBattle(1);
    SaveUnit d = g.state().units[0];
    d.nation = kFrench;
    d.x = 1;
    d.y = 1;
    d.profession = 0x1C;
    d.orders = SaveUnit::kFortified;
    g.state().units.push_back(d);
    VCR_CHECK(!g.colonyUndefended(0));
    const auto a = g.analyseAttack(0, 1);
    VCR_CHECK(labels(a.defender) == std::vector<int>{kColony, kFortified});
}

namespace {
constexpr int kMerchantman = 14;
constexpr int kGalleon = 15;
constexpr int kPrivateer = 16;
constexpr int kFrigate = 17;

// A 3 × 3 sea with a Dutch frigate at (1, 0) and a French merchantman at
// (1, 1); the corner (2, 2) is land. `turn` changes the dice.
Game seaBattle(int turn, int attackerType = kFrigate, int defenderType = kMerchantman) {
    SaveGame s;
    s.year = 1600;
    s.turn = turn;
    s.humanNation = kDutch;
    s.map.width = 3;
    s.map.height = 3;
    s.map.squares.resize(9);
    for (auto& sq : s.map.squares) {
        sq.terrain = 25;
    }
    s.map.at(2, 2).terrain = 0;
    SaveUnit a;
    a.x = 1;
    a.y = 0;
    a.type = attackerType;
    a.nation = kDutch;
    SaveUnit d = a;
    d.y = 1;
    d.type = defenderType;
    d.nation = kFrench;
    s.units = {a, d};
    Ruleset rules;
    rules.units.resize(19);
    const auto set = [&](int row, const char* name, int movement, int attack, int combat, int guns, int hull) {
        auto& u = rules.units[static_cast<std::size_t>(row)];
        u.name = name;
        u.movement = movement;
        u.attack = attack;
        u.combat = combat;
        u.cargoSlots = 4;
        u.guns = guns;
        u.hull = hull;
    };
    set(unit_type::kColonist, "Colonists", 1, 0, 1, 0, 0);
    set(kMerchantman, "Merchantman", 5, 0, 6, 0, 6);
    set(kGalleon, "Galleon", 6, 0, 10, 0, 10);
    set(kPrivateer, "Privateer", 8, 8, 8, 8, 8);
    set(kFrigate, "Frigate", 6, 16, 16, 16, 16);
    rules.terrain.resize(27);
    return Game(std::move(s), rules);
}

// Fights in fresh games until the outcome comes up.
Game seaBattleUntil(CombatOutcome wanted, const std::function<void(Game&)>& setUp = {}) {
    for (int turn = 0; turn < 2000; ++turn) {
        Game g = seaBattle(turn);
        if (setUp) {
            setUp(g);
        }
        if (g.attack(0, 1) == wanted) {
            return g;
        }
    }
    VCR_CHECK(false);
    return seaBattle(0);
}
} // namespace

VCR_TEST("combat: how well a ship gets away (read from the original)") {
    Game g = seaBattle(0);
    VCR_CHECK(g.evasion(0) == 21); // frigate: 6 moves in thirds, plus 3
    VCR_CHECK(g.evasion(1) == 18);
    g.state().units[1].holds = 2;
    VCR_CHECK(g.evasion(1) == 10); // 4 less for each hold in use
    g.state().units[1].holds = 5;
    VCR_CHECK(g.evasion(1) == 1);
    g.state().units[1].holds = 0;
    g.state().units[1].type = kGalleon;
    VCR_CHECK(g.evasion(1) == 24);
    g.state().units[1].type = kPrivateer;
    VCR_CHECK(g.evasion(1) == 54);
}

VCR_TEST("combat: a merchantman may evade a frigate") {
    Game g = seaBattleUntil(CombatOutcome::Evaded);
    VCR_CHECK(g.unitCount() == 2);
    VCR_CHECK(!g.isDamagedShip(0) && !g.isDamagedShip(1));
    VCR_CHECK(g.movesLeft(0) == 0);
    const auto notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices[0].section == "EVASIVE");
}

VCR_TEST("combat: a nation's last ship of a kind is damaged, not sunk") {
    for (int i = 0; i < 10; ++i) {
        Game g = seaBattleUntil(CombatOutcome::AttackerWon, [i](Game& game) { game.state().turn += 2000 * (i + 1); });
        VCR_CHECK(g.unitCount() == 2);
        VCR_CHECK(g.isDamagedShip(1));
    }
}

VCR_TEST("combat: a sunk ship takes its passengers down") {
    bool sank = false;
    for (int i = 0; i < 20 && !sank; ++i) {
        Game g = seaBattleUntil(CombatOutcome::AttackerWon, [i](Game& game) {
            auto& s = game.state();
            s.turn += 2000 * (i + 1);
            SaveUnit other = s.units[1];
            other.x = 0;
            other.y = 2;
            SaveUnit passenger = s.units[1];
            passenger.type = unit_type::kColonist;
            s.units.push_back(other);
            s.units.push_back(passenger);
        });
        if (g.unitCount() == 2) {
            sank = true;
            const auto notices = g.takeNotices();
            VCR_CHECK(notices.size() == 1 && notices[0].section == "SHIPSUNK");
            VCR_CHECK(g.unit(1).x == 0 && g.unit(1).y == 2);
        } else {
            VCR_CHECK(g.unitCount() == 3); // damaged: the passenger is lost
            VCR_CHECK(g.isDamagedShip(1));
        }
    }
    VCR_CHECK(sank);
}

VCR_TEST("combat: a frigate beaten by a merchantman is repaired at the drydock (measured)") {
    Game g = seaBattleUntil(CombatOutcome::DefenderWon, [](Game& game) {
        auto& s = game.state();
        SaveColony c;
        c.x = 2;
        c.y = 2;
        c.name = "Jamestown";
        c.nation = kDutch;
        setBuildingLevel(c, BuildingKind::Docks, 2);
        s.colonies.push_back(c);
        s.map.at(2, 2).flags |= MapSquare::kHasColony;
        s.units[0].holds = 3;
    });
    VCR_CHECK(g.isDamagedShip(0));
    VCR_CHECK(g.unit(0).x == 2 && g.unit(0).y == 2);
    VCR_CHECK(g.unit(0).holds == 0);
    VCR_CHECK(g.unit(0).workTurns == 10); // 16 - 6
    auto notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices[0].section == "SHIPDAMAGE");
    VCR_CHECK(notices[0].strings.at(2) == "Jamestown");
    VCR_CHECK(g.move(0, {1, 0}) == MoveResult::Blocked);
    VCR_CHECK(!g.needsOrders(0));
    VCR_CHECK(!g.canAttack(0));
    g.endTurn();
    g.endTurn();
    VCR_CHECK(g.isDamagedShip(0));
    g.endTurn();
    VCR_CHECK(!g.isDamagedShip(0));
    notices = g.takeNotices();
    VCR_CHECK(std::any_of(notices.begin(), notices.end(), [](const GameNotice& n) {
        return n.section == "REFIT" && n.strings.at(1) == "Jamestown";
    }));
}

VCR_TEST("combat: without a drydock a damaged ship goes back to Europe") {
    Game g = seaBattleUntil(CombatOutcome::DefenderWon);
    VCR_CHECK(g.isDamagedShip(0));
    VCR_CHECK(!g.onMap(0));
    VCR_CHECK(europePlace(g.unit(0)) == EuropePlace::InPort);
    VCR_CHECK(!g.sailToNewWorld(0));
    const auto notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices[0].section == "SHIPDAMAGE" && notices[0].value == kDutch);
}
