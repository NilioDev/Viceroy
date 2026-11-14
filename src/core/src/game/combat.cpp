// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/game/combat.hpp"

#include "viceroy/game/colony.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/sounds.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>

namespace viceroy {

int CombatSide::strength() const {
    int percent = 100;
    int points = base;
    for (const auto& b : bonuses) {
        percent += b.percent;
        points += b.points;
    }
    return std::max(0, points * std::max(0, percent));
}

namespace {

constexpr int kVeteranSoldier = 0x15; // the expert soldier's skill
constexpr int kIndenturedServant = 0x19;
constexpr int kPettyCriminal = 0x1A;
constexpr int kIndianConvert = 0x1B;
constexpr int kGeorgeWashington = 11;
constexpr int kDrake = 13;
constexpr int kPrivateer = 16;
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
// rest, read from the original's list: cavalry become regulars,
// continental cavalry continental army, which becomes colonists. Scouts
// and regulars are not on it: they die.
int demotedType(int type) {
    switch (type) {
    case unit_type::kDragoon: return unit_type::kSoldier;
    case kCavalry: return kRegulars;
    case kContinentalCavalry: return kContinentalArmy;
    case unit_type::kSoldier:
    case kContinentalArmy: return unit_type::kColonist;
    default: return -1;
    }
}

// Only colonists, wagon trains and treasure are taken; anyone else who
// loses and is not demoted dies.
bool capturable(int type) {
    return type == unit_type::kColonist || type == unit_type::kWagonTrain || type == unit_type::kTreasure;
}

} // namespace

bool Game::canAttack(int index) const {
    const auto& u = unit(index);
    if (!onMap(index) || isAboard(index) || isDamagedShip(index) || u.type < 0 ||
        u.type >= static_cast<int>(rules_.units.size())) {
        return false;
    }
    return rules_.units[static_cast<std::size_t>(u.type)].attack > 0;
}

int Game::defenderAt(int x, int y, int attacker) const {
    int best = -1;
    int bestStrength = -1;
    for (const int other : unitsAt(x, y)) {
        // Ships fight ships, land units land units.
        if (unit(other).nation == unit(attacker).nation || isShip(other) != isShip(attacker)) {
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

int Game::addColonyBonus(CombatSide& side, int colony) const {
    // Seen in the box: "Colony +50%" with the colony's picture, or the
    // stockade's name ("Fort +150%") with its picture.
    const auto& c = save_.colonies[static_cast<std::size_t>(colony)];
    const int level = buildingLevel(c, BuildingKind::Fortification);
    const int quarters = 2 * (level + 1);
    CombatBonus bonus{combat_label::kColony, quarters * 25};
    bonus.colonyPicture = true;
    if (level > 0 && level - 1 < static_cast<int>(rules_.buildings.size())) {
        bonus.text = rules_.buildings[static_cast<std::size_t>(level - 1)].name;
    }
    side.bonuses.push_back(bonus);
    return quarters;
}

bool Game::rebelAmbush(const SaveUnit& attacker, const SaveUnit& defender) const {
    // Read from the original: during the war a human player attacking a
    // European in the open takes the square's bonus as an ambush, and the
    // defender gets none.
    const int a = attacker.nation;
    return (save_.header[18] & 0x01) != 0 && a >= 0 && a < kEuropeanNations &&
           save_.players[static_cast<std::size_t>(a)].control == 0 && defender.nation < kEuropeanNations &&
           !colonyAt(defender.x, defender.y) && !settlementAt(defender.x, defender.y);
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
    if (au.type == kPrivateer && hasFather(au.nation, kDrake)) {
        a.attacker.bonuses.push_back({kDrakeBonus, 50});
    }
    const bool ambush = rebelAmbush(au, du) && save_.map.inside(du.x, du.y);
    if (ambush) {
        const int row = terrainRow(save_.map.at(du.x, du.y));
        if (row >= 0 && row < static_cast<int>(rules_.terrain.size())) {
            const int bonus = rules_.terrain[static_cast<std::size_t>(row)].defenseBonus * 25;
            if (bonus > 0) {
                a.attacker.bonuses.push_back({kAmbush, bonus, true});
            }
        }
    }
    a.attacker.bonuses.push_back({kAttackBonus, 50});
    // Measured: no penalty against a fortified unit (the manual: artillery
    // is good at attacking fortified units).
    const bool dugIn = (du.orders == SaveUnit::kFortify || du.orders == SaveUnit::kFortified) &&
                       du.nation < kEuropeanNations;
    if (au.type == kArtillery && !defenderInColony && !dugIn) {
        a.attacker.bonuses.push_back({kArtilleryInOpen, -75});
    }
    // The war of independence (labels after the order of LABELS.TXT, not
    // measured on the box).
    if ((save_.header[18] & 0x01) != 0 && au.nation < kEuropeanNations) {
        const bool crown = au.nation == crownNation();
        if (defenderInColony) {
            if (crown || (save_.header[18] & 0x02) != 0) {
                a.attacker.bonuses.push_back({kBombard, 50});
            }
            const auto& c = save_.colonies[static_cast<std::size_t>(*colonyAt(du.x, du.y))];
            const int sons = sonsOfLibertyPercent(c, nationEffects(save_, c.nation).libertyExtra(true));
            const int share = crown ? 100 - sons : sons;
            if (share > 0) {
                a.attacker.bonuses.push_back({crown ? kTories : kRebels, share});
            }
        } else if (crown && !save_.map.at(du.x, du.y).isWater() && save_.difficulty > 0) {
            a.attacker.bonuses.push_back({kExpeditionaryForce, 5 * save_.difficulty});
        }
    }

    a.defender.unit = defender;
    a.defender.base = typeOf(du.type) ? typeOf(du.type)->combat : 0;
    if (isDamagedArtillery(defender)) {
        a.defender.base = kDamagedArtilleryDefense;
    }
    if (armed(du.type) && du.profession == kVeteranSoldier) {
        a.defender.bonuses.push_back({kVeteran, 50});
    }
    if (du.type == kPrivateer && hasFather(du.nation, kDrake)) {
        a.defender.bonuses.push_back({kDrakeBonus, 50});
    }
    if (!defenderInColony && !ambush) {
        const auto& square = save_.map.at(du.x, du.y);
        const int row = terrainRow(square);
        if (row >= 0 && row < static_cast<int>(rules_.terrain.size())) {
            const int bonus = rules_.terrain[static_cast<std::size_t>(row)].defenseBonus * 25;
            if (bonus > 0) {
                a.defender.bonuses.push_back({kTerrain, bonus, true});
            }
        }
    }
    int quarters = 0;
    if (defenderInColony) {
        quarters = addColonyBonus(a.defender, *colonyAt(du.x, du.y));
    }
    // Fortified adds +50% only where the colony gives less than +125%.
    if (du.orders == SaveUnit::kFortified && quarters < 5) {
        a.defender.bonuses.push_back({kFortified, 50});
    }
    if (du.type == kArtillery && !defenderInColony && !dugIn) {
        a.defender.bonuses.push_back({kArtilleryInOpen, -75});
    }
    return a;
}

CombatStrengths Game::combatStrengths(int attacker, int defender, bool beforeLevels) const {
    return strengthsAgainst(attacker, unit(defender), isShip(defender), isDamagedArtillery(defender), 0, beforeLevels);
}

int Game::defenceStrength(int attacker, const SaveUnit& du, bool defenderShip, bool defenderDamaged,
                          int defenderExtra) const {
    // The defender's strength before the steps that depend on who attacks
    // (the human's bonus and the rest): eight per point of combat, with the
    // bonus of its colony, settlement or square in quarters.
    const auto& au = unit(attacker);
    const bool attackerEuropean = au.nation < kEuropeanNations;
    const bool defenderEuropean = du.nation < kEuropeanNations;
    const auto colony = colonyAt(du.x, du.y);
    const auto settlement = colony ? std::nullopt : settlementAt(du.x, du.y);
    int v = 0;
    if (du.type >= 0 && du.type < static_cast<int>(rules_.units.size())) {
        v = rules_.units[static_cast<std::size_t>(du.type)].combat;
    }
    v += defenderExtra;
    if (defenderDamaged) {
        v -= 2;
    }
    v = std::max(0, v) * 8;
    if ((du.type == unit_type::kSoldier || du.type == unit_type::kDragoon) && du.profession == kVeteranSoldier) {
        v += v >> 1;
    }
    if (du.type == kPrivateer && hasFather(du.nation, kDrake)) {
        v += v >> 1;
    }
    // The defender's bonus, in quarters.
    int m = 0;
    if (colony) {
        m = 2 * (buildingLevel(save_.colonies[static_cast<std::size_t>(*colony)], BuildingKind::Fortification) + 1);
    } else if (settlement) {
        m = settlementQuarters(*settlement);
    } else if ((!defenderEuropean || attackerEuropean) && !rebelAmbush(au, du)) {
        const int row = terrainRow(save_.map.at(du.x, du.y));
        if (row >= 0 && row < static_cast<int>(rules_.terrain.size())) {
            m = rules_.terrain[static_cast<std::size_t>(row)].defenseBonus;
        }
    }
    if (du.orders == SaveUnit::kFortified && !defenderShip && m < 5) {
        m += 2;
    }
    return v * (4 + m) >> 2;
}

CombatStrengths Game::strengthsAgainst(int attacker, const SaveUnit& du, bool defenderShip, bool defenderDamaged,
                                       int defenderExtra, bool beforeLevels) const {
    // Observed in the original with a debugger: the integer steps below, in
    // this order. Natives, ships and independence are not covered yet.
    const auto& au = unit(attacker);
    const auto typeOf = [&](int type) -> const UnitType* {
        return type >= 0 && type < static_cast<int>(rules_.units.size()) ? &rules_.units[static_cast<std::size_t>(type)]
                                                                         : nullptr;
    };
    const auto strength8 = [&](const SaveUnit& u, bool attacking, bool damaged, int extra) {
        const UnitType* t = typeOf(u.type);
        int v = (t ? (attacking ? t->attack : t->combat) : 0) + extra;
        if (damaged) {
            v -= 2;
        }
        v = std::max(0, v) * 8;
        if ((u.type == unit_type::kSoldier || u.type == unit_type::kDragoon) && u.profession == kVeteranSoldier) {
            v += v >> 1;
        }
        // Read from the original: Francis Drake's privateers, attacking or
        // not.
        if (u.type == kPrivateer && hasFather(u.nation, kDrake)) {
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
    const auto settlement = colony ? std::nullopt : settlementAt(du.x, du.y);

    int d = defenceStrength(attacker, du, defenderShip, defenderDamaged, defenderExtra);
    int a = strength8(au, true, isDamagedArtillery(attacker), 0);
    // The ambush, read from the original: a tribe attacking a European in
    // the open who is not fortified has the square's defence bonus (in
    // quarters) on its own side; the defender then gets none.
    // During the war the rebels ambush the Crown in the open the same way,
    // fortified or not.
    if (((!attackerEuropean && defenderEuropean && du.orders != SaveUnit::kFortified) || rebelAmbush(au, du)) &&
        !colony && !settlement && save_.map.inside(du.x, du.y)) {
        const int row = terrainRow(save_.map.at(du.x, du.y));
        if (row >= 0 && row < static_cast<int>(rules_.terrain.size())) {
            a = a * (4 + rules_.terrain[static_cast<std::size_t>(row)].defenseBonus) >> 2;
        }
    }
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
    if (!isShip(attacker) && !defenderShip && at && at->attack > 1 && dt && dt->combat < 2) {
        d >>= 1; // someone who cannot fight
    }
    const bool dugIn = (du.orders == SaveUnit::kFortify || du.orders == SaveUnit::kFortified) && defenderEuropean;
    if (!colony && !settlement && !dugIn) {
        if (au.type == kArtillery) {
            a >>= 2;
        }
        if (du.type == kArtillery) {
            d >>= 2;
        }
    }
    // Spain's bonus against the natives in their villages.
    constexpr int kSpain = 2;
    if (settlement && au.nation == kSpain && !defenderEuropean) {
        a += a >> 1;
    }
    // Read from the original: the war of independence. On a colony the
    // Crown attacks with half again and the colony's Tories in %, the
    // rebels with its Sons of Liberty in % (and half again once the foreign
    // power has joined); in the open the Crown adds 5% a level of
    // difficulty on land.
    const bool war = (save_.header[18] & 0x01) != 0;
    if (war && attackerEuropean) {
        const bool crown = au.nation == crownNation();
        if (colony) {
            if (crown || (save_.header[18] & 0x02) != 0) {
                a += a >> 1;
            }
            const auto& c = save_.colonies[static_cast<std::size_t>(*colony)];
            const int sons = sonsOfLibertyPercent(c, nationEffects(save_, c.nation).libertyExtra(true));
            a += a * (crown ? 100 - sons : sons) / 100;
        } else if (crown && save_.map.inside(du.x, du.y) && !save_.map.at(du.x, du.y).isWater()) {
            a += a * difficulty / 20;
        }
    }
    if (beforeLevels) {
        // Where the original stops when a computer player only weighs an
        // attack: the steps below are left out of its odds.
        return {std::max(0, a), std::max(0, d)};
    }
    // During the war the easy levels' help to a colony under attack is
    // gone, unless a ship attacks.
    if (difficulty <= 1 && !(war && colony && !isShip(attacker))) {
        constexpr int kEarlyTurns = 80;
        if (defenderHuman && save_.turn < kEarlyTurns && colony) {
            a = difficulty == 0 ? a - (a >> 2) : a >> 1;
        }
        if (defenderHuman && (!attackerEuropean || save_.turn < kEarlyTurns)) {
            a >>= 1;
        }
    }
    if (difficulty == 0 && attackerHuman) {
        a <<= 1;
    }
    if (colony && defenderEuropean) {
        // Read from the original: a tribe never takes a nation's last
        // colony, and the player's colonies that hold at least half of the
        // player's colonists defend with 4 more per easier level.
        int colonies = 0;
        int people = 0;
        for (const auto& c : save_.colonies) {
            if (c.nation == du.nation) {
                ++colonies;
                people += c.population;
            }
        }
        if (!attackerEuropean && colonies == 1) {
            a = 0;
        }
        if (defenderHuman && (people >> 1) <= save_.colonies[static_cast<std::size_t>(*colony)].population) {
            d += (4 - difficulty) * 4;
        }
    }
    return {std::max(0, a), std::max(0, d)};
}

bool Game::colonyUndefended(int colony) const {
    if (colony < 0 || colony >= static_cast<int>(save_.colonies.size())) {
        return false;
    }
    const auto& c = save_.colonies[static_cast<std::size_t>(colony)];
    return c.population > 0 && unitsAt(c.x, c.y).empty();
}

int Game::pickColonyDefender(int colony) {
    // Seen: a different colonist of Quebec stood up in each try.
    const auto& c = save_.colonies[static_cast<std::size_t>(colony)];
    return nextRandom(std::max(1, c.population));
}

SaveUnit Game::colonistAsUnit(int colony, int colonist) const {
    const auto& c = save_.colonies[static_cast<std::size_t>(colony)];
    SaveUnit u;
    u.x = c.x;
    u.y = c.y;
    u.type = unit_type::kColonist;
    u.nation = c.nation;
    u.profession = c.skills[static_cast<std::size_t>(std::clamp(colonist, 0, kMaxColonists - 1))];
    u.orders = SaveUnit::kNoOrders;
    return u;
}

namespace {
constexpr int kMuskets = 15;      // row of @CARGO
constexpr int kArmingMuskets = 50; // measured: 49 give nothing, 50 give +1
constexpr int kRevere = 12;        // row of @FATHERS
} // namespace

bool Game::colonistTakesUpArms(int colony) const {
    // Read from the original: only with Paul Revere does the colonist take
    // up the colony's muskets.
    const auto& c = save_.colonies[static_cast<std::size_t>(colony)];
    return c.stock[kMuskets] >= kArmingMuskets && hasFather(c.nation, kRevere);
}

CombatAnalysis Game::analyseColonyAttack(int attacker, int colony, int colonist) const {
    // The attacker's side is the same as against a unit.
    CombatAnalysis a;
    const auto& au = unit(attacker);
    const UnitType* at = au.type >= 0 && au.type < static_cast<int>(rules_.units.size())
                             ? &rules_.units[static_cast<std::size_t>(au.type)]
                             : nullptr;
    a.attacker.unit = attacker;
    a.attacker.base = isDamagedArtillery(attacker) ? kDamagedArtilleryAttack : at ? at->attack : 0;
    if (armed(au.type) && au.profession == kVeteranSoldier) {
        a.attacker.bonuses.push_back({combat_label::kVeteran, 50});
    }
    a.attacker.bonuses.push_back({combat_label::kAttackBonus, 50});

    // Seen: the colonist by their skill ("Ore Miner 1"), "Muskets +1" when
    // the colony has 50 and its nation Paul Revere, then the colony.
    a.defender.colony = colony;
    a.defender.colonist = colonist;
    a.defender.base = rules_.units.size() > unit_type::kColonist ? rules_.units[unit_type::kColonist].combat : 1;
    a.defender.armed = colonistTakesUpArms(colony);
    if (a.defender.armed) {
        CombatBonus muskets{0, 0};
        muskets.points = 1;
        muskets.text = kMuskets < static_cast<int>(rules_.cargo.size()) ? rules_.cargo[kMuskets].name : "Muskets";
        a.defender.bonuses.push_back(muskets);
    }
    addColonyBonus(a.defender, colony);
    return a;
}

CombatStrengths Game::colonyStrengths(int attacker, int colony, int colonist, bool beforeLevels) const {
    const int extra = colonistTakesUpArms(colony) ? 1 : 0;
    return strengthsAgainst(attacker, colonistAsUnit(colony, colonist), false, false, extra, beforeLevels);
}

CombatOutcome Game::attackColony(int attacker, int colony, int colonist) {
    const auto s = colonyStrengths(attacker, colony, colonist);
    const int roll = 1 + nextRandom(s.attack + s.defense);
    const bool won = s.attack + s.defense > 0 && attackerWins(s.attack, s.defense, roll);
    auto& a = save_.units[static_cast<std::size_t>(attacker)];
    a.movesUsed = movementAllowance(attacker);
    const int owner = save_.colonies[static_cast<std::size_t>(colony)].nation;
    fightStartSound(a.nation, a.type, owner, colonistAsUnit(colony, colonist).type);
    if (won) {
        if (a.nation == human() || owner == human()) {
            sound(sounds::kColonyTaken);
        }
        captureColony(colony, attacker);
        promote(attacker, s.attack, s.defense);
    } else {
        loseBattle(attacker, -1);
    }
    refreshUnitFlags();
    return won ? CombatOutcome::AttackerWon : CombatOutcome::DefenderWon;
}

void Game::captureColony(int colony, int attacker) {
    // Measured on Martinique and Quebec: the attacker marches in, the
    // colony changes hands with its people, buildings and goods, its
    // liberty bells start again, and the owner's gold goes with it in
    // proportion to the colony's share of the owner's colonists (1000 gold,
    // 1 of 16 colonists: 62; 11 of 16: 687).
    auto& c = save_.colonies[static_cast<std::size_t>(colony)];
    const int loser = c.nation;
    auto& a = save_.units[static_cast<std::size_t>(attacker)];
    int people = 0;
    for (const auto& other : save_.colonies) {
        if (other.nation == loser) {
            people += other.population;
        }
    }
    int plunder = 0;
    if (loser >= 0 && loser < kEuropeanNations && people > 0) {
        auto& from = save_.nations[static_cast<std::size_t>(loser)];
        plunder = static_cast<int>(static_cast<long long>(std::max(0, from.gold)) * c.population / people);
        from.gold -= plunder;
        if (a.nation >= 0 && a.nation < kEuropeanNations) {
            save_.nations[static_cast<std::size_t>(a.nation)].gold += plunder;
        }
    }
    c.nation = a.nation;
    c.rebelDividend = 0;
    a.x = c.x;
    a.y = c.y;
    a.orders = SaveUnit::kNoOrders;
    reveal(c.x, c.y, 2, a.nation);
    coloniesChangeHands(colony);
    // "Dutch march into Martinique! 62 plundered!"
    GameNotice notice{"CAPTURED", {"", "", c.name}};
    notice.peoples = {{0, a.nation}};
    notice.numbers = {plunder};
    notices_.push_back(std::move(notice));
}

bool Game::isDamagedShip(int index) const {
    return isShip(index) && (unit(index).raw[4] & SaveUnit::kDamaged) != 0;
}

namespace {
constexpr int kGalleon = 15;
constexpr int kFrigate = 17;
constexpr int kManOWar = 18;
} // namespace

int Game::evasion(int ship) const {
    // Read from the original: the ship's moves in thirds plus a move, twice
    // that for a privateer, a move more for a galleon, and a move and a
    // third less for each hold in use; at least 1. Checked: frigate 21,
    // merchantman 18, galleon 24, privateer 54.
    int v = movementAllowance(ship) + 3;
    const int type = unit(ship).type;
    if (type == kPrivateer) {
        v *= 2;
    }
    if (type == kGalleon) {
        v += 3;
    }
    v -= 4 * unit(ship).holds;
    return std::max(1, v);
}

void Game::loseShip(int loser, int winner) {
    // Read from the original and checked in six fights: the winner's guns
    // against the loser's hull decide between damaged and sunk, but a
    // nation's last ship of a kind is only damaged.
    // Without a winner (a raid on the harbour) the original takes a
    // privateer's guns.
    const auto& lt = rules_.units[static_cast<std::size_t>(unit(loser).type)];
    const auto& wt = rules_.units[static_cast<std::size_t>(winner >= 0 ? unit(winner).type : kPrivateer)];
    const int nation = unit(loser).nation;
    const int winnerNation = winner >= 0 ? unit(winner).nation : -1;
    const bool winnerShip = winner < 0 || isShip(winner);
    bool damaged = true;
    if (wt.attack > 0 && wt.guns > 0) {
        damaged = 1 + nextRandom(wt.guns + lt.hull) <= lt.hull;
        const bool war = (save_.header[18] & 0x01) != 0;
        if (!damaged && !war) {
            int same = 0;
            for (const auto& u : save_.units) {
                same += u.nation == nation && u.type == unit(loser).type ? 1 : 0;
            }
            damaged = same < 2;
        } else if (!damaged && nation == crownNation() && unit(loser).type == kManOWar) {
            // Read from the original: during the war only the Crown's last
            // man-of-war is spared.
            int same = 0;
            for (const auto& u : save_.units) {
                same += u.nation == nation && u.type == kManOWar ? 1 : 0;
            }
            damaged = same < 2;
        }
    }
    // Where a damaged ship goes: the nearest of its nation's colonies with
    // a drydock, or Europe.
    const auto drydock = [&] {
        int best = -1;
        int bestDistance = 0;
        const auto& l = unit(loser);
        for (std::size_t c = 0; c < save_.colonies.size(); ++c) {
            const auto& col = save_.colonies[c];
            if (col.nation != nation || buildingLevel(col, BuildingKind::Docks) < 2) {
                continue;
            }
            const int distance = std::max(std::abs(col.x - l.x), std::abs(col.y - l.y));
            if (best < 0 || distance < bestDistance) {
                best = static_cast<int>(c);
                bestDistance = distance;
            }
        }
        return best;
    }();
    if (damaged && drydock < 0 && nation == human() && (save_.header[18] & 0x01) != 0) {
        damaged = false; // read from the original: during the war Europe is closed to the rebels' ships
    }
    const auto aboard = passengers(loser);
    if (!damaged) {
        // "Dutch Frigate sunk by English Privateer!"
        GameNotice notice{"SHIPSUNK", {"", lt.name, "", wt.name}};
        notice.adjectives = {{0, nation}, {2, winnerNation}};
        if (nation == human() || winnerNation == human()) {
            notice.music.push_back({MusicCue::Kind::Sound, sounds::kShipSunk});
        }
        // Read from the original: a sad tune for the loser, a military one
        // for the winner.
        if (nation == human()) {
            notice.music.push_back({MusicCue::Kind::MoodEvent, 1});
        }
        if (winnerNation == human()) {
            notice.music.push_back({MusicCue::Kind::MoodEvent, 4});
        }
        if (winner >= 0 || nation == human()) {
            notices_.push_back(std::move(notice));
        }
        std::vector<int> gone = aboard;
        gone.push_back(loser);
        std::sort(gone.begin(), gone.end(), std::greater<>());
        for (const int g : gone) {
            removeUnit(g);
        }
        return;
    }
    // Damaged: cargo and passengers are lost and the ship goes at once to
    // the nearest of its nation's colonies with a drydock, or to Europe.
    std::vector<int> gone = aboard;
    std::sort(gone.begin(), gone.end(), std::greater<>());
    int ship = loser;
    for (const int g : gone) {
        removeUnit(g);
        if (g < ship) {
            --ship;
        }
    }
    auto& l = save_.units[static_cast<std::size_t>(ship)];
    l.raw[4] = static_cast<std::uint8_t>(l.raw[4] | SaveUnit::kDamaged);
    l.holds = 0;
    l.orders = SaveUnit::kNoOrders;
    l.movesUsed = movementAllowance(ship);
    // The repair starts from how much the winner fell short of the loser's
    // strength (measured: a frigate beaten by a merchantman starts at 10 of
    // 16).
    const int beat = wt.combat * (winnerShip ? 1 : 2);
    int start = std::max(0, lt.combat - beat);
    if (l.type == kFrigate) {
        start = std::max(start, 4);
    } else if (l.type == kManOWar) {
        start = std::max(start, 8);
    }
    l.workTurns = start;
    GameNotice notice{"SHIPDAMAGE", {"", lt.name, ""}};
    notice.adjectives = {{0, nation}};
    if (drydock >= 0) {
        const auto& col = save_.colonies[static_cast<std::size_t>(drydock)];
        l.x = col.x;
        l.y = col.y;
        notice.strings[2] = col.name;
    } else {
        setEuropeStage(l, kPortStage);
        // The screen names the home port; the Crown's is the player's
        // (read from the original).
        const bool crown = (save_.header[18] & 0x01) != 0 && nation == crownNation();
        notice.value = crown ? human() : nation;
    }
    if (winner >= 0 || nation == human()) {
        notices_.push_back(std::move(notice));
    }
}

void Game::repairShips() {
    // Read from the original: a damaged ship's counter (byte 22) goes up a
    // turn, two in a drydock on the map, until it reaches the ship's
    // combat value. Measured: 10 of 16 for a frigate at Jamestown was
    // repaired three turns later.
    for (int i = 0; i < unitCount(); ++i) {
        if (!isDamagedShip(i)) {
            continue;
        }
        auto& u = save_.units[static_cast<std::size_t>(i)];
        u.workTurns += onMap(i) ? 2 : 1;
        const int needed = rules_.units[static_cast<std::size_t>(u.type)].combat;
        if (u.workTurns < needed) {
            continue;
        }
        u.raw[4] = static_cast<std::uint8_t>(u.raw[4] & ~SaveUnit::kDamaged);
        u.workTurns = 0;
        if (u.nation == human()) {
            // "Frigate has completed its repairs in Jamestown."
            GameNotice notice{"REFIT", {rules_.units[static_cast<std::size_t>(u.type)].name, ""}};
            if (const auto colony = colonyAt(u.x, u.y)) {
                notice.strings[1] = save_.colonies[static_cast<std::size_t>(*colony)].name;
            } else {
                notice.value = u.nation;
            }
            notices_.push_back(std::move(notice));
        }
    }
}

namespace {
constexpr int kLastShipType = 18;

bool mounted(int type) {
    return type == unit_type::kDragoon || type == unit_type::kScout || type == 7 || type == 8;
}
} // namespace

void Game::fightStartSound(int attackerNation, int attackerType, int defenderNation, int defenderType) {
    if (attackerNation != human() && defenderNation != human()) {
        return;
    }
    // Braves that fall on the player's men cry out first.
    if (attackerNation >= kEuropeanNations && defenderNation == human()) {
        sound(sounds::kWarCry + attackerType);
    }
    const auto attackOf = [&](int type) {
        return type >= 0 && type < static_cast<int>(rules_.units.size())
                   ? rules_.units[static_cast<std::size_t>(type)].attack
                   : 0;
    };
    const bool ship = attackerType >= unit_type::kCaravel && attackerType <= kLastShipType;
    if (ship || attackerType == kArtillery || defenderType == kArtillery) {
        sound(sounds::kCannon);
    } else if (mounted(attackerType)) {
        sound(sounds::kHorses);
    } else if (attackOf(attackerType) > 1 || attackOf(defenderType) > 1) {
        sound(sounds::kMuskets);
    } else {
        sound(sounds::kScuffle);
    }
}

void Game::fightWonSound(int attacker, int x, int y) {
    const auto& a = unit(attacker);
    const bool ship = a.type >= unit_type::kCaravel && a.type <= kLastShipType;
    if (ship || a.type == kArtillery) {
        sound(sounds::kCannonHit);
    } else if (colonyAt(x, y) || settlementAt(x, y)) {
        sound(sounds::kAtHome);
    } else {
        sound(sounds::kScuffle);
    }
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
    const bool shown = a.nation == human() || unit(defender).nation == human();
    fightStartSound(a.nation, a.type, unit(defender).nation, unit(defender).type);
    if (isShip(attacker) && isShip(defender)) {
        // Observed: after the roll, whoever won, a ship with less attack than
        // its attacker may get away; the odds are its share of the two
        // ships' speeds.
        const auto& at = rules_.units[static_cast<std::size_t>(unit(attacker).type)];
        const auto& dt = rules_.units[static_cast<std::size_t>(unit(defender).type)];
        if (dt.attack < at.attack) {
            const int fa = evasion(attacker);
            const int fd = evasion(defender);
            if (1 + nextRandom(fa + fd) <= fd) {
                // "English Merchantman evades Dutch Frigate."
                GameNotice notice{"EVASIVE", {"", dt.name, "", at.name}};
                notice.adjectives = {{0, unit(defender).nation}, {2, unit(attacker).nation}};
                notices_.push_back(std::move(notice));
                refreshUnitFlags();
                return CombatOutcome::Evaded;
            }
        }
        if (shown) {
            if (won) {
                fightWonSound(attacker, unit(defender).x, unit(defender).y);
            }
            sound(sounds::kShipBeaten);
        }
        if (won) {
            loseShip(defender, attacker);
        } else {
            loseShip(attacker, defender);
        }
        refreshUnitFlags();
        return won ? CombatOutcome::AttackerWon : CombatOutcome::DefenderWon;
    }
    if (won) {
        if (shown) {
            fightWonSound(attacker, unit(defender).x, unit(defender).y);
        }
        const bool gone = loseBattle(defender, attacker);
        promote(gone && defender < attacker ? attacker - 1 : attacker, s.attack, s.defense);
    } else {
        const bool gone = loseBattle(attacker, defender);
        promote(gone && attacker < defender ? defender - 1 : defender, s.defense, s.attack);
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
    // Veterans only go further during the war of independence, once the
    // Continental Army has been raised (the player's flag, whoever the
    // unit is); other experts never.
    const int from = u.profession;
    if (from == kVeteranSoldier) {
        constexpr std::uint8_t kDeclared = 0x01;
        constexpr std::uint8_t kMobilized = 0x08;
        if ((save_.header[18] & kDeclared) == 0 ||
            (save_.nations[static_cast<std::size_t>(this->human())].raw[0] & kMobilized) == 0) {
            return;
        }
    } else if (from != kIndenturedServant && from != kPettyCriminal && from != kIndianConvert &&
               from != kFreeColonist) {
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
                   : from == kVeteranSoldier    ? -1 // the Continental Army
                                                : kVeteranSoldier;
    if (to == from) {
        return;
    }
    const bool humanControl = u.nation >= 0 && u.nation < kEuropeanNations &&
                              save_.players[static_cast<std::size_t>(u.nation)].control == 0;
    if (to < 0) {
        if (!humanControl) {
            return;
        }
        u.type = u.type == unit_type::kSoldier ? kContinentalArmy : kContinentalCavalry;
    } else {
        u.profession = to;
    }
    if (!human) {
        return;
    }
    const auto typeName = u.type < static_cast<int>(rules_.units.size())
                              ? rules_.units[static_cast<std::size_t>(u.type)].name
                              : std::string();
    if (to < 0) {
        // "Our Veteran ... have hardened to Continental Army status" (the
        // original names the new kind).
        GameNotice notice{"CONTINENTAL", {typeName}};
        notice.unit = index;
        notices_.push_back(std::move(notice));
        return;
    }
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

bool Game::loseBattle(int loser, int winner) {
    auto& l = save_.units[static_cast<std::size_t>(loser)];
    const auto name = [&](int type) {
        return type >= 0 && type < static_cast<int>(rules_.units.size()) ? rules_.units[static_cast<std::size_t>(type)].name
                                                                         : std::string();
    };
    if (l.nation >= kEuropeanNations) {
        loseNative(loser);
        return true;
    }
    if (int to = demotedType(l.type); to >= 0) {
        // A Jesuit missionary who took up arms goes back to preaching.
        constexpr int kJesuit = 0x18;
        if (to == unit_type::kColonist && l.profession == kJesuit) {
            to = unit_type::kMissionary;
        }
        // "French Dragoons routed! Unit demoted to Soldiers status."
        GameNotice notice{"DEMOTE", {"", name(l.type), name(to)}};
        notice.adjectives = {{0, l.nation}};
        notices_.push_back(std::move(notice));
        // Measured: a fortified unit stays fortified.
        l.type = to;
        return false;
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
        return destroyed;
    }
    // Measured: a scout beaten by a village's brave is gone, without a
    // word. Natives take no one.
    if (!capturable(l.type) || winner < 0 || unit(winner).nation >= kEuropeanNations) {
        removeUnit(loser);
        return true;
    }
    const auto& w = unit(winner);
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
    return false;
}

} // namespace viceroy
