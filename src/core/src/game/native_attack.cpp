// SPDX-License-Identifier: GPL-3.0-or-later
//
// A European unit attacks a native settlement: who defends it, the
// strengths, and what a won fight takes from the village. Read from the
// original with a debugger and checked against its answers; see
// docs/rules/natives.md.
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"
#include "viceroy/game/sounds.hpp"

#include <algorithm>

namespace viceroy {

namespace {

constexpr int kBrave = 19;
constexpr int kArmed = 1;   // armed braves are one type on
constexpr int kMounted = 2; // mounted braves two
constexpr int kArmedBrave = 20;
constexpr int kMountedBrave = 21;
constexpr int kMountedWarrior = 22;
constexpr int kFirstShip = 13;
constexpr int kLastShip = 18;
constexpr int kSpain = 2;
constexpr int kCortes = 10;    // rows of @FATHERS
constexpr int kSepulveda = 23;
constexpr int kLasCasas = 24;
constexpr int kConvert = 0x1B;
constexpr int kJesuit = 0x18;
constexpr int kFreeColonist = 0x1C;
constexpr int kTreasure = 10;
constexpr int kMissionary = 3;
constexpr int kCapitalRow = 4; // the row of @LEVELS that names a capital

// Relations: the nation's byte 52 + other, the tribe's byte 58 + nation.
constexpr std::size_t kNationRelations = 52;
constexpr std::size_t kTribeRelations = 58;
constexpr std::uint8_t kWar = 0x04;
constexpr int kAttackTension = 10;
constexpr int kAttackAlarm = 0x100;
constexpr int kBowedTension = 15;

// The tribe's stock: muskets at 7, horses at 10 (16 bits).
constexpr std::size_t kTribeMuskets = 7;
constexpr std::size_t kTribeHorses = 10;
constexpr int kHorsesToRide = 25;
// The nation's count of burned villages.
constexpr std::size_t kVillagesBurned = 24;
// The number of settlements of each tribe, in the block after the tribe
// records.
constexpr std::size_t kSettlementCounts = static_cast<std::size_t>(kTribes) * kTribeRecordSize + 581;

bool isShipType(int type) {
    return type >= kFirstShip && type <= kLastShip;
}

std::size_t tribeByte(int tribe, std::size_t at) {
    return static_cast<std::size_t>(tribe) * kTribeRecordSize + at;
}

// The settlement's alarm toward a nation: 16 bits from byte 10.
void addAlarm(SaveSettlement& s, int nation, int amount) {
    const auto at = static_cast<std::size_t>(10 + 2 * nation);
    const int value = s.raw[at] | (s.raw[at + 1] << 8);
    const auto v = static_cast<std::uint16_t>(std::clamp(value + amount, 0, 0xFFFF));
    s.raw[at] = static_cast<std::uint8_t>(v & 0xFF);
    s.raw[at + 1] = static_cast<std::uint8_t>(v >> 8);
}

} // namespace

bool Game::canAttackVillage(int index) const {
    const auto& u = unit(index);
    if (u.nation >= kEuropeanNations || isShipType(u.type) || u.type < 0 ||
        u.type >= static_cast<int>(rules_.units.size())) {
        return false;
    }
    return rules_.units[static_cast<std::size_t>(u.type)].attack > 0;
}

void Game::attackVillageWar(int index, int settlement) {
    const int nation = unit(index).nation;
    auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    const int tribe = tribeOf(s);
    if (nation < 0 || nation >= kEuropeanNations || tribe < 0 || tribe >= kTribes) {
        return;
    }
    // Measured: the war bit on both sides, the tribe's tension up by 10
    // and twice the difficulty level (10, 14, 18 on levels 0, 2, 4, from 0
    // and from 40 alike), and the high byte of the village's alarm one up.
    auto& relation = save_.nations[static_cast<std::size_t>(nation)].raw[kNationRelations + static_cast<std::size_t>(s.nation)];
    relation = static_cast<std::uint8_t>(relation | kWar);
    if (save_.tribesAndMore.size() >= tribeByte(tribe, kTribeRecordSize)) {
        auto& back = save_.tribesAndMore[tribeByte(tribe, kTribeRelations + static_cast<std::size_t>(nation))];
        back = static_cast<std::uint8_t>(back | kWar);
    }
    changeTension(tribe, nation, kAttackTension + 2 * save_.difficulty);
    addAlarm(s, nation, kAttackAlarm);
}

int Game::villageDefenderUnit(int settlement, int attacker) const {
    const auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    return defenderAt(s.x, s.y, attacker);
}

SaveUnit Game::villageBrave(int settlement) const {
    const auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    const int tribe = tribeOf(s);
    SaveUnit u;
    u.x = s.x;
    u.y = s.y;
    u.nation = s.nation;
    u.type = kBrave;
    const auto& block = save_.tribesAndMore;
    if (tribe >= 0 && tribe < kTribes && block.size() >= tribeByte(tribe, kTribeRecordSize)) {
        if (block[tribeByte(tribe, kTribeMuskets)] != 0) {
            u.type += kArmed;
        }
        const int horses = static_cast<std::int16_t>(static_cast<std::uint16_t>(
            block[tribeByte(tribe, kTribeHorses)] | (block[tribeByte(tribe, kTribeHorses) + 1] << 8)));
        if (horses >= kHorsesToRide) {
            u.type += kMounted;
        }
    }
    return u;
}

int Game::settlementQuarters(int settlement) const {
    // Read from the original: +50% for a camp or village, +100% for a
    // city of the advanced and civilized tribes, twice that for a capital.
    const auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    int m = tribeLevel(save_, tribeOf(s)) >= 2 ? 4 : 2;
    if ((s.raw[3] & kSettlementCapital) != 0) {
        m *= 2;
    }
    return m;
}

CombatAnalysis Game::analyseVillageAttack(int attacker, int settlement) const {
    const int defender = villageDefenderUnit(settlement, attacker);
    CombatAnalysis a = defender >= 0 ? analyseAttack(attacker, defender) : CombatAnalysis{};
    if (defender < 0) {
        // Seen: the attacker as against a unit, then the village's brave
        // ("Braves 1") and the settlement ("Camp +50%" with its picture).
        const SaveUnit brave = villageBrave(settlement);
        const auto& au = unit(attacker);
        a.attacker.unit = attacker;
        a.attacker.base = isDamagedArtillery(attacker) ? 5 : rules_.units[static_cast<std::size_t>(au.type)].attack;
        if ((au.type == unit_type::kSoldier || au.type == unit_type::kDragoon) && au.profession == 0x15) {
            a.attacker.bonuses.push_back({combat_label::kVeteran, 50});
        }
        a.attacker.bonuses.push_back({combat_label::kAttackBonus, 50});
        a.defender.settlement = settlement;
        a.defender.braveType = brave.type;
        a.defender.base = rules_.units.size() > static_cast<std::size_t>(brave.type)
                              ? rules_.units[static_cast<std::size_t>(brave.type)].combat
                              : 1;
    } else {
        // A brave standing in the village: the settlement instead of the
        // square, no artillery in the open, and fortified only where the
        // settlement gives less than +125%.
        const bool strong = settlementQuarters(settlement) >= 5;
        const auto drop = [strong](std::vector<CombatBonus>& bonuses) {
            bonuses.erase(std::remove_if(bonuses.begin(), bonuses.end(),
                                         [strong](const CombatBonus& b) {
                                             return b.terrainPicture || b.label == combat_label::kArtilleryInOpen ||
                                                    (strong && b.label == combat_label::kFortified);
                                         }),
                          bonuses.end());
        };
        drop(a.attacker.bonuses);
        drop(a.defender.bonuses);
    }
    CombatBonus bonus{0, settlementQuarters(settlement) * 25};
    bonus.settlementPicture = settlement;
    a.defender.bonuses.insert(a.defender.bonuses.begin(), bonus);
    if (unit(attacker).nation == kSpain) {
        a.attacker.bonuses.push_back({combat_label::kSpainBonus, 50});
    }
    return a;
}

CombatStrengths Game::villageStrengths(int attacker, int settlement, bool beforeLevels) const {
    const int defender = villageDefenderUnit(settlement, attacker);
    if (defender >= 0) {
        return combatStrengths(attacker, defender, beforeLevels);
    }
    return strengthsAgainst(attacker, villageBrave(settlement), false, false, 0, beforeLevels);
}

CombatOutcome Game::attackVillage(int attacker, int settlement) {
    const int defender = villageDefenderUnit(settlement, attacker);
    const auto s = villageStrengths(attacker, settlement);
    const int roll = 1 + nextRandom(s.attack + s.defense);
    const bool won = s.attack + s.defense > 0 && attackerWins(s.attack, s.defense, roll);
    save_.units[static_cast<std::size_t>(attacker)].movesUsed = movementAllowance(attacker);
    const auto& v = save_.settlements[static_cast<std::size_t>(settlement)];
    const bool shown = unit(attacker).nation == human();
    fightStartSound(unit(attacker).nation, unit(attacker).type, kEuropeanNations + tribeOf(v),
                    defender >= 0 ? unit(defender).type : villageBrave(settlement).type);
    if (won) {
        int a = attacker;
        if (shown) {
            // Read from the original: a beaten defender, a village that
            // loses a person, or one that burns.
            if (defender >= 0) {
                fightWonSound(attacker, v.x, v.y);
            } else {
                sound(v.population > 1 ? sounds::kVillageHit : sounds::kBurned);
            }
        }
        if (defender >= 0) {
            loseBattle(defender, attacker);
            a = defender < attacker ? attacker - 1 : attacker;
        } else {
            winAgainstVillage(attacker, settlement);
        }
        promote(a, s.attack, s.defense);
    } else {
        loseBattle(attacker, defender);
    }
    refreshUnitFlags();
    return won ? CombatOutcome::AttackerWon : CombatOutcome::DefenderWon;
}

void Game::loseNative(int loser) {
    // Read from the original: half the time (always for a brave marked in
    // its byte 4) the tribe gets back the muskets and horses of a beaten
    // brave. The brave is gone, without a word.
    const auto& l = unit(loser);
    const int tribe = l.nation - kEuropeanNations;
    auto& block = save_.tribesAndMore;
    constexpr std::uint8_t kKeepsArms = 0x10;
    if (tribe >= 0 && tribe < kTribes && block.size() >= tribeByte(tribe, kTribeRecordSize) &&
        ((l.raw[4] & kKeepsArms) != 0 || nextRandom(2) != 0)) {
        if (l.type == kArmedBrave || l.type == kMountedWarrior) {
            auto& muskets = block[tribeByte(tribe, kTribeMuskets)];
            muskets = static_cast<std::uint8_t>(muskets + 1);
        }
        if (l.type == kMountedBrave || l.type == kMountedWarrior) {
            const auto at = tribeByte(tribe, kTribeHorses);
            const auto horses = static_cast<std::uint16_t>((block[at] | (block[at + 1] << 8)) + kHorsesToRide);
            block[at] = static_cast<std::uint8_t>(horses & 0xFF);
            block[at + 1] = static_cast<std::uint8_t>(horses >> 8);
        }
    }
    removeUnit(loser);
}

void Game::winAgainstVillage(int attacker, int settlement) {
    auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    const auto& a = unit(attacker);
    const int nation = a.nation;
    const int tribe = tribeOf(s);
    const int mission = missionOf(s);
    const bool expertMission = mission >= 0 && (s.raw[5] & kExpertMission) != 0;
    const bool capital = (s.raw[3] & kSettlementCapital) != 0;
    const int sx = s.x;
    const int sy = s.y;
    const int ax = a.x;
    const int ay = a.y;
    const std::string adjective = static_cast<std::size_t>(tribe) < rules_.tribes.size()
                                      ? rules_.tribes[static_cast<std::size_t>(tribe)].adjective
                                      : std::string();
    bool burned = false;
    // Measured: a village of more than one loses a person; the last one's
    // fall burns it, and the mission's missionary comes out.
    if (s.population > 1) {
        --s.population;
    } else {
        constexpr std::uint8_t kBurnedByHuman = 0x40;
        if (nation == human() && save_.tribesAndMore.size() >= tribeByte(tribe, kTribeRecordSize)) {
            auto& flags = save_.tribesAndMore[tribeByte(tribe, 3)];
            flags = static_cast<std::uint8_t>(flags | kBurnedByHuman);
        }
        burnSettlement(settlement);
        burned = true;
        if (mission == nation) {
            addBuiltUnit(sx, sy, nation, kMissionary);
            save_.units.back().profession = expertMission ? kJesuit : kFreeColonist;
        }
    }
    // Read from the original: the people of the attacker's own mission may
    // flee to it as converts.
    if (mission == nation) {
        int chance = expertMission ? 8 : 4;
        if (nation == kSpain) {
            chance += 4;
        }
        if (hasFather(nation, kSepulveda)) {
            chance += 4;
        }
        if (hasFather(nation, kLasCasas)) {
            chance -= 4;
        }
        if (nextRandom(13) < chance) {
            addColonist(ax, ay, nation);
            save_.units.back().profession = kConvert;
            save_.units.back().raw[23] = kConvert;
            // "Frightened Apache flock to Dutch mission as converts."
            GameNotice notice{"INDIANSLAVES", {adjective, ""}};
            notice.adjectives = {{1, nation}};
            if (nation == human()) {
                notice.music = {{MusicCue::Kind::ChangeMood, 4}}; // read from the original
            }
            notices_.push_back(std::move(notice));
        }
    }
    if (!burned) {
        return;
    }
    if (nation >= 0 && nation < kEuropeanNations) {
        auto& count = save_.nations[static_cast<std::size_t>(nation)].raw[kVillagesBurned];
        count = static_cast<std::uint8_t>(count + 1);
    }
    const int treasure = villageTreasure(nation, tribe, capital);
    // "Dutch loot and burn Apache Camp! ..." with what the ruins hold, or
    // only the burning. The screen names the place by the row of @LEVELS
    // in the notice's value: the tribe's level, or the capital's row.
    const int place = capital ? kCapitalRow : tribeLevel(save_, tribe);
    if (treasure > 0) {
        addBuiltUnit(sx, sy, nation, kTreasure);
        save_.units.back().profession = treasure;
        GameNotice notice{"LOOT", {"", adjective, ""}, place};
        notice.adjectives = {{0, nation}};
        notice.numbers = {treasure * 100};
        if (nation == human()) {
            notice.music = {{MusicCue::Kind::ChangeMood, 2}}; // read from the original
        }
        notices_.push_back(std::move(notice));
    } else {
        GameNotice notice{"LOOT2", {"", adjective, ""}, place};
        notice.adjectives = {{0, nation}};
        notices_.push_back(std::move(notice));
    }
    if (!capital) {
        return;
    }
    // Read from the original: the tribe's capital burned, it calms down
    // to 15, forgets every village's alarm, and bows.
    if (tribeTension(save_, tribe, nation) > kBowedTension) {
        setTribeTension(save_, tribe, nation, kBowedTension);
    }
    for (auto& other : save_.settlements) {
        if (tribeOf(other) == tribe && nation >= 0 && nation < kEuropeanNations) {
            other.raw[static_cast<std::size_t>(10 + 2 * nation)] = 0;
            other.raw[static_cast<std::size_t>(11 + 2 * nation)] = 0;
        }
    }
    if (nation == human()) {
        // "The Apache tribe bows before the might of the Dutch."
        GameNotice notice{"INDIANBOW", {adjective, ""}};
        notice.adjectives = {{1, nation}};
        notice.value = tribe;
        notices_.push_back(std::move(notice));
    }
}

void Game::burnSettlement(int settlement) {
    // Measured: the settlement is gone with the braves who live there;
    // the others count their homes one lower, and the tribe has one
    // settlement less.
    const auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    const int tribe = tribeOf(s);
    auto& sq = save_.map.at(s.x, s.y);
    sq.flags = static_cast<std::uint8_t>(sq.flags & ~MapSquare::kHasColony);
    constexpr std::size_t kHome = 6;
    for (int i = unitCount() - 1; i >= 0; --i) {
        const auto& u = unit(i);
        if (u.nation >= kEuropeanNations && u.raw[kHome] == settlement) {
            removeUnit(i);
        }
    }
    for (auto& u : save_.units) {
        if (u.nation >= kEuropeanNations && u.raw[kHome] != 0xFF && u.raw[kHome] > settlement) {
            --u.raw[kHome];
        }
    }
    const auto at = kSettlementCounts + static_cast<std::size_t>(tribe);
    if (tribe >= 0 && tribe < kTribes && at < save_.tribesAndMore.size() && save_.tribesAndMore[at] > 0) {
        --save_.tribesAndMore[at];
    }
    save_.settlements.erase(save_.settlements.begin() + settlement);
}

int Game::villageTreasure(int nation, int tribe, bool capital) {
    // Read from the original, in hundreds. The poorer tribes often have
    // nothing (a camp more often for others than Spain); a capital and
    // Cortes always find some.
    const bool spain = nation == kSpain;
    const bool cortes = hasFather(nation, kCortes);
    const auto between = [this](int low, int high) { return low + nextRandom(high - low + 1); };
    int amount = 0;
    const int level = tribeLevel(save_, tribe);
    if (level <= 1) {
        const int limit = level == 0 ? (spain ? 3 : 6) : 2;
        if (between(0, limit) != 0 && !capital && !cortes) {
            return 0;
        }
        amount = level == 0 ? between(2, 4) : between(3, 8);
        if (capital) {
            amount *= 2;
        }
        if (cortes) {
            amount += amount >> 1;
        }
    } else if (level == 2) {
        amount = (capital ? between(4, 10) : between(2, 6)) + (cortes ? 6 : 0) + (spain ? 3 : 0);
        amount *= 10;
    } else {
        amount = (between(0, 4) + 2) * ((capital ? 25 : 16) + (cortes ? 10 : 0) + (spain ? 5 : 0));
    }
    return amount;
}

} // namespace viceroy
