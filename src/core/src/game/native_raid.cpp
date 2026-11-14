// SPDX-License-Identifier: GPL-3.0-or-later
//
// A native unit attacks a European unit or colony: the fight, what the
// winner takes, and the raid a tribe's party makes on a colony. Read from
// the original with a debugger; see docs/rules/natives.md ("When the
// natives attack").
#include "viceroy/game/colony.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"

#include <algorithm>
#include <cstdlib>

namespace viceroy {

namespace {

constexpr int kNativesTune = 0x32; // "Natives", a driver command


constexpr int kBrave = 19;
constexpr int kArmedBrave = 20;
constexpr int kMountedBrave = 21;
constexpr int kFirstShip = 13;
constexpr int kLastShip = 18;
constexpr int kBuildingRows = 42;

// Unit bytes.
constexpr std::size_t kUnitFlags = 4;
constexpr std::uint8_t kKeepsArms = 0x10; // a beaten brave's arms go home
constexpr std::size_t kHome = 6;

// Tribe bytes: muskets at 7, horse herds at 8, horses at 10 (16 bits),
// the relation to each nation at 58.
constexpr std::size_t kTribeMuskets = 7;
constexpr std::size_t kTribeHerds = 8;
constexpr std::size_t kTribeHorses = 10;
constexpr std::size_t kTribeRelations = 58;
constexpr std::uint8_t kAngry = 0x02;
constexpr int kHorsesToRide = 25;

// LABELS.TXT @MISC rows.
constexpr int kWildernessLabel = 17;
constexpr int kDefeatLabel = 73; // and "defeats" one on

// Rows of @CARGO and @JOB, and colony bytes.
constexpr int kHorses = 8;
constexpr int kMuskets = 15;
constexpr int kCarpenter = 13;
constexpr std::size_t kWarehouseLevel = 149;
constexpr std::size_t kCapitolLevel = 150;
constexpr int kWarehouseRow = 15;
constexpr int kCapitolRow = 30;

// The raid's outcomes.
enum Raid { kNothing = 0, kStores = 1, kBurn = 2, kShip = 3, kGold = 4 };

std::size_t tribeByte(int tribe, std::size_t at) {
    return static_cast<std::size_t>(tribe) * kTribeRecordSize + at;
}

bool isShipType(int type) {
    return type >= kFirstShip && type <= kLastShip;
}

bool hasBuilding(const SaveColony& c, int row) {
    return row >= 0 && row < 48 && ((c.buildings >> row) & 1U) != 0;
}

// The kind of building a row of @BUILDING belongs to, if any.
const BuildingField* fieldOf(int row) {
    for (const auto& f : kBuildingFields) {
        if (f.firstRow >= 0 && row >= f.firstRow && row < f.firstRow + f.bits) {
            return &f;
        }
    }
    return nullptr;
}

int firstOfKind(int row) {
    const auto* f = fieldOf(row);
    return f ? f->firstRow : row;
}

int nextLevel(int row) {
    const auto* f = fieldOf(row);
    return f && row + 1 < f->firstRow + f->bits ? row + 1 : -1;
}

} // namespace

CombatOutcome Game::nativeAttack(int index, int x, int y) {
    auto& a = save_.units[static_cast<std::size_t>(index)];
    const int nation = a.nation;
    const int tribe = nation - kEuropeanNations;
    const int home = a.raw[kHome];
    const int attackerType = a.type;
    const auto colony = colonyAt(x, y);
    const int defender = defenderAt(x, y, index);
    if (tribe < 0 || tribe >= kTribes || (defender < 0 && !colony)) {
        return CombatOutcome::DefenderWon;
    }
    // Who stands: the best unit there, or else one of the colony's people.
    int colonist = -1;
    CombatStrengths s{};
    int defenderNation = -1;
    int defenderType = 0;
    if (defender >= 0) {
        s = combatStrengths(index, defender);
        defenderNation = unit(defender).nation;
        defenderType = unit(defender).type;
    } else {
        colonist = pickColonyDefender(*colony);
        s = colonyStrengths(index, *colony, colonist);
        defenderNation = save_.colonies[static_cast<std::size_t>(*colony)].nation;
        defenderType = unit_type::kColonist;
    }
    // The fight ends the attacker's turn.
    save_.units[static_cast<std::size_t>(index)].movesUsed = movementAllowance(index);
    const bool human = defenderNation == this->human();
    const int roll = 1 + nextRandom(s.attack + s.defense);
    bool won = s.attack + s.defense > 0 && attackerWins(s.attack, s.defense, roll);
    // A brave without muskets or horses never beats the player's
    // artillery, and its tribe then raids the colony whatever its walls.
    bool againstArtillery = false;
    if (human && attackerType == kBrave && defenderType == kArtillery) {
        won = false;
        againstArtillery = true;
    }

    // The nearest colony names the place ("near Jamestown"); the fight was
    // at it when it is no squares away.
    const int nearest = nearestColony(save_, x, y, -1);
    const bool atColony = nearest >= 0 && save_.colonies[static_cast<std::size_t>(nearest)].x == x &&
                          save_.colonies[static_cast<std::size_t>(nearest)].y == y;
    const auto& tribes = rules_.tribes;
    const std::string tribeName =
        static_cast<std::size_t>(tribe) < tribes.size() ? tribes[static_cast<std::size_t>(tribe)].name : "";
    const std::string tribeAdjective =
        static_cast<std::size_t>(tribe) < tribes.size() ? tribes[static_cast<std::size_t>(tribe)].adjective : "";
    const auto typeName = [&](int type) {
        return type >= 0 && type < static_cast<int>(rules_.units.size())
                   ? rules_.units[static_cast<std::size_t>(type)].name
                   : std::string();
    };
    GameNotice notice{"",
                      {tribeName, "", "", nearest >= 0 ? save_.colonies[static_cast<std::size_t>(nearest)].name : "",
                       tribeAdjective}};
    notice.adjectives = {{1, defenderNation}};
    if (nearest < 0) {
        notice.labels = {{3, kWildernessLabel}};
    }
    auto& block = save_.tribesAndMore;
    const bool hasBlock = block.size() >= tribeByte(tribe, kTribeRecordSize);
    const auto addAt = [&](std::size_t at, int amount) {
        if (hasBlock) {
            block[tribeByte(tribe, at)] = static_cast<std::uint8_t>(block[tribeByte(tribe, at)] + amount);
        }
    };
    int tension = 0;

    if (!won) {
        const int dIndex = defender >= 0 && defender > index ? defender - 1 : defender;
        loseNative(index);
        if (dIndex >= 0) {
            promote(dIndex, s.defense, s.attack);
        }
        if (atColony) {
            raidColony(nation, nearest, home, againstArtillery);
        } else if (human) {
            // "Dutch Soldiers defeat Sioux near Jamestown!"
            notice.section = "INDIANLOSE";
            notice.music = {{MusicCue::Kind::ChangeMood, 4}};
            notice.strings[2] = typeName(defenderType);
            notice.labels.push_back({4, kDefeatLabel + (defenderType < 7 ? 0 : 1)});
            notices_.push_back(std::move(notice));
        }
        refreshUnitFlags();
        return CombatOutcome::DefenderWon;
    }

    // The tribe's alarm at the home village is spent.
    if (home >= 0 && home < static_cast<int>(save_.settlements.size()) && defenderNation >= 0 &&
        defenderNation < kEuropeanNations) {
        auto& hs = save_.settlements[static_cast<std::size_t>(home)];
        hs.raw[static_cast<std::size_t>(10 + 2 * defenderNation)] = 0;
        hs.raw[static_cast<std::size_t>(11 + 2 * defenderNation)] = 0;
    }
    if (defender < 0) {
        // Nobody under arms: one of the colony's people dies, or the colony
        // burns with its last.
        auto& c = save_.colonies[static_cast<std::size_t>(*colony)];
        const int skill = c.skills[static_cast<std::size_t>(std::clamp(colonist, 0, kMaxColonists - 1))];
        const int job = professionJob(skill);
        notice.strings[2] = job >= 0 && job < static_cast<int>(rules_.jobs.size())
                                ? rules_.jobs[static_cast<std::size_t>(job)].name
                                : std::string();
        if (c.population > 1) {
            save_.units[static_cast<std::size_t>(index)].raw[kUnitFlags] |= kKeepsArms;
            removeColonist(c, colonist);
            notice.section = "INDIANWINCOLONY";
            tension = (human ? save_.difficulty : 0) - 10;
        } else {
            if (c.stock[kHorses] != 0) {
                addAt(kTribeHerds, 1);
            }
            if (c.stock[kMuskets] != 0) {
                addAt(kTribeMuskets, 1);
            }
            const int cx = c.x;
            const int cy = c.y;
            removeColony(*colony);
            // Ships in its harbour are lost with it.
            for (bool more = true; more;) {
                more = false;
                for (const int i : unitsAt(cx, cy)) {
                    if (isShip(i)) {
                        loseShip(i, -1);
                        more = true;
                        break;
                    }
                }
            }
            notice.section = "INDIANBURNCOLONY";
            tension = -50;
        }
    } else {
        // An ambush in the open: the braves take the beaten scouts' and
        // dragoons' horses and the soldiers' muskets.
        int gained = 0;
        auto& w = save_.units[static_cast<std::size_t>(index)];
        if (colony) {
            w.raw[kUnitFlags] |= kKeepsArms;
        } else if ((defenderType == unit_type::kDragoon || defenderType == unit_type::kScout) &&
                   (attackerType == kBrave || attackerType == kArmedBrave)) {
            w.type += 2;
            w.movesUsed = movementAllowance(index);
            addAt(kTribeHerds, 1);
            gained = 2;
        } else if (defenderType == unit_type::kSoldier && (attackerType == kBrave || attackerType == kMountedBrave)) {
            w.type += 1;
            gained = 1;
        }
        loseBattle(defender, index);
        notice.section = gained == 1 ? "INDIANWIN1" : gained == 2 ? "INDIANWIN2" : "INDIANWIN0";
        notice.strings[2] = typeName(defenderType);
        tension = (human ? save_.difficulty >> 1 : 0) - 5;
    }
    if (human) {
        // Read from the original: the natives' own tune.
        notice.music = {{MusicCue::Kind::Pick, kNativesTune}};
        notices_.push_back(std::move(notice));
    }
    if (tension != 0 && hasBlock && defenderNation >= 0 && defenderNation < kEuropeanNations &&
        (block[tribeByte(tribe, kTribeRelations + static_cast<std::size_t>(defenderNation))] & kAngry) == 0) {
        changeTension(tribe, defenderNation, tension);
    }
    refreshUnitFlags();
    return CombatOutcome::AttackerWon;
}

void Game::raidColony(int nation, int colonyIndex, int home, bool always) {
    const int tribe = nation - kEuropeanNations;
    auto& c = save_.colonies[static_cast<std::size_t>(colonyIndex)];
    const int owner = c.nation;
    if (tribe < 0 || tribe >= kTribes || owner < 0 || owner >= kEuropeanNations) {
        return;
    }
    const bool human = owner == this->human();
    const int difficulty = save_.difficulty;
    // The walls first: a roll of -1 to 11 (moved by the level against the
    // player) under three per level of fortification and one does nothing.
    int kind = kNothing;
    const int walls = 3 * buildingLevel(c, BuildingKind::Fortification) + 1;
    int roll = nextRandom(13) - 1;
    if (human) {
        roll += difficulty - 2;
    }
    if (roll >= walls || always) {
        kind = 1 + nextRandom(4);
        if (40 * (2 - difficulty) > save_.turn && difficulty <= 1 && (kind == kBurn || kind == kShip)) {
            kind = kNothing;
        }
        if (kind == kBurn) {
            const int level = human ? difficulty : 1;
            if (nextRandom(9) > level + 2 || hasBuilding(c, 1)) {
                kind = kStores;
            }
        }
        if (kind == kGold && hasBuilding(c, 0)) {
            kind = kStores;
        }
        if (kind == kShip && hasBuilding(c, 2)) {
            kind = kNothing;
        }
        if (kind == kStores && hasBuilding(c, 0) && nextRandom(9) > difficulty) {
            kind = kNothing;
        }
    }
    auto& block = save_.tribesAndMore;
    const bool hasBlock = block.size() >= tribeByte(tribe, kTribeRecordSize);

    // Choose what is hit; when there is nothing to take the raid comes to
    // nothing.
    int good = -1;
    int row = -1;
    int ship = -1;
    int gold = 0;
    if (kind == kStores) {
        int tries = 0;
        do {
            ++tries;
            good = nextRandom(16);
            if (hasBlock && block[tribeByte(tribe, kTribeHerds)] == 0 && tries == 1 &&
                c.stock[static_cast<std::size_t>(good)] > 52 && nextRandom(2) == 0) {
                good = kHorses;
            }
        } while (tries < 100 && c.stock[static_cast<std::size_t>(good)] < 10);
        if (tries >= 100) {
            kind = kNothing;
        }
    } else if (kind == kBurn) {
        const bool buildingInProgress = c.construction >= 0 && c.construction < kBuildingRows;
        bool ok = false;
        for (int tries = 1; tries <= 100; ++tries) {
            row = nextRandom(kBuildingRows);
            ok = row != 35 && firstOfKind(row) != 9 &&
                 !(buildingInProgress && firstOfKind(c.construction) == firstOfKind(row)) && row != 39 && row != 21 &&
                 row != 24 && row != 27 && row != 0 && row != 1 && row != 2 && row != 32;
            if (ok && hasBuilding(c, row)) {
                break;
            }
            ok = false;
        }
        if (!ok) {
            kind = kNothing;
        } else {
            while (nextLevel(row) >= 0 && hasBuilding(c, nextLevel(row))) {
                row = nextLevel(row);
            }
        }
    } else if (kind == kShip) {
        for (const int u : unitsAt(c.x, c.y)) {
            if (isShipType(unit(u).type)) {
                ship = u;
                break;
            }
        }
        if (ship < 0) {
            kind = kNothing;
        }
    } else if (kind == kGold) {
        auto& treasury = save_.nations[static_cast<std::size_t>(owner)].gold;
        const long long share = static_cast<long long>(treasury) * c.population / (nationPeople(owner) + 1) + 10;
        const int most = static_cast<int>(std::min<long long>(share, 0x7FFF));
        gold = most >= 50 ? 50 + nextRandom(most - 49) : 50;
        if (treasury < gold) {
            kind = kNothing;
        }
    }

    GameNotice notice{"",
                      {[&] {
                           const auto t = static_cast<std::size_t>(tribe);
                           return t < rules_.tribes.size() ? rules_.tribes[t].name : std::string();
                       }(),
                       c.name, "", ""}};
    notice.adjectives = {{3, owner}};
    notice.colony = colonyIndex;
    int tension = 0;
    // Read from the original: a raid that comes to nothing makes the next
    // tune a cheerful one; any other plays the natives' tune.
    notice.music = {kind == kNothing ? MusicCue{MusicCue::Kind::SetMood, 2}
                                     : MusicCue{MusicCue::Kind::Pick, kNativesTune}};
    switch (kind) {
    case kStores: {
        auto& stock = c.stock[static_cast<std::size_t>(good)];
        if (stock == 0) {
            break;
        }
        const int half = stock / 2;
        const int least = std::min(half, 10);
        int amount = least + nextRandom(std::max(1, half - least + 1));
        amount = std::max(1, std::min(stock, amount));
        stock -= amount;
        notice.section = "RAIDSTORES";
        notice.strings[2] = good < static_cast<int>(rules_.cargo.size())
                                ? rules_.cargo[static_cast<std::size_t>(good)].name
                                : std::string();
        if (hasBlock && good == kHorses) {
            block[tribeByte(tribe, kTribeHerds)] = static_cast<std::uint8_t>(block[tribeByte(tribe, kTribeHerds)] + 1);
            const auto at = tribeByte(tribe, kTribeHorses);
            const auto horses = static_cast<std::uint16_t>((block[at] | (block[at + 1] << 8)) + kHorsesToRide);
            block[at] = static_cast<std::uint8_t>(horses & 0xFF);
            block[at + 1] = static_cast<std::uint8_t>(horses >> 8);
        }
        if (hasBlock && good == kMuskets) {
            auto& muskets = block[tribeByte(tribe, kTribeMuskets)];
            muskets = static_cast<std::uint8_t>(muskets + (amount >= 50 ? 2 : 1));
        }
        tension = -4;
        break;
    }
    case kBurn: {
        const auto nameOf = [&](int r) {
            return r < static_cast<int>(rules_.buildings.size()) ? rules_.buildings[static_cast<std::size_t>(r)].name
                                                                 : std::string();
        };
        notice.section = "RAIDBURN";
        notice.strings[2] = nameOf(row);
        if (row == kWarehouseRow || row == kCapitolRow) {
            // These keep their level in a byte of their own.
            auto& level = c.raw[row == kWarehouseRow ? kWarehouseLevel : kCapitolLevel];
            level = static_cast<std::uint8_t>(level - 1);
            if (level == 0) {
                c.buildings &= ~(std::uint64_t{1} << row);
            }
            if (level != 0 || row == kCapitolRow) {
                notice.strings[2] = nameOf(row + 1);
            }
        } else {
            if (firstOfKind(row) == row) {
                // Whoever worked in it goes to the carpenter's.
                for (int i = 0; i < std::min(c.population, kMaxColonists); ++i) {
                    const auto in = buildingForJob(c.jobs[static_cast<std::size_t>(i)]);
                    const auto* f = fieldOf(row);
                    if (in && f && &kBuildingFields[static_cast<std::size_t>(*in)] == f) {
                        c.jobs[static_cast<std::size_t>(i)] = kCarpenter;
                    }
                }
            }
            c.buildings &= ~(std::uint64_t{1} << row);
        }
        tension = -12;
        break;
    }
    case kShip:
        notice.section = "RAIDSHIP";
        notice.strings[2] = rules_.units[static_cast<std::size_t>(unit(ship).type)].name;
        if (human) {
            notices_.push_back(notice);
            notice.section.clear();
        }
        loseShip(ship, -1);
        tension = -16;
        break;
    case kGold:
        save_.nations[static_cast<std::size_t>(owner)].gold -= gold;
        notice.section = "RAIDGOLD";
        notice.numbers = {gold};
        tension = -8;
        break;
    default:
        // "Sioux raiding party wiped out in Jamestown! Colonists jubilant!"
        notice.section = "RAIDNOTHING";
        break;
    }
    if (human && !notice.section.empty()) {
        notices_.push_back(std::move(notice));
    }
    if (tension != 0 && hasBlock &&
        (block[tribeByte(tribe, kTribeRelations + static_cast<std::size_t>(owner))] & kAngry) == 0) {
        changeTension(tribe, owner, tension);
    }
    if (home >= 0 && home < static_cast<int>(save_.settlements.size())) {
        auto& hs = save_.settlements[static_cast<std::size_t>(home)];
        hs.raw[static_cast<std::size_t>(10 + 2 * owner)] = 0;
        hs.raw[static_cast<std::size_t>(11 + 2 * owner)] = 0;
    }
}

} // namespace viceroy
