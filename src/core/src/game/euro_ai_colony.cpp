// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a computer nation's colony plans for itself at the start of the
// nation's turn: the guards it wants, what it is short of, which units go
// in and which colonist goes out, the goods it asks its ships for, tools it
// buys. Read from the original with a debugger and a disassembler (observed
// only); the rules are in our own words in docs/rules/european-ai.md.
#include "viceroy/game/colony.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"

#include <algorithm>
#include <cstdlib>

namespace viceroy {

namespace {

constexpr int kRegions = 16;
constexpr int kTypesCounted = 19;
constexpr int kFirstShip = 13;
constexpr int kLastShip = 18;
constexpr int kSpain = 2;

// The block after the tribe records.
constexpr std::size_t kExtra = static_cast<std::size_t>(kTribes) * kTribeRecordSize;
constexpr std::size_t kTraits = kExtra + 0; // 3 signed bytes per nation
constexpr std::size_t kAttack = kExtra + 44; // words
constexpr std::size_t kByType = kExtra + 64;
constexpr std::size_t kRegionFlags = kExtra + 156;
constexpr std::size_t kRegionAttackOut = kExtra + 300;
constexpr std::size_t kTribeSettlements = kExtra + 581;
constexpr std::size_t kTribeRegionDefence = kExtra + 589;
constexpr std::size_t kBlockEnd = kExtra + 727;

// Nation record.
constexpr std::size_t kRelations = 52;
constexpr std::size_t kTribeRelations = 58; // the tribe's byte 58 + nation

std::size_t tribeByte(int tribe, std::size_t at) {
    return static_cast<std::size_t>(tribe) * kTribeRecordSize + at;
}
constexpr std::size_t kMusketLots = 73; // lots of 50 muskets kept in Europe

// Colony record.
constexpr std::size_t kNeeds = 0x1B;
constexpr std::size_t kMarks = 0x1C;
constexpr std::size_t kThreatLevel = 0x1E;
constexpr std::size_t kWorkTools = 0x8C; // turns of tools for the colony's own land work
constexpr std::size_t kWantedGood = 0x8D;
constexpr std::size_t kGarrison = 0x8E;
constexpr std::size_t kPickupBonus = 0x8F;

// Bits of the needs byte (the lowest two are the census's: ships near).
constexpr std::uint8_t kTooManyGuards = 0x04;
constexpr std::uint8_t kWantsGuards = 0x08;
constexpr std::uint8_t kWantsPeople = 0x10;
constexpr std::uint8_t kForestToClear = 0x20;
constexpr std::uint8_t kGuardsWanted = 0x40;
constexpr std::uint8_t kWantsPioneer = 0x80;

// The roles a unit has in the colony's lists (its type, as the original
// groups them): -1 for those that are not people.
constexpr int kRoleColonist = 0x13;
constexpr int kRolePioneer = 0x14;
constexpr int kRoleSoldier = 0x15;
constexpr int kRoleScout = 0x16;
constexpr int kRoleDragoon = 0x17;
constexpr std::array<int, 10> kRoles{kRoleColonist, kRoleSoldier, kRolePioneer, 0x18, kRoleDragoon,
                                     kRoleScout,    -1,           kRoleDragoon, -1,   kRoleSoldier};

int roleOf(int type) {
    return type >= 0 && type < static_cast<int>(kRoles.size()) ? kRoles[static_cast<std::size_t>(type)] : -1;
}

// The 20 squares around a colony at distance 1 and 2, in the original's
// order.
constexpr std::array<int, 20> kRingX{0, 1, 0, -1, -1, 1, 1, -1, 0, 2, 0, -2, -1, 1, -1, 1, -2, -2, 2, 2};
constexpr std::array<int, 20> kRingY{-1, 0, 1, 0, -1, -1, 1, 1, -2, 0, 2, 0, -2, -2, 2, 2, -1, 1, -1, 1};

bool shipType(int type) {
    return type >= kFirstShip && type <= kLastShip;
}

int signedByte(std::uint8_t b) {
    return static_cast<std::int8_t>(b);
}

// The original's kind of terrain: the terrain byte's kind, hills and
// mountains apart.
int terrainKind(const MapSquare& sq) {
    if (sq.mountains()) {
        return 27;
    }
    if (sq.hills()) {
        return 28;
    }
    return sq.terrain;
}

// Built, by row of @BUILDING (one bit each).
bool hasBuilding(const SaveColony& c, int row) {
    return row >= 0 && row < 48 && ((c.buildings >> row) & 1) != 0;
}

int distance(int dx, int dy) {
    const int ax = std::abs(dx);
    const int ay = std::abs(dy);
    return std::max(ax, ay) + std::min(ax, ay) / 2;
}

int alarmFor(const SaveSettlement& s, int nation) {
    if (nation < 0 || nation >= kEuropeanNations) {
        return 0;
    }
    const auto at = static_cast<std::size_t>(10 + 2 * nation);
    return static_cast<std::int16_t>(s.raw[at] | (s.raw[at + 1] << 8));
}

} // namespace

void Game::aiColonyPlan(int c) {
    auto colony = [&]() -> SaveColony& { return save_.colonies[static_cast<std::size_t>(c)]; };
    const int n = colony().nation;
    if (n < 0 || n >= kEuropeanNations) {
        return;
    }
    const auto un = static_cast<std::size_t>(n);
    const auto& map = save_.map;
    const auto& block = save_.tribesAndMore;
    const bool blockOk = block.size() >= kBlockEnd;
    const bool independence = (save_.header[18] & 0x01) != 0;
    auto& nation = save_.nations[un];
    const int X = colony().x;
    const int Y = colony().y;
    const auto isHuman = [&](int k) {
        return k >= 0 && k < kEuropeanNations && save_.players[static_cast<std::size_t>(k)].control == 0;
    };
    const auto bumpByte = [](std::uint8_t& b) {
        if (signedByte(b) < 0x7F) {
            b = static_cast<std::uint8_t>(b + 1);
        }
    };
    // The people units on the colony's square, in the order of the square.
    const auto peopleHere = [&]() {
        std::vector<int> list;
        for (const int i : unitsAt(X, Y)) {
            if (roleOf(unit(i).type) >= 0) {
                list.push_back(i);
            }
        }
        return list;
    };

    colony().raw[0x8A] = 0;
    colony().raw[0x8B] = 0;
    bumpByte(colony().raw[kWorkTools]);
    bumpByte(colony().raw[kPickupBonus]);
    bool toolsOk = colony().stock[cargo::kTools] >= 20;
    const int REG = landRegion(map, X, Y);
    const int status = REG < 0 ? 0 : ai_.status[un][static_cast<std::size_t>(REG)];
    const int tories =
        independence ? 0 : ((100 - sonsOfLibertyPercent(colony())) * colony().population + 50) / 100;
    (void)tories; // weighed by the colony's jobs (still to come)
    // The squares it counts: 8, or 12 and 20 with a larger town hall.
    const int halls = std::min(2, static_cast<int>((colony().buildings >> 10) & 1) +
                                      static_cast<int>(((colony().buildings >> 11) & 1)));
    const int squares = halls == 0 ? 8 : halls == 1 ? 12 : 20;
    int tribe = -1;
    {
        int best = INT32_MAX;
        for (const auto& s : save_.settlements) {
            if (REG >= 0 && landRegion(map, s.x, s.y) != REG) {
                continue;
            }
            const int d = distance(s.x - X, s.y - Y);
            if (d <= best) {
                best = d;
                tribe = tribeOf(s);
            }
        }
    }

    // The threat: armed units of others within five squares, by their
    // force and nearness (half on a town square). Natives count only when
    // tense and alarmed; the player's units half as much again.
    int threat = 0;
    int adjacent = 0;
    for (int dy = -5; dy <= 5; ++dy) {
        for (int dx = -5; dx <= 5; ++dx) {
            const int x = X + dx;
            const int y = Y + dy;
            if (!map.inside(x, y)) {
                continue;
            }
            const auto here = unitsAt(x, y);
            if (here.empty() || unit(here.front()).nation == n) {
                continue;
            }
            const bool town = colonyAt(x, y).has_value() || settlementAt(x, y).has_value();
            for (const int i : here) {
                const auto& u = unit(i);
                if (shipType(u.type)) {
                    continue;
                }
                int f = unitForce(u, true);
                if (u.nation >= kEuropeanNations) {
                    if (tribeTension(save_, u.nation - kEuropeanNations, n) < 25) {
                        f = 0;
                    }
                    const int home = signedByte(u.raw[6]);
                    if (home < 0 || home >= static_cast<int>(save_.settlements.size()) ||
                        alarmFor(save_.settlements[static_cast<std::size_t>(home)], n) < 0x80) {
                        f = 0;
                    }
                } else {
                    if (f <= 1) {
                        f = 0;
                    }
                    if (isHuman(u.nation)) {
                        f += f / 2;
                    }
                    if (f != 0 && std::abs(dx) <= 1 && std::abs(dy) <= 1) {
                        ++adjacent;
                    }
                }
                if (town) {
                    f >>= 1;
                }
                threat += f * (8 - distance(dx, dy)) >> 3;
            }
        }
    }
    const int cap = std::min(threat, 16);
    int level = threat / (buildingLevel(colony(), BuildingKind::Fortification) + 1);
    level = std::max(level, cap);
    colony().raw[kThreatLevel] = static_cast<std::uint8_t>(level >> 3);
    const int peopleUnits = static_cast<int>(peopleHere().size());
    int want = std::min(level >> 3, (colony().population + peopleUnits) >> 1);
    if (independence) {
        ++want;
    }
    if (adjacent != 0 && colony().population + peopleUnits > 1) {
        want = std::max(want, 1);
    }
    colony().raw[kGarrison] = static_cast<std::uint8_t>(want);

    // The armed units already there count against it; units without a home
    // colony take this one.
    for (const int i : unitsAt(X, Y)) {
        auto& u = save_.units[static_cast<std::size_t>(i)];
        if (shipType(u.type)) {
            continue;
        }
        if (signedByte(u.raw[6]) < 0) {
            u.raw[6] = static_cast<std::uint8_t>(c);
        }
        if (rules_.units[static_cast<std::size_t>(u.type)].attack > 1 && want != 0) {
            --want;
        }
    }
    int guards = 0; // armed units that call it home
    for (const auto& u : save_.units) {
        if (u.nation == n && signedByte(u.raw[6]) == c && !shipType(u.type) &&
            rules_.units[static_cast<std::size_t>(u.type)].attack > 1) {
            ++guards;
        }
    }
    guards += want - colony().raw[kGarrison];

    // The land around.
    int poorSquares = 0;  // off the map, water or poor farmland
    int waterSquares = 0; // off the map or water (none with docks)
    int forests = 0;
    int goodForests = 0;
    int goodFarms = 0;
    int unimproved = 0;
    int unplowed = 0;
    for (int k = 0; k < squares; ++k) {
        const int x = X + kRingX[static_cast<std::size_t>(k)];
        const int y = Y + kRingY[static_cast<std::size_t>(k)];
        if (!map.inside(x, y)) {
            ++waterSquares;
            ++poorSquares;
            continue;
        }
        const auto& sq = map.at(x, y);
        const int kind = terrainKind(sq);
        const auto food = [&](int row) {
            return row >= 0 && row < static_cast<int>(rules_.terrain.size())
                       ? rules_.terrain[static_cast<std::size_t>(row)].yield[0]
                       : 0;
        };
        if (kind == 25 || kind == 26) {
            ++poorSquares;
            ++waterSquares;
            ++goodFarms;
        }
        if (kind >= 8 && kind < 24) {
            ++forests;
            ++poorSquares;
            if (food(kind & 7) >= 3) {
                ++goodForests;
            }
        } else if (food(terrainRow(sq)) >= 3) {
            ++goodFarms;
        } else if (food(terrainRow(sq)) < 2) {
            ++poorSquares;
        }
        if (k < 8 && colony().squares[static_cast<std::size_t>(k)] >= 0) {
            const bool improved = (sq.flags & (MapSquare::kHasColony | MapSquare::kRoad)) != 0;
            if (!improved) {
                ++unimproved;
            }
            if (kind <= 7 && (sq.flags & MapSquare::kPlowed) == 0) {
                ++unplowed;
            }
        }
    }
    if (hasBuilding(colony(), 6)) {
        waterSquares = 0; // docks
    }

    // What it is short of.
    auto& needs = colony().raw[kNeeds];
    needs &= 0x07;
    if ((colony().raw[kMarks] & 0x10) != 0 && colony().population < kMaxColonists) {
        needs |= kWantsPeople;
        colony().raw[kMarks] &= static_cast<std::uint8_t>(~0x10);
    }
    if (want > 0) {
        needs |= kGuardsWanted;
    }
    const int trait = blockOk ? signedByte(block[kTraits + 3 * un + 2]) : 0;
    int ratio = 0;
    {
        int num = ((colony().population + peopleUnits) * 3 >> 1) - trait - (save_.turn >> 7);
        int den = trait + 5;
        if (want != 0) {
            ++den;
        }
        if (status == 4) {
            --den;
        }
        if (status == 0) {
            num += 2;
        }
        if (status == 3) {
            ++num;
        }
        ratio = den != 0 ? num / den : 0;
    }
    const int regionFlags = blockOk && REG >= 0 ? block[kRegionFlags + static_cast<std::size_t>(REG)] : 0;
    if (regionFlags == 0 && status != 0) {
        ratio = 0;
    }
    if ((regionFlags & 0x01) != 0) {
        if ((regionFlags & 0x06) != 0 && n != kSpain) {
            ratio = std::min(ratio, 1);
        } else if (blockOk && REG >= 0 && tribe >= 0) {
            const int out = block[kRegionAttackOut + kRegions * un + static_cast<std::size_t>(REG)];
            if (out >= 2) {
                int force = 2 * (block[kAttack + 2 * un] | (block[kAttack + 2 * un + 1] << 8));
                int local = 4 * out;
                if (n == kSpain) {
                    force *= 2;
                    local *= 2;
                }
                const auto t = static_cast<std::size_t>(tribe);
                // A tribe small enough against the nation's force, and
                // weak in the region, is marked for war.
                if (block[kTribeSettlements + t] <= force &&
                    block[kTribeRegionDefence + kRegions * t + static_cast<std::size_t>(REG)] < local &&
                    (tribeTension(save_, tribe, n) > 25 || n == kSpain)) {
                    const int other = kEuropeanNations + tribe;
                    nation.raw[kRelations + static_cast<std::size_t>(other)] |= 0x02;
                    if (save_.tribesAndMore.size() >= tribeByte(tribe, kTribeRecordSize)) {
                        save_.tribesAndMore[tribeByte(tribe, kTribeRelations + un)] |= 0x02;
                    }
                }
            }
        }
    }
    if (ratio > guards) {
        needs |= kWantsGuards;
    }
    if (ratio + (ratio > 1 ? 1 : 0) < guards) {
        needs |= kTooManyGuards;
    }
    if (squares - 1 <= poorSquares && forests > 1) {
        needs |= kForestToClear | kWantsPioneer;
    }
    if (((colony().population + 3) >> 2) > goodFarms && goodForests != 0 && forests > 1) {
        needs |= kForestToClear | kWantsPioneer;
    }
    if (unimproved != 0 || unplowed != 0) {
        needs |= kWantsPioneer;
    }
    const int colonyNeed = aiColonyNeed(n);
    {
        const int target = halls == 0 ? 8 : halls == 1 ? 12 : 32;
        const int P = halls + 2;
        const int pop = colony().population;
        if (pop < kMaxColonists && target + 2 * P > pop && pop - 2 * P < squares - waterSquares) {
            needs |= kWantsPeople;
        }
    }

    // Units on the square go in, while it wants people.
    if (!peopleHere().empty() && (needs & kWantsPeople) != 0) {
        bool changed = true;
        while (changed) {
            changed = false;
            for (const int i : peopleHere()) {
                if (colony().population >= kMaxColonists) {
                    break;
                }
                const int role = roleOf(unit(i).type);
                bool join = false;
                if (role == kRoleSoldier || role == kRoleDragoon) {
                    join = (needs & kTooManyGuards) != 0;
                    if (join) {
                        needs &= static_cast<std::uint8_t>(~kTooManyGuards);
                        ++want;
                    }
                } else if (role == kRolePioneer) {
                    join = ((needs & kWantsPioneer) != 0 && !toolsOk) || status == 0;
                    if (join) {
                        toolsOk = true;
                    }
                } else if (role == kRoleScout) {
                    join = status == 0 || colony().stock[cargo::kHorses] < 52;
                } else if (role == kRoleColonist) {
                    join = true;
                }
                if (join) {
                    aiTakeIntoColony(i, c);
                    changed = true;
                    break;
                }
            }
        }
    }

    // One colonist goes out as a scout, pioneer or soldier.
    if (colony().population > 1) {
        int role = kRoleColonist;
        bool make = false;
        const int pop = colony().population;
        const int target = halls == 0 ? 8 : halls == 1 ? 12 : 32;
        if (colony().stock[cargo::kHorses] >= 102 && (pop >= 10 || target <= pop) && (needs & kWantsPeople) == 0) {
            role = kRoleScout;
            make = true;
        }
        bool crowded = false;
        if (status == 0 && pop > 10) {
            crowded = aiRandom(0, 3) == 0 && (needs & kWantsPeople) == 0;
        }
        const int pioneers = blockOk ? block[kByType + kTypesCounted * un + unit_type::kPioneer] : 0;
        if (crowded && pioneers == 0 && colonyNeed != 0 && colony().stock[cargo::kTools] >= 20) {
            role = kRolePioneer;
            make = true;
        }
        if (((needs & (kGuardsWanted | kWantsGuards)) != 0 || crowded) && colony().stock[cargo::kMuskets] >= 50) {
            role = colony().stock[cargo::kHorses] >= 52 ? kRoleDragoon : kRoleSoldier;
            make = true;
        }
        if (make) {
            const int wanted = role == kRoleDragoon ? kRoleSoldier : role;
            int best = -1;
            int pick = -1;
            int last = -1;
            for (int k = 0; k < pop; ++k) {
                const int skill = colony().skills[static_cast<std::size_t>(k)];
                last = skill;
                int s = 0;
                if (skill == wanted) {
                    s = 4;
                } else if (!isExpert(skill)) {
                    s = 1;
                } else if (wanted == kRoleSoldier && (needs & kGuardsWanted) == 0) {
                    s = -99;
                }
                if (skill == 0x19) {
                    ++s;
                }
                if (skill == 0x1A) {
                    s += 2;
                }
                if (skill == 0x1B) {
                    continue;
                }
                if (s >= best) {
                    best = s;
                    pick = k;
                }
            }
            if (pick >= 0) {
                // (As read: the skill tested is the last colonist's.)
                if (isExpert(last) && last != wanted) {
                    colony().skills[static_cast<std::size_t>(pick)] = 0x1C;
                }
                aiSendOut(c, pick, role);
            }
        }
    }

    // The goods it asks for.
    const auto production = colonyProduction(colony(), map, rules_, save_.difficulty);
    const auto ask = [&](int good, bool on) {
        auto& C = colony();
        if (C.stock[static_cast<std::size_t>(good)] >= storageLimit(C) ||
            production.produced[static_cast<std::size_t>(good)] != 0) {
            on = false;
        }
        if (on) {
            C.raw[kWantedGood] = static_cast<std::uint8_t>(good);
        } else if (signedByte(C.raw[kWantedGood]) == good) {
            C.raw[kWantedGood] = 0xFF;
        }
    };
    {
        auto& C = colony();
        const int muskets = C.stock[cargo::kMuskets];
        const int trait0 = blockOk ? signedByte(block[kTraits + 3 * un]) : 0;
        ask(cargo::kMuskets, (trait0 + 2) * 50 > muskets);
        const bool askedMuskets = signedByte(colony().raw[kWantedGood]) == cargo::kMuskets;
        ask(cargo::kTradeGoods, colony().stock[cargo::kTradeGoods] < 100 &&
                                    bidPrice(nation, cargo::kTradeGoods) <= 3 && (colony().raw[kMarks] & 0x20) != 0);
        ask(cargo::kHorses, colony().stock[cargo::kHorses] < 50);
        ask(cargo::kTools, !toolsOk && (needs & kWantsPioneer) != 0);
        {
            const auto& D = colony();
            const int asks = signedByte(D.raw[kWantedGood]);
            const bool few = D.stock[cargo::kMuskets] < 50;
            const bool on = (want > 0 && few) || (D.raw[kGarrison] == 1 && few && asks != cargo::kTools) ||
                            ((needs & kWantsGuards) != 0 && few && asks != cargo::kTools) ||
                            (askedMuskets && asks == cargo::kMuskets);
            ask(cargo::kMuskets, on);
        }
        // Spare muskets go back to Europe's store.
        if (want == 0 && colony().stock[cargo::kMuskets] >= 200 && nation.raw[kMusketLots] < 20) {
            ++nation.raw[kMusketLots];
            colony().stock[cargo::kMuskets] -= 50;
        }
    }
    // Tools for the colony's own work on the land, bought in Europe.
    if (((needs & kWantsPioneer) != 0 || save_.turn % 10 == 0) && !toolsOk) {
        const int cost = 20 * bidPrice(nation, cargo::kTools);
        if (nation.gold >= cost) {
            nation.gold -= cost;
            aiMarket(n, cargo::kTools, 20);
            colony().stock[cargo::kTools] += 20;
            toolsOk = true;
        }
    }

    // Two horses, bought for a ship or wagon train in port.
    if (colony().stock[cargo::kHorses] < 2 && save_.turn >= 40) {
        const auto here = unitsAt(X, Y);
        if (!here.empty() && rules_.units[static_cast<std::size_t>(unit(here.front()).type)].cargoSlots != 0 &&
            nation.gold >= 10) {
            nation.gold -= 10;
            colony().stock[cargo::kHorses] = 2;
        }
    }
}

void Game::aiTakeIntoColony(int i, int c) {
    // A unit on the square joins the colony: what it carried goes into the
    // store (50 muskets, 50 horses, its tools).
    auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    if (colony.population >= kMaxColonists) {
        return;
    }
    const auto& u = unit(i);
    const int role = roleOf(u.type);
    if (role == kRoleSoldier || role == kRoleDragoon) {
        colony.stock[cargo::kMuskets] += 50;
    }
    if (role == kRoleDragoon || role == kRoleScout) {
        colony.stock[cargo::kHorses] += 50;
    }
    if (role == kRolePioneer) {
        colony.stock[cargo::kTools] += u.tools;
    }
    aiJoinColony(i, c);
}

void Game::aiSendOut(int c, int colonist, int role) {
    // A colonist leaves as a unit of the role, taking its gear from the
    // store: 50 muskets, 50 horses, or the tools in twenties (at most 100).
    auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    const int skill = colony.skills[static_cast<std::size_t>(colonist)];
    const int type = role == kRoleSoldier   ? unit_type::kSoldier
                     : role == kRoleDragoon ? unit_type::kDragoon
                     : role == kRolePioneer ? unit_type::kPioneer
                     : role == kRoleScout   ? unit_type::kScout
                                            : unit_type::kColonist;
    const int tools = std::min(100, colony.stock[cargo::kTools] / 20 * 20);
    addColonist(colony.x, colony.y, colony.nation);
    auto& u = save_.units.back();
    u.type = type;
    u.profession = skill;
    const auto take = [&](int good, int amount) {
        auto& s = colony.stock[static_cast<std::size_t>(good)];
        s = std::max(0, s - amount);
    };
    if (role == kRoleSoldier || role == kRoleDragoon) {
        take(cargo::kMuskets, 50);
    }
    if (role == kRoleDragoon || role == kRoleScout) {
        take(cargo::kHorses, 50);
    }
    if (role == kRolePioneer) {
        u.tools = tools;
        take(cargo::kTools, tools);
    }
    removeColonist(colony, colonist);
    refreshUnitFlags();
}

} // namespace viceroy
