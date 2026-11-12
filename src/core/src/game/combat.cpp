// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/game/combat.hpp"

#include "viceroy/game/colony.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game/game.hpp"

#include <algorithm>

namespace viceroy {

int CombatSide::strength() const {
    int percent = 100;
    for (const auto& b : bonuses) {
        percent += b.percent;
    }
    return std::max(0, base * std::max(0, percent));
}

namespace {

constexpr int kVeteranSoldier = 0x15; // the expert soldier's skill
constexpr int kRegulars = 6;
constexpr int kContinentalCavalry = 7;
constexpr int kCavalry = 8;
constexpr int kContinentalArmy = 9;
constexpr int kHeaderOptions = 19;
constexpr std::uint8_t kCombatAnalysisOption = 0x02;
constexpr int kRelations = 52;          // nation record: attitude to each European nation
constexpr std::uint8_t kPeace = 0x40;   // a peace treaty
// Damaged artillery: attack measured in the analysis box, defense from the
// manual's table.
constexpr int kDamagedArtilleryAttack = 5;
constexpr int kDamagedArtilleryDefense = 3;

bool armed(int type) {
    return type == unit_type::kSoldier || type == unit_type::kDragoon || (type >= kRegulars && type <= kContinentalArmy);
}

// Measured: a soldier routed becomes a colonist, a dragoon a soldier. The
// rest follow the same rule (they lose their horses first, then their
// muskets) and are not measured.
int demotedType(int type) {
    switch (type) {
    case unit_type::kDragoon: return unit_type::kSoldier;
    case kCavalry: return kRegulars;
    case kContinentalCavalry: return kContinentalArmy;
    case unit_type::kSoldier:
    case unit_type::kScout:
    case kRegulars:
    case kContinentalArmy: return unit_type::kColonist;
    default: return -1;
    }
}

} // namespace

bool Game::canAttack(int index) const {
    const auto& u = unit(index);
    if (!onMap(index) || isShip(index) || isAboard(index) || u.type < 0 ||
        u.type >= static_cast<int>(rules_.units.size())) {
        return false;
    }
    return rules_.units[static_cast<std::size_t>(u.type)].attack > 0;
}

int Game::defenderAt(int x, int y, int attacker) const {
    int best = -1;
    int bestStrength = -1;
    for (const int other : unitsAt(x, y)) {
        if (unit(other).nation == unit(attacker).nation || isShip(other)) {
            continue;
        }
        const int strength = analyseAttack(attacker, other).defender.strength();
        if (strength > bestStrength) {
            best = other;
            bestStrength = strength;
        }
    }
    return best;
}

bool Game::isDamagedArtillery(int index) const {
    const auto& u = unit(index);
    return u.type == kArtillery && (u.raw[4] & SaveUnit::kDamaged) != 0;
}

bool Game::atPeace(int nation, int other) const {
    if (nation < 0 || nation >= kEuropeanNations || other < 0 || other >= kEuropeanNations) {
        return false;
    }
    return (save_.nations[static_cast<std::size_t>(nation)].raw[static_cast<std::size_t>(kRelations + other)] & kPeace) != 0;
}

void Game::declareWar(int nation, int other) {
    if (nation < 0 || nation >= kEuropeanNations || other < 0 || other >= kEuropeanNations) {
        return;
    }
    // Measured: breaking the treaty clears the bit on both sides.
    save_.nations[static_cast<std::size_t>(nation)].raw[static_cast<std::size_t>(kRelations + other)] &=
        static_cast<std::uint8_t>(~kPeace);
    save_.nations[static_cast<std::size_t>(other)].raw[static_cast<std::size_t>(kRelations + nation)] &=
        static_cast<std::uint8_t>(~kPeace);
}

bool Game::combatAnalysisShown() const {
    return (save_.header[kHeaderOptions] & kCombatAnalysisOption) != 0;
}

CombatAnalysis Game::analyseAttack(int attacker, int defender) const {
    using namespace combat_label;
    CombatAnalysis a;
    const auto& au = unit(attacker);
    const auto& du = unit(defender);
    const auto typeOf = [&](int type) -> const UnitType* {
        return type >= 0 && type < static_cast<int>(rules_.units.size()) ? &rules_.units[static_cast<std::size_t>(type)]
                                                                         : nullptr;
    };
    const bool defenderInColony = colonyAt(du.x, du.y).has_value();

    // Measured on the original's box: the attacker's veteran status, the
    // attack bonus every attacker gets, artillery in the open; the
    // defender's veteran status, its square (forest +50%), fortified, and
    // artillery in the open.
    a.attacker.unit = attacker;
    a.attacker.base = typeOf(au.type) ? typeOf(au.type)->attack : 0;
    if (isDamagedArtillery(attacker)) {
        a.attacker.base = kDamagedArtilleryAttack;
    }
    if (armed(au.type) && au.profession == kVeteranSoldier) {
        a.attacker.bonuses.push_back({kVeteran, 50});
    }
    a.attacker.bonuses.push_back({kAttackBonus, 50});
    // Measured: no penalty against a fortified unit (the manual: artillery
    // is good at attacking fortified units).
    if (au.type == kArtillery && !defenderInColony && du.orders != SaveUnit::kFortified) {
        a.attacker.bonuses.push_back({kArtilleryInOpen, -75});
    }

    a.defender.unit = defender;
    a.defender.base = typeOf(du.type) ? typeOf(du.type)->combat : 0;
    if (isDamagedArtillery(defender)) {
        a.defender.base = kDamagedArtilleryDefense;
    }
    if (armed(du.type) && du.profession == kVeteranSoldier) {
        a.defender.bonuses.push_back({kVeteran, 50});
    }
    if (!defenderInColony) {
        const auto& square = save_.map.at(du.x, du.y);
        const int row = terrainRow(square);
        if (row >= 0 && row < static_cast<int>(rules_.terrain.size())) {
            const int bonus = rules_.terrain[static_cast<std::size_t>(row)].defenseBonus * 25;
            if (bonus > 0) {
                a.defender.bonuses.push_back({kTerrain, bonus, true});
            }
        }
    }
    if (du.orders == SaveUnit::kFortified) {
        a.defender.bonuses.push_back({kFortified, 50});
    }
    if (du.type == kArtillery && !defenderInColony) {
        a.defender.bonuses.push_back({kArtilleryInOpen, -75});
    }
    return a;
}

bool Game::attackerWins(int attack, int defense, int roll) {
    // Not measured: the attacker wins in proportion to the two strengths.
    return roll < attack || defense <= 0;
}

int Game::nextRandom(int range) {
    if (random_ == 0) {
        random_ = static_cast<std::uint32_t>(save_.year * 4 + save_.season + save_.turn * 7919) | 1U;
    }
    random_ = random_ * 1103515245U + 12345U;
    return range <= 0 ? 0 : static_cast<int>((random_ >> 8) % static_cast<std::uint32_t>(range));
}

CombatOutcome Game::attack(int attacker, int defender) {
    const auto analysis = analyseAttack(attacker, defender);
    const int thirds = std::clamp(movesLeft(attacker), 0, 3);
    // Measured text: a tired unit fights at the thirds of a move it has
    // left.
    const int attack = analysis.attacker.strength() * thirds / 3;
    const int defense = analysis.defender.strength();
    const bool won = attackerWins(attack, defense, nextRandom(attack + defense));
    auto& a = save_.units[static_cast<std::size_t>(attacker)];
    a.movesUsed = movementAllowance(attacker);
    if (won) {
        loseBattle(defender, attacker);
    } else {
        loseBattle(attacker, defender);
    }
    refreshUnitFlags();
    return won ? CombatOutcome::AttackerWon : CombatOutcome::DefenderWon;
}

void Game::loseBattle(int loser, int winner) {
    auto& l = save_.units[static_cast<std::size_t>(loser)];
    const auto& w = unit(winner);
    const auto name = [&](int type) {
        return type >= 0 && type < static_cast<int>(rules_.units.size()) ? rules_.units[static_cast<std::size_t>(type)].name
                                                                         : std::string();
    };
    if (const int to = demotedType(l.type); to >= 0) {
        // "French Dragoons routed! Unit demoted to Soldiers status."
        GameNotice notice{"DEMOTE", {"", name(l.type), name(to)}};
        notice.adjectives = {{0, l.nation}};
        notices_.push_back(std::move(notice));
        // Measured: a fortified unit stays fortified.
        l.type = to;
        return;
    }
    if (l.type == kArtillery) {
        // Measured: the first defeat sets the damage flag, and the unit
        // stays where it is; the second destroys it.
        const bool destroyed = isDamagedArtillery(loser);
        GameNotice notice{destroyed ? "ARTILLERY2" : "ARTILLERY", {"", name(l.type)}};
        notice.adjectives = {{0, l.nation}};
        notices_.push_back(std::move(notice));
        if (destroyed) {
            removeUnit(loser);
        } else {
            l.raw[4] = static_cast<std::uint8_t>(l.raw[4] | SaveUnit::kDamaged);
        }
        return;
    }
    // Someone who cannot fight is taken by the winner (measured for
    // colonists: they change sides, stand on the winner's square and wait
    // for orders).
    const bool veteran = l.profession == kVeteranSoldier;
    const char* section = l.type == unit_type::kWagonTrain ? "WAGONCAPTURE"
                          : l.type == unit_type::kTreasure ? "LOOTCAPTURE"
                          : veteran                        ? "COLONISTCAPTURE2"
                                                           : "COLONISTCAPTURE";
    GameNotice notice{section, {"", ""}};
    notice.adjectives = {{0, l.nation}, {1, w.nation}};
    notices_.push_back(std::move(notice));
    l.nation = w.nation;
    l.raw[3] = static_cast<std::uint8_t>((l.raw[3] & 0xF0) | (w.nation & 0x0F));
    l.x = w.x;
    l.y = w.y;
    l.orders = SaveUnit::kNoOrders;
    l.movesUsed = movementAllowance(loser);
    if (veteran) {
        l.profession = kFreeColonist;
    }
}

} // namespace viceroy
