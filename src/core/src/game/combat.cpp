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
constexpr int kIndenturedServant = 0x19;
constexpr int kPettyCriminal = 0x1A;
constexpr int kIndianConvert = 0x1B;
constexpr int kGeorgeWashington = 11;
constexpr int kFathers = 7; // nation record: the founding fathers, one bit each
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
    const bool dugIn = (du.orders == SaveUnit::kFortify || du.orders == SaveUnit::kFortified) &&
                       du.nation < kEuropeanNations;
    if (au.type == kArtillery && !defenderInColony && !dugIn) {
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
    if (du.type == kArtillery && !defenderInColony && !dugIn) {
        a.defender.bonuses.push_back({kArtilleryInOpen, -75});
    }
    return a;
}

CombatStrengths Game::combatStrengths(int attacker, int defender) const {
    // Observed in the original with a debugger: the integer steps below, in
    // this order. Natives, ships and independence are not covered yet.
    const auto& au = unit(attacker);
    const auto& du = unit(defender);
    const auto typeOf = [&](int type) -> const UnitType* {
        return type >= 0 && type < static_cast<int>(rules_.units.size()) ? &rules_.units[static_cast<std::size_t>(type)]
                                                                         : nullptr;
    };
    const auto strength8 = [&](int index, bool attacking) {
        const auto& u = unit(index);
        const UnitType* t = typeOf(u.type);
        int v = t ? (attacking ? t->attack : t->combat) : 0;
        if (isDamagedArtillery(index)) {
            v -= 2;
        }
        v = std::max(0, v) * 8;
        if ((u.type == unit_type::kSoldier || u.type == unit_type::kDragoon) && u.profession == kVeteranSoldier) {
            v += v >> 1;
        }
        return v;
    };
    const bool attackerEuropean = au.nation < kEuropeanNations;
    const bool defenderEuropean = du.nation < kEuropeanNations;
    const bool attackerHuman = au.nation == human();
    const bool defenderHuman = du.nation == human();
    const int difficulty = save_.difficulty;
    const auto colony = colonyAt(du.x, du.y);

    // The defender's bonus, in quarters.
    int m = 0;
    if (colony) {
        m = 2 * (buildingLevel(save_.colonies[static_cast<std::size_t>(*colony)], BuildingKind::Fortification) + 1);
    } else if (attackerEuropean && defenderEuropean) {
        const int row = terrainRow(save_.map.at(du.x, du.y));
        if (row >= 0 && row < static_cast<int>(rules_.terrain.size())) {
            m = rules_.terrain[static_cast<std::size_t>(row)].defenseBonus;
        }
    }
    if (du.orders == SaveUnit::kFortified && !isShip(defender) && m < 5) {
        m += 2;
    }
    int d = strength8(defender, false) * (4 + m) >> 2;
    int a = strength8(attacker, true);
    a = a * 3 >> 1; // the attack bonus
    if (attackerHuman) {
        a += 4 - difficulty;
    }
    if (defenderHuman) {
        d += 4 - difficulty;
    }
    const int thirds = std::clamp(movesLeft(attacker), 0, 3);
    if (thirds < 3) {
        a = a * thirds / 3;
    }
    const UnitType* at = typeOf(au.type);
    const UnitType* dt = typeOf(du.type);
    if (!isShip(attacker) && !isShip(defender) && at && at->attack > 1 && dt && dt->combat < 2) {
        d >>= 1; // someone who cannot fight
    }
    const bool dugIn = (du.orders == SaveUnit::kFortify || du.orders == SaveUnit::kFortified) && defenderEuropean;
    if (!colony && !dugIn) {
        if (au.type == kArtillery) {
            a >>= 2;
        }
        if (du.type == kArtillery) {
            d >>= 2;
        }
    }
    if (difficulty <= 1) {
        constexpr int kEarlyTurns = 80;
        if (defenderHuman && save_.turn < kEarlyTurns && colony) {
            a = difficulty == 0 ? a - (a >> 2) : a >> 1;
        }
        if (defenderHuman && (!attackerEuropean || save_.turn < kEarlyTurns)) {
            a >>= 1;
        }
        if (difficulty == 0 && attackerHuman) {
            a <<= 1;
        }
    }
    return {std::max(0, a), std::max(0, d)};
}

bool Game::attackerWins(int attack, int defense, int roll) {
    (void)defense;
    return roll <= attack;
}

int Game::nextRandom(int range) {
    if (random_ == 0) {
        random_ = static_cast<std::uint32_t>(save_.year * 4 + save_.season + save_.turn * 7919) | 1U;
    }
    random_ = random_ * 1103515245U + 12345U;
    return range <= 0 ? 0 : static_cast<int>((random_ >> 8) % static_cast<std::uint32_t>(range));
}

CombatOutcome Game::attack(int attacker, int defender) {
    const auto s = combatStrengths(attacker, defender);
    const int roll = 1 + nextRandom(s.attack + s.defense);
    const bool won = s.attack + s.defense > 0 && attackerWins(s.attack, s.defense, roll);
    auto& a = save_.units[static_cast<std::size_t>(attacker)];
    a.movesUsed = movementAllowance(attacker);
    if (won) {
        loseBattle(defender, attacker);
        promote(attacker, s.attack, s.defense);
    } else {
        loseBattle(attacker, defender);
        promote(defender, s.defense, s.attack);
    }
    refreshUnitFlags();
    return won ? CombatOutcome::AttackerWon : CombatOutcome::DefenderWon;
}

bool Game::hasFather(int nation, int father) const {
    if (nation < 0 || nation >= kEuropeanNations || father < 0 || father >= 32) {
        return false;
    }
    const auto& raw = save_.nations[static_cast<std::size_t>(nation)].raw;
    return (raw[static_cast<std::size_t>(kFathers + father / 8)] & (1U << (father % 8))) != 0;
}

void Game::promote(int index, int own, int opponent) {
    // Observed in the original with a debugger.
    auto& u = save_.units[static_cast<std::size_t>(index)];
    if (u.type != unit_type::kSoldier && u.type != unit_type::kDragoon) {
        return;
    }
    // Veterans only go further after independence, and experts never.
    const int from = u.profession;
    if (from != kIndenturedServant && from != kPettyCriminal && from != kIndianConvert && from != kFreeColonist) {
        return;
    }
    // The chance is the opponent's share of the fight: an unlikely win
    // hardens more often. The human player's units are a little less
    // lucky on the harder levels, the others a little more.
    int total = own + opponent;
    const bool human = u.nation == this->human();
    total += human ? save_.difficulty : -save_.difficulty;
    if (from == kPettyCriminal) {
        total -= 10;
    } else if (from == kIndenturedServant) {
        total -= 5;
    }
    // With George Washington in congress every win counts.
    if (!hasFather(u.nation, kGeorgeWashington) && 1 + nextRandom(std::max(1, total)) > opponent) {
        return;
    }
    const int to = from == kPettyCriminal       ? kIndenturedServant
                   : from == kIndenturedServant ? kFreeColonist
                   : from == kIndianConvert     ? kIndianConvert
                                                : kVeteranSoldier;
    if (to == from) {
        return;
    }
    u.profession = to;
    if (!human) {
        return;
    }
    const auto typeName = u.type < static_cast<int>(rules_.units.size())
                              ? rules_.units[static_cast<std::size_t>(u.type)].name
                              : std::string();
    if (to == kVeteranSoldier) {
        // "Our Dragoons have hardened to Veteran status, Your Excellency!"
        notices_.push_back({"VETERAN", {typeName}});
        return;
    }
    // "... our Soldiers soldiers have been promoted from Indentured
    // Servants to Free Colonists status."
    const auto expertName = [&](int profession) {
        const int job = professionJob(profession);
        return job >= 0 && job < static_cast<int>(rules_.jobs.size()) ? rules_.jobs[static_cast<std::size_t>(job)].expertName
                                                                       : std::string();
    };
    notices_.push_back({"VALOR", {typeName, expertName(from), expertName(to)}});
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
