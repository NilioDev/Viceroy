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
#include "viceroy/game/resource.hpp"

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
constexpr std::size_t kTribeDefence = kExtra + 565; // each tribe's strength, from its count
constexpr std::size_t kTribeRegionDefence = kExtra + 589;
constexpr std::size_t kColonyCount = kExtra + 16;
constexpr std::size_t kPeople = kExtra + 28;
constexpr std::size_t kHoldsAll = kExtra + 36;
constexpr std::size_t kArmedShips = kExtra + 52;
constexpr std::size_t kRegionColonies = kExtra + 236;
constexpr std::size_t kCombat = kExtra + 32;        // defence of the units
constexpr std::size_t kRegionCombat = kExtra + 428; // the same by region
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

// A building's levels in the original's chain of rows: the next row up
// (-1 at the top) and the one it needs below (-1 at the bottom).
int nextRow(int row) {
    for (const auto& f : kBuildingFields) {
        if (row >= f.shift && row < f.shift + f.bits) {
            return row + 1 < f.shift + f.bits ? row + 1 : (row == 15 ? 16 : -1);
        }
    }
    return -1;
}

int rowBelow(int row) {
    if (row == 16) {
        return 15;
    }
    for (const auto& f : kBuildingFields) {
        if (row >= f.shift && row < f.shift + f.bits) {
            return row > f.shift ? row - 1 : -1;
        }
    }
    return -1;
}

// The levels built from this row up its chain.
int levelsFrom(const SaveColony& c, int row) {
    int n = 0;
    for (; row >= 0; row = nextRow(row)) {
        n += hasBuilding(c, row) ? 1 : 0;
    }
    return n;
}

int topRow(int row) {
    while (nextRow(row) >= 0) {
        row = nextRow(row);
    }
    return row;
}

// Each craft as the plans see it: its building, the good its people make
// and the raw good they use.
struct Craft {
    int building;
    int job;
    int raw;
};
constexpr std::array<Craft, 6> kCrafts{{
    {3, 15, cargo::kTools},  // armory: muskets from tools
    {39, 14, cargo::kOre},   // blacksmith: tools from ore
    {32, 12, cargo::kFurs},  // fur trader: coats from furs
    {27, 9, cargo::kSugar},  // distiller: rum from sugar
    {24, 10, cargo::kTobacco}, // tobacconist: cigars from tobacco
    {21, 11, cargo::kCotton},  // weaver: cloth from cotton
}};

// The value of a resource to the colony's work on the land, by the
// original's order of resources.
int resourceValue(Resource r) {
    switch (r) {
    case Resource::DepletedSilver:
        return 6;
    case Resource::Oasis:
        return 3;
    case Resource::Grain:
        return 4;
    case Resource::Cotton:
    case Resource::Tobacco:
    case Resource::Beaver:
    case Resource::Deer:
    case Resource::Timber:
    case Resource::HillsOre:
        return 6;
    case Resource::Sugar:
        return 7;
    case Resource::Minerals:
        return 4;
    case Resource::Fish:
        return 5;
    case Resource::Silver:
        return 12;
    case Resource::None:
        break;
    }
    return -1;
}

// Whose units, or else whose colony or settlement, are on the square.
int ownerOn(const SaveMap& map, int x, int y) {
    if (!map.inside(x, y)) {
        return -1;
    }
    const auto& sq = map.at(x, y);
    if ((sq.flags & (MapSquare::kHasUnit | MapSquare::kHasColony)) == 0) {
        return -1;
    }
    const int who = sq.region >> 4;
    return who == 0x0F ? -1 : who;
}

int alarmFor(const SaveSettlement& s, int nation) {
    if (nation < 0 || nation >= kEuropeanNations) {
        return 0;
    }
    const auto at = static_cast<std::size_t>(10 + 2 * nation);
    return static_cast<std::int16_t>(s.raw[at] | (s.raw[at + 1] << 8));
}


// The building row each job works in (the first level), -1 on the land.
constexpr std::array<int, 19> kJobBuilding{-1, -1, -1, -1, -1, -1, -1, -1, -1, 27,
                                           24, 21, 32, 35, 39, 3,  37, 9,  12};
// The craft that uses what a job on the land makes (sugar for the
// distiller and so on), and the raw good each craft uses.
constexpr std::array<int, 19> kFeeds{-1, 9, 10, 11, 12, -1, 14, -1, -1, 1, 2, 3, 4, -1, 6, -1, -1, -1, -1};
constexpr std::array<int, 19> kUses{-1, -1, -1, -1, -1, -1, -1, -1, -1, cargo::kSugar, cargo::kTobacco,
                                    cargo::kCotton, cargo::kFurs, -1, cargo::kOre, cargo::kTools, -1, -1, -1};
constexpr std::size_t kSchoolTurns = 0x8C; // shared with the land work's turns
constexpr int kSkillsCounted = 32;

// What a colony makes and needs of each good, as its job plans count them:
// made, needed, short of this turn's making, and short even with the store.
struct Balance {
    std::array<int, 20> made{};
    std::array<int, 20> need{};
    std::array<int, 20> deficit{};
    std::array<int, 20> shortfall{};
};

Balance balanceOf(const SaveColony& colony, const SaveMap& map, const Ruleset& rules, int difficulty,
                  const NationEffects& nation) {
    Balance b;
    const auto p = colonyProduction(colony, map, rules, difficulty, false, nation);
    for (std::size_t g = 0; g < p.produced.size(); ++g) {
        b.made[g] = p.produced[g];
    }
    const auto stock = [&](int g) {
        return g >= 0 && g < kGoodsKinds ? colony.stock[static_cast<std::size_t>(g)] : 0;
    };
    const int pop = colony.population;
    // Horses: two for each started herd of 50 (25 with stables), fed with
    // half the food left over, as far as the warehouse has room.
    const int horses = stock(cargo::kHorses);
    int herd = 0;
    if (horses >= 2) {
        const int size = hasBuilding(colony, 17) ? 25 : 50;
        herd = (horses + size - 1) / size * 2;
    }
    const int spare = std::max(0, b.made[cargo::kFood] - 2 * pop);
    const int fed = std::min((spare + 1) >> 1, herd);
    const int bred = std::min(std::max(0, storageLimit(colony) - horses), fed);
    b.made[cargo::kHorses] += herd;
    b.shortfall[cargo::kHorses] = herd - bred;
    const auto settle = [&](int g, int need) {
        int made = b.made[static_cast<std::size_t>(g)];
        if (g == cargo::kTools && b.shortfall[cargo::kOre] != 0) {
            made -= b.shortfall[cargo::kOre];
        }
        const auto ug = static_cast<std::size_t>(g);
        b.need[ug] = need;
        b.deficit[ug] = need > made ? need - made : 0;
        b.shortfall[ug] = stock(g) + made < need ? need - stock(g) - made : 0;
    };
    // A craft needs what it makes, two thirds of it in a factory; short in
    // a factory, it is short of the whole (or half as much again).
    const auto craft = [&](int raw, int product) {
        const int full = b.made[static_cast<std::size_t>(product)];
        int need = full;
        if (levelsFrom(colony, kJobBuilding[static_cast<std::size_t>(product)]) > 2) {
            need = full * 2 / 3;
        }
        settle(raw, need);
        auto& short_ = b.shortfall[static_cast<std::size_t>(raw)];
        if (short_ != 0 && full != need) {
            short_ = short_ == need ? full : short_ * 3 / 2;
        }
    };
    settle(cargo::kFood, 2 * pop + bred);
    settle(cargo::kLumber, b.made[cargo::kHammers]);
    craft(cargo::kOre, cargo::kTools);
    craft(cargo::kTobacco, cargo::kCigars);
    craft(cargo::kCotton, cargo::kCloth);
    craft(cargo::kFurs, cargo::kCoats);
    craft(cargo::kSugar, cargo::kRum);
    settle(cargo::kTools, b.made[cargo::kMuskets]);
    return b;
}

// What a square is worth to the work besides food: its best yield times
// the good's highest starting price.
int landWorth(const Ruleset& rules, const MapSquare& sq) {
    const auto row = static_cast<std::size_t>(terrainRow(sq));
    if (row >= rules.terrain.size()) {
        return 0;
    }
    int best = 0;
    for (std::size_t j = 1; j < kSquareWorkCount; ++j) {
        const int price = j < rules.cargo.size() ? rules.cargo[j].startPriceHigh : 0;
        best = std::max(best, rules.terrain[row].yield[j] * price);
    }
    return best;
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
    int ocean = 0;
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
            ++ocean;
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
                if (block[kTribeDefence + t] <= force &&
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

    // Its colonists by skill, before anyone comes or goes.
    AiColonyView view{REG, tribe, 0, adjacent, squares, 0, ocean, tories, {}};
    for (int k = 0; k < colony().population; ++k) {
        const int skill = colony().skills[static_cast<std::size_t>(k)];
        const int counted = isExpert(skill) ? skill : kRoleColonist;
        if (counted >= 0 && counted < static_cast<int>(view.skills.size())) {
            ++view.skills[static_cast<std::size_t>(counted)];
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
    const auto production =
        colonyProduction(colony(), map, rules_, save_.difficulty, false, nationEffects(save_, colony().nation));
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

    // Every seventh turn, a road towards a colony nearby; on other turns,
    // its own work on the land. Either uses 20 tools.
    bool worked = false;
    if (toolsOk && save_.turn % 7 == 0) {
        worked = aiRoadToColony(c);
    } else if ((needs & kWantsPioneer) != 0 && toolsOk) {
        worked = aiImproveLand(c, REG);
    }
    if (worked) {
        colony().stock[cargo::kTools] -= std::min(20, colony().stock[cargo::kTools]);
        colony().raw[kWorkTools] = 0;
    }

    // Who works where, then what it builds next, who learns at its school
    // and who is trained.
    view.guards = want;
    view.water = waterSquares;
    aiAssignJobs(c, view);
    aiChooseConstruction(c, view);
    aiTeach(c, view);
    aiTrain(c);

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

bool Game::aiRoadToColony(int c) {
    // A road square towards another of the nation's colonies within six
    // squares in the same land, along the route finder's way there.
    const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    const int n = colony.nation;
    const auto& map = save_.map;
    const int REG = landRegion(map, colony.x, colony.y);
    const auto& block = save_.tribesAndMore;
    if (REG < 0 || block.size() < kBlockEnd) {
        return false;
    }
    const int count = block[kRegionColonies + kRegions * static_cast<std::size_t>(n) + static_cast<std::size_t>(REG)];
    if (count < 2) {
        return false;
    }
    for (std::size_t o = 0; o < save_.colonies.size(); ++o) {
        const auto& other = save_.colonies[o];
        if (other.nation != n || static_cast<int>(o) == c || std::abs(other.x - colony.x) >= 7 ||
            std::abs(other.y - colony.y) >= 7 || landRegion(map, other.x, other.y) != REG ||
            aiRandom(0, count - 2) != 0) {
            continue;
        }
        // Walk the way there; the first square that is not a town and has
        // no road is the one.
        route_.nation = -1;
        route_.type = unit_type::kSoldier;
        route_.roads = true;
        route_.targetX = other.x;
        route_.targetY = other.y;
        int x = colony.x;
        int y = colony.y;
        bool found = false;
        for (int steps = 0; steps < 64; ++steps) {
            const int d = routeFlood(x, y, 99);
            if (d < 0 || d >= 8) {
                break;
            }
            x += kSteps[static_cast<std::size_t>(d)].dx;
            y += kSteps[static_cast<std::size_t>(d)].dy;
            if (x == other.x && y == other.y) {
                break;
            }
            if (!colonyAt(x, y) && (map.at(x, y).flags & (MapSquare::kHasColony | MapSquare::kRoad)) == 0) {
                found = true;
                break;
            }
        }
        route_.roads = false;
        if (!found) {
            continue;
        }
        const int owner = ownerOn(map, x, y);
        if ((owner >= 0 && owner != n) || (routeEnemyLand(x, y, n) >= 0)) {
            continue;
        }
        const auto row = static_cast<std::size_t>(terrainRow(map.at(x, y)));
        const int need = (row < rules_.terrain.size() ? rules_.terrain[row].improvementTurns : 0) + 2;
        if (need > colony.raw[kWorkTools]) {
            return false;
        }
        auto& sq = save_.map.at(x, y);
        sq.flags = static_cast<std::uint8_t>(sq.flags | MapSquare::kRoad);
        return true;
    }
    return false;
}

int Game::aiTribeClaim(const SaveColony& colony, int k, int REG) const {
    // The tribe whose land the square is: the nearest settlement's within
    // its reach (1, or 2 and 3 for the cities), once met; never a square
    // the colony works, water, land marked 16, or with Peter Minuit.
    const auto& map = save_.map;
    const int n = colony.nation;
    const int x = colony.x + kRingX[static_cast<std::size_t>(k)];
    const int y = colony.y + kRingY[static_cast<std::size_t>(k)];
    if (n < 0 || n >= kEuropeanNations || !map.inside(x, y) || k >= 8 ||
        colony.squares[static_cast<std::size_t>(k)] >= 0 || map.at(x, y).isWater() ||
        (map.at(x, y).flags & 0x10) != 0 || hasFather(n, 2)) {
        return -1;
    }
    const auto& nation = save_.nations[static_cast<std::size_t>(n)];
    int nearest = -1;
    int nearestDistance = INT32_MAX;
    for (std::size_t s = 0; s < save_.settlements.size(); ++s) {
        const auto& st = save_.settlements[s];
        if (REG >= 0 && landRegion(map, st.x, st.y) != REG) {
            continue;
        }
        const int d = distance(st.x - x, st.y - y);
        if (d <= nearestDistance) {
            nearestDistance = d;
            nearest = static_cast<int>(s);
        }
    }
    if (nearest < 0) {
        return -1;
    }
    const int tribe = tribeOf(save_.settlements[static_cast<std::size_t>(nearest)]);
    const int level = tribeLevel(save_, tribe);
    const int reach = level <= 1 ? 1 : level == 2 ? 2 : 3;
    if (nearestDistance > reach) {
        return -1;
    }
    const int other = kEuropeanNations + tribe;
    if ((nation.raw[kRelations + static_cast<std::size_t>(other)] & 0x20) == 0) {
        return -1;
    }
    return tribe;
}

bool Game::aiImproveLand(int c, int REG) {
    // The colony plows, clears or builds a road on its best square itself,
    // as a pioneer would in a turn, when it has saved up enough turns.
    const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    const int n = colony.nation;
    const auto& map = save_.map;
    const auto& block = save_.tribesAndMore;
    const auto& nation = save_.nations[static_cast<std::size_t>(n)];
    const std::uint8_t needs = colony.raw[kNeeds];
    const bool clearForest = (needs & kForestToClear) != 0;
    const std::uint8_t regionFlags =
        block.size() >= kBlockEnd && REG >= 0 ? block[kRegionFlags + static_cast<std::size_t>(REG)] : 0;
    const auto jobAt = [&](int k) {
        const int w = k < 8 ? colony.squares[static_cast<std::size_t>(k)] : -1;
        return w >= 0 && w < colony.population ? static_cast<int>(colony.jobs[static_cast<std::size_t>(w)]) : -1;
    };
    // A square is free when no town stands on it, nobody else's units are
    // there and no other colony works it.
    const auto free = [&](int x, int y) {
        if (colonyAt(x, y) || settlementAt(x, y)) {
            return false;
        }
        const int owner = ownerOn(map, x, y);
        if (owner >= 0 && owner != n) {
            return false;
        }
        for (const auto& other : save_.colonies) {
            if (&other == &colony || std::abs(other.x - x) > 1 || std::abs(other.y - y) > 1) {
                continue;
            }
            for (std::size_t s = 0; s < 8; ++s) {
                if (other.squares[s] >= 0 && other.x + kRingX[s] == x && other.y + kRingY[s] == y) {
                    return false;
                }
            }
        }
        return true;
    };
    int best = -1;
    int bestValue = -1;
    for (int k = 0; k < 8; ++k) {
        const int x = colony.x + kRingX[static_cast<std::size_t>(k)];
        const int y = colony.y + kRingY[static_cast<std::size_t>(k)];
        if (!map.inside(x, y) || map.at(x, y).isWater() || !free(x, y)) {
            continue;
        }
        const auto& sq = map.at(x, y);
        const int kind = terrainKind(sq);
        const auto row = static_cast<std::size_t>(terrainRow(sq));
        int value = row < rules_.terrain.size() ? rules_.terrain[row].value : 0;
        // (The original's resource test gives none at all with a seed of 0.)
        const Resource res = map.resourceSeed != 0 ? resourceAt(map, x, y) : Resource::None;
        if (resourceValue(res) >= 0) {
            value = resourceValue(res);
        }
        const bool roadOrTown = (sq.flags & (MapSquare::kHasColony | MapSquare::kRoad)) != 0;
        const bool plowed = (sq.flags & MapSquare::kPlowed) != 0;
        if (clearForest && kind >= 8 && kind < 24) {
            value *= 2;
        } else {
            const int job = jobAt(k);
            if (job >= 0 && (job < 4 ? !plowed : !roadOrTown)) {
                value *= 2;
            }
            if (roadOrTown && plowed) {
                continue;
            }
        }
        // Land a tribe claims costs it goodwill.
        const int claim = aiTribeClaim(colony, k, REG);
        if (claim >= 0) {
            int cost = 4 - tribeTension(save_, claim, n);
            if (resourceValue(res) >= 0) {
                cost *= 2;
            }
            if ((regionFlags & 0x01) != 0 && (colony.raw[kMarks] & 0x20) == 0) {
                if (!clearForest) {
                    continue;
                }
                cost *= 2;
            }
            value -= nation.gold < 2000 ? 2 * cost : cost >> 1;
        }
        if (value > bestValue) {
            bestValue = value;
            best = k;
        }
    }
    if (best < 0) {
        return false;
    }
    const int x = colony.x + kRingX[static_cast<std::size_t>(best)];
    const int y = colony.y + kRingY[static_cast<std::size_t>(best)];
    // Not with the player's units next to it, unless the nation's own
    // stand on the square.
    bool ok = true;
    for (const auto& s : kSteps) {
        const int who = ownerOn(map, x + s.dx, y + s.dy);
        if (who >= 0 && who < kEuropeanNations && save_.players[static_cast<std::size_t>(who)].control == 0) {
            ok = false;
        }
    }
    if (ownerOn(map, x, y) == n) {
        ok = true;
    }
    if (!ok) {
        return false;
    }
    auto& sq = save_.map.at(x, y);
    const int kind = terrainKind(sq);
    const bool clear = clearForest && kind >= 8 && kind < 24;
    const auto row = static_cast<std::size_t>(terrainRow(sq));
    const int need = (row < rules_.terrain.size() ? rules_.terrain[row].improvementTurns : 0) + 2 + (clear ? 2 : 0);
    if (colony.raw[kWorkTools] < need) {
        return false;
    }
    const auto plow = [&] {
        if (sq.isForest()) {
            sq.terrain = static_cast<std::uint8_t>(sq.terrain - 8);
        } else {
            sq.flags = static_cast<std::uint8_t>(sq.flags | MapSquare::kPlowed);
        }
        return true;
    };
    const auto road = [&] {
        sq.flags = static_cast<std::uint8_t>(sq.flags | MapSquare::kRoad);
        return true;
    };
    if (clear) {
        return plow();
    }
    const int job = jobAt(best);
    const bool plowed = (sq.flags & MapSquare::kPlowed) != 0;
    const bool roadOrTown = (sq.flags & (MapSquare::kHasColony | MapSquare::kRoad)) != 0;
    if (job >= 0 && job < 4 && !plowed) {
        return plow();
    }
    if (job >= 4 && !roadOrTown) {
        return road();
    }
    if (kind >= 2 && kind <= 7) {
        return !plowed && plow();
    }
    return !roadOrTown && road();
}

bool Game::aiCanBuild(int c, int row) const {
    // As the colony screen offers it, with the founding fathers some rows
    // need.
    const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    if (row < 0 || row >= 42 || hasBuilding(colony, row)) {
        return false;
    }
    const int below = rowBelow(row);
    if (below >= 0 && !hasBuilding(colony, below)) {
        return false;
    }
    if (static_cast<std::size_t>(row) >= rules_.buildings.size() ||
        rules_.buildings[static_cast<std::size_t>(row)].minColonySize > colony.population) {
        return false;
    }
    switch (row) {
    case 10:
    case 11:
    case 30:
        return false;
    case 6: { // docks: water among its squares
        bool water = false;
        for (std::size_t k = 0; k < 8; ++k) {
            const int x = colony.x + kRingX[k];
            const int y = colony.y + kRingY[k];
            water = water || (save_.map.inside(x, y) && save_.map.at(x, y).isWater());
        }
        return water;
    }
    case 7: // drydock: by the sea
        return (colony.raw[kMarks] & 0x40) != 0;
    case 16:
        return storageLimit(colony) < 300;
    case 18: // custom house: Peter Stuyvesant
        return hasFather(colony.nation, 3);
    case 5:
    case 23:
    case 26:
    case 29:
    case 34:
    case 41: // factories: Adam Smith
        return hasFather(colony.nation, 0);
    default:
        return true;
    }
}

bool Game::aiTryBuild(int c, int row) {
    // True to go on down the list: the row is built already, or neither
    // it nor what it needs can be built. False once a choice is made.
    auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    if (row < 0 || hasBuilding(colony, row)) {
        return true;
    }
    bool goOn = false;
    if (aiCanBuild(c, row)) {
        colony.construction = row;
    } else if (aiTryBuild(c, rowBelow(row))) {
        goOn = true;
    }
    if (!goOn) {
        save_.colonies[static_cast<std::size_t>(c)].raw[kMarks] &= 0x7F;
    }
    return goOn;
}

void Game::aiChooseConstruction(int c, const AiColonyView& view) {
    auto colony = [&]() -> SaveColony& { return save_.colonies[static_cast<std::size_t>(c)]; };
    const int n = colony().nation;
    const auto un = static_cast<std::size_t>(n);
    const auto& block = save_.tribesAndMore;
    const bool blockOk = block.size() >= kBlockEnd;
    const auto& nation = save_.nations[un];
    const int pop = colony().population;
    const auto jobCount = [&](int job) {
        int count = 0;
        for (int k = 0; k < pop; ++k) {
            count += colony().jobs[static_cast<std::size_t>(k)] == job ? 1 : 0;
        }
        return count;
    };
    const int foodWorkers = jobCount(0) + jobCount(8);
    colony().raw[0x1D] &= 0x7F;
    colony().construction = -1;
    const auto unitChoice = [&](int type) {
        colony().raw[kMarks] &= 0x7F;
        colony().construction = type + 0x1F;
    };
    const auto mark = [&] { colony().raw[0x1D] |= 0x80; };

    // Its experts, and the highest teaching level among them.
    int experts = 0;
    int teach = 0;
    {
        std::vector<int> skills;
        for (int k = 0; k < pop; ++k) {
            skills.push_back(colony().skills[static_cast<std::size_t>(k)]);
        }
        for (const int i : unitsAt(colony().x, colony().y)) {
            if (roleOf(unit(i).type) >= 0) {
                skills.push_back(unit(i).profession);
            }
        }
        for (const int s : skills) {
            if (!isExpert(s)) {
                continue;
            }
            ++experts;
            if (s >= 0 && static_cast<std::size_t>(s) < rules_.jobs.size()) {
                teach = std::max(teach, rules_.jobs[static_cast<std::size_t>(s)].skillLevel % 4);
            }
        }
    }
    const auto production =
        colonyProduction(colony(), save_.map, rules_, save_.difficulty, false, nationEffects(save_, colony().nation));
    const auto made = [&](int good) { return production.produced[static_cast<std::size_t>(good)]; };
    const bool starving =
        balanceOf(colony(), save_.map, rules_, save_.difficulty, nationEffects(save_, colony().nation))
            .shortfall[cargo::kFood] != 0;
    const bool hungry = ((pop >> 1) < foodWorkers && foodWorkers > 1) || starving;

    if (view.squares - view.water <= pop || (view.water != 0 && hungry)) {
        if (!aiTryBuild(c, 6)) {
            return;
        }
    }
    if (!aiTryBuild(c, 0)) {
        return;
    }
    if (colony().stock[cargo::kHorses] >= 2 && !aiTryBuild(c, 17)) {
        return;
    }
    if (pop < 4) {
        mark();
        return;
    }
    const int sixths = pop / 6;
    if (sixths > colony().raw[149] && colony().raw[149] < 1 && !aiTryBuild(c, 16)) {
        return;
    }
    const int armed = blockOk ? block[kArmedShips + un] : 0;
    const int humanArmed = blockOk && human() >= 0 && human() < kEuropeanNations
                               ? block[kArmedShips + static_cast<std::size_t>(human())]
                               : 0;
    if (pop >= 6 && ((colony().raw[kNeeds] & 0x03) != 0 || humanArmed - 2 > armed || pop >= 12) &&
        !aiTryBuild(c, 18)) {
        return;
    }
    const std::uint8_t regionFlags =
        blockOk && view.region >= 0 ? block[kRegionFlags + static_cast<std::size_t>(view.region)] : 0;
    if ((colony().raw[kMarks] & 0x20) == 0 && save_.year < 1600 && (regionFlags & 0x01) != 0 && view.tribe >= 0 &&
        tribeTension(save_, view.tribe, n) < 50 && view.adjacent == 0) {
        unitChoice(unit_type::kWagonTrain);
        return;
    }
    if (teach >= 1 && pop + experts >= 4 && !aiTryBuild(c, 12)) {
        return;
    }
    if (pop < 6) {
        mark();
    }
    const int musketPrice = bidPrice(nation, cargo::kMuskets);
    if (((musketPrice + save_.difficulty / 2 >= 4 || save_.turn > 80) && pop >= 6 &&
        (colony().stock[cargo::kTools] >= 40 || made(cargo::kTools) != 0)) ||
        view.skills[job::kGunsmith] != 0) {
        if (!aiTryBuild(c, 3)) {
            return;
        }
    }
    // A firebrand preacher wants a church.
    if (view.skills[job::kPreacher] != 0 && !aiTryBuild(c, 37)) {
        return;
    }
    if (!aiTryBuild(c, 36) || !aiTryBuild(c, 1)) {
        return;
    }
    if (bidPrice(nation, cargo::kMuskets) >= 4 && pop >= 4 &&
        (colony().stock[cargo::kOre] >= 40 || made(cargo::kOre) != 0) && !aiTryBuild(c, 40)) {
        return;
    }
    if (colony().raw[149] < sixths && !aiTryBuild(c, 16)) {
        return;
    }
    if (made(cargo::kBells) >= 4 && !aiTryBuild(c, 20)) {
        return;
    }
    if (teach >= 2 && pop + experts >= 10 && !aiTryBuild(c, 13)) {
        return;
    }
    if (pop < 8) {
        mark();
        return;
    }
    if (teach >= 3 && pop + experts >= 16 && !aiTryBuild(c, 14)) {
        return;
    }
    if (!aiTryBuild(c, 37)) {
        return;
    }
    if (pop >= 10 && !aiTryBuild(c, 2)) {
        return;
    }
    bool craftAtTop = false;
    for (const auto& craft : kCrafts) {
        craftAtTop = craftAtTop || levelsFrom(colony(), craft.building) == 3;
    }
    const auto byType = [&](int type) {
        return blockOk ? static_cast<int>(block[kByType + kTypesCounted * un + static_cast<std::size_t>(type)]) : 0;
    };
    const auto ships = [&]() {
        // A shipyard builds galleons while the nation's holds fall short,
        // else privateers and a frigate.
        const int people = blockOk ? block[kPeople + un] : 0;
        const int colonies = blockOk ? block[kColonyCount + un] : 0;
        const int holds = blockOk ? block[kHoldsAll + un] : 0;
        if ((people >> 1) + colonies >= holds) {
            unitChoice(15);
            return true;
        }
        if (!(byType(17) == 0 && armed != 0) && armed < 4) {
            unitChoice(16);
            return true;
        }
        if (byType(17) < 1) {
            unitChoice(17);
            return true;
        }
        return false;
    };
    if (craftAtTop && (colony().raw[kMarks] & 0x40) != 0) {
        if (!aiTryBuild(c, 8)) {
            return;
        }
        if (hasBuilding(colony(), 8) && ships()) {
            return;
        }
    }
    int artillery = 0;
    for (const int i : unitsAt(colony().x, colony().y)) {
        artillery += unit(i).type == 11 ? 1 : 0;
    }
    if (craftAtTop && (artillery == 0 || view.guards == 0)) {
        if (colony().stock[cargo::kTools] != 0 && !aiTryBuild(c, 3)) {
            return;
        }
        unitChoice(11);
        return;
    }
    for (int k = static_cast<int>(kCrafts.size()) - 1; k >= 0; --k) {
        const auto& craft = kCrafts[static_cast<std::size_t>(k)];
        // A craft busy enough for the next level of its building.
        const int level = levelsFrom(colony(), craft.building);
        int need = 0;
        if (made(craft.raw) >= 3) {
            need = 2;
        }
        if (made(craft.raw) >= 8 || colony().stock[static_cast<std::size_t>(craft.raw)] >= 100) {
            need = 3;
        }
        if (level < need && !aiTryBuild(c, topRow(craft.building))) {
            return;
        }
    }
    if (!aiTryBuild(c, 38)) {
        return;
    }
    if (hasBuilding(colony(), 8)) {
        if (byType(15) < std::min(armed, 8)) {
            unitChoice(15);
            return;
        }
        if (armed < 8) {
            unitChoice(17);
            return;
        }
    }
    if (pop < 10) {
        colony().raw[kMarks] |= 0x10;
    }
    if (artillery < 3) {
        if (!aiTryBuild(c, 3)) {
            return;
        }
        if (made(cargo::kMuskets) == 0) {
            unitChoice(11);
            return;
        }
        if (!aiTryBuild(c, 5)) {
            return;
        }
        unitChoice(11);
        return;
    }
    colony().construction = -1;
    mark();
}

int Game::aiRank(int nation) const {
    // Each nation's score: its gold in hundreds, twice its colonies, its
    // people and its attack force; sorted weakest first.
    const auto& block = save_.tribesAndMore;
    const bool blockOk = block.size() >= kBlockEnd;
    std::array<int, kEuropeanNations> key{};
    for (int i = 0; i < kEuropeanNations; ++i) {
        const auto ui = static_cast<std::size_t>(i);
        key[ui] = save_.nations[ui].gold / 100;
        if (blockOk) {
            key[ui] += 2 * block[kColonyCount + ui] + block[kPeople + ui] +
                       static_cast<std::int16_t>(block[kAttack + 2 * ui] | (block[kAttack + 2 * ui + 1] << 8));
        }
    }
    // The original's sort: take the first that is smaller than the one
    // before it and put it before the first not smaller, until none is.
    std::vector<int> order{0, 1, 2, 3};
    for (;;) {
        std::size_t at = 0;
        for (std::size_t i = 1; i < order.size(); ++i) {
            if (key[static_cast<std::size_t>(order[i])] < key[static_cast<std::size_t>(order[i - 1])]) {
                at = i;
                break;
            }
        }
        if (at == 0) {
            break;
        }
        const int v = order[at];
        order.erase(order.begin() + static_cast<std::ptrdiff_t>(at));
        std::size_t to = 0;
        while (key[static_cast<std::size_t>(order[to])] < key[static_cast<std::size_t>(v)]) {
            ++to;
        }
        order.insert(order.begin() + static_cast<std::ptrdiff_t>(to), v);
    }
    if (nation < 0 || nation >= kEuropeanNations) {
        nation = human();
    }
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (order[i] == nation) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

void Game::aiAssignJobs(int c, AiColonyView& view) {
    // Every colonist leaves their work, and the colony gives the jobs out
    // again: food first, then lumber, carpenters, the experts at their own
    // work, and the rest where they are worth most.
    auto& C = save_.colonies[static_cast<std::size_t>(c)];
    const int n = C.nation;
    const auto un = static_cast<std::size_t>(n);
    const auto& map = save_.map;
    const auto& block = save_.tribesAndMore;
    const bool blockOk = block.size() >= kBlockEnd;
    auto& nation = save_.nations[un];
    const int pop = std::min(C.population, kMaxColonists);
    const int A0 = std::min(view.squares, 8);
    const int storage = storageLimit(C);
    const int REG = view.region;
    const WorkModifier modifier{std::clamp(C.libertyBonus, 0, 2), 0};
    const auto stock = [&](int g) { return g >= 0 && g < kGoodsKinds ? C.stock[static_cast<std::size_t>(g)] : 0; };
    const auto balance = [&] { return balanceOf(C, map, rules_, save_.difficulty, nationEffects(save_, C.nation)); };
    const int rankHere = aiRank(n);
    const int rankHuman = aiRank(human());

    // Armed units on the colony's square soften a tribe's claim.
    int armed = 0;
    for (const int i : unitsAt(C.x, C.y)) {
        const int type = unit(i).type;
        if (type >= 0 && static_cast<std::size_t>(type) < rules_.units.size() &&
            rules_.units[static_cast<std::size_t>(type)].combat > 1) {
            ++armed;
        }
    }

    // The squares: whether the colony may work each, and whose land it is.
    struct Square {
        bool usable = false;
        int claim = -1;
        int dx = 0;
        int dy = 0;
        bool forest = false;
        int worth = 0;
    };
    std::array<Square, 8> sq{};
    for (int s = 0; s < A0; ++s) {
        auto& q = sq[static_cast<std::size_t>(s)];
        q.dx = kRingX[static_cast<std::size_t>(s)];
        q.dy = kRingY[static_cast<std::size_t>(s)];
        const int x = C.x + q.dx;
        const int y = C.y + q.dy;
        q.claim = aiTribeClaim(C, s, REG);
        if (!map.inside(x, y)) {
            continue;
        }
        const auto& m = map.at(x, y);
        const int kind = terrainKind(m);
        q.forest = kind >= 8 && kind < 24;
        q.worth = landWorth(rules_, m);
    }

    // Everyone leaves their work (the schoolhouse stands for "no work").
    std::array<int, kMaxColonists> skill{};
    std::array<bool, kMaxColonists> done{};
    std::array<int, kSkillsCounted> given{}; // jobs given out in the buildings
    for (int k = 0; k < pop; ++k) {
        const auto uk = static_cast<std::size_t>(k);
        skill[uk] = C.skills[uk];
        // Its turns at the same work start again.
        auto& turns = C.raw[0x60 + uk / 2];
        turns = static_cast<std::uint8_t>((k & 1) != 0 ? (turns & 0x0F) : (turns & 0xF0));
        C.jobs[uk] = job::kTeacher;
    }
    C.squares.fill(-1);
    for (int s = 0; s < A0; ++s) {
        auto& q = sq[static_cast<std::size_t>(s)];
        const int x = C.x + q.dx;
        const int y = C.y + q.dy;
        if (!map.inside(x, y)) {
            continue;
        }
        q.usable = squareFree(C, map, save_.colonies, s) && !settlementAt(x, y).has_value();
        // Not where a tribe's armed units stand fortified.
        for (const int i : unitsAt(x, y)) {
            const auto& u = unit(i);
            if (u.nation >= kEuropeanNations && (u.orders == SaveUnit::kFortify || u.orders == SaveUnit::kFortified) &&
                static_cast<std::size_t>(u.type) < rules_.units.size() &&
                rules_.units[static_cast<std::size_t>(u.type)].attack > 1) {
                q.usable = false;
            }
        }
    }
    std::array<bool, 19> buildingThere{};
    for (std::size_t j = 0; j < buildingThere.size(); ++j) {
        buildingThere[j] = kJobBuilding[j] < 0 || hasBuilding(C, kJobBuilding[j]);
    }
    Balance bal = balance();
    bool late = false; // the plans past the food

    // Puts colonist k to work on the land: at job `mode`, at the best job
    // (-1), or only weighs where it would go (-2).
    struct Place {
        bool placed = false;
        int amount = 0;
        int score = 0;
    };
    const auto place = [&](int k, int mode) {
        const auto uk = static_cast<std::size_t>(k);
        const int old = C.jobs[uk];
        C.jobs[uk] = job::kTeacher;
        bal = balance();
        const int food = stock(cargo::kFood);
        bool foodShort = bal.need[cargo::kFood] > bal.made[cargo::kFood];
        if (late || storage < food || 16 * bal.deficit[cargo::kFood] < food) {
            foodShort = false;
        }
        const int lumber = stock(cargo::kLumber);
        const bool noLumber = lumber <= 10 && bal.made[cargo::kLumber] == 0;
        int best = 0;
        int bestAmount = 0;
        int bestJob = -1;
        int bestSquare = -1;
        for (int s = 0; s < A0; ++s) {
            const auto& q = sq[static_cast<std::size_t>(s)];
            if (!q.usable || C.squares[static_cast<std::size_t>(s)] >= 0) {
                continue;
            }
            const int x = C.x + q.dx;
            const int y = C.y + q.dy;
            const auto& m = map.at(x, y);
            C.squares[static_cast<std::size_t>(s)] = k;
            for (int j = 0; j < static_cast<int>(kSquareWorkCount); ++j) {
                if (mode >= 0 && j != mode) {
                    continue;
                }
                const auto uj = static_cast<std::size_t>(j);
                C.jobs[uk] = static_cast<std::uint8_t>(j);
                const int amount = squareYield(rules_, m, j, C.skills[uk], modifier, landAround(map, x, y) >= 3,
                                               resourceAt(map, x, y));
                // (As read: the job's own number picks the good, so a
                // fisherman's room is the horses'.)
                int score = amount;
                if (mode < 0) {
                    score = std::min(amount, std::max(1, storage - stock(j)));
                }
                score *= 8;
                if (score == 0) {
                    continue;
                }
                score += 7 - std::abs(q.dx) - std::abs(q.dy);
                const bool foodJob = j == job::kFarmer || j == job::kFisherman;
                if (mode < 0 && foodShort) {
                    if (foodJob) {
                        if (j == job::kFisherman) {
                            score += 8;
                        }
                        score <<= 5;
                        int worth = q.worth;
                        if (q.forest && noLumber) {
                            worth += 24;
                        }
                        score = std::max(1, score - worth);
                    }
                    if (q.claim >= 0) {
                        score >>= 1;
                    }
                } else if (mode < 0) {
                    int price = 0;
                    if (foodJob) {
                        price = pop < 2 * A0 && late ? 4 : 0;
                    } else {
                        price = bidPrice(nation, j);
                        if (j == 6 && pop >= 8 && save_.turn >= 80) { // ore miners
                            price += 2;
                            if (rankHere >= rankHuman) {
                                price += levelsFrom(C, 40) + 2 * levelsFrom(C, 3);
                            }
                        }
                    }
                    int weight = price + 1;
                    if (bal.deficit[uj] != 0 || bal.shortfall[uj] != 0) {
                        ++weight;
                        if (bal.shortfall[uj] != 0) {
                            score *= 2;
                        }
                    } else if (j == 5 && lumber + bal.made[cargo::kLumber] >= 2) {
                        --weight;
                    }
                    // Work whose crop a craft uses counts the craft's levels.
                    const int feeds = kFeeds[uj];
                    if (feeds >= 0) {
                        weight += nextRow(kJobBuilding[static_cast<std::size_t>(feeds)]) >= 0 ? 2 : 1;
                    }
                    if (q.claim >= 0) {
                        const auto o = static_cast<std::size_t>(q.claim);
                        int cost = 4 - tribeTension(save_, q.claim, n);
                        if (blockOk && REG >= 0) {
                            const auto r = static_cast<std::size_t>(REG);
                            if (block[kTribeRegionDefence + kRegions * o + r] >
                                block[kRegionCombat + kRegions * un + r]) {
                                cost *= 2;
                            }
                        }
                        if (blockOk && block[kTribeDefence + o] > block[kCombat + un]) {
                            cost = cost * 3 / 2;
                        }
                        weight -= std::max(0, cost - armed);
                    }
                    score = (std::max(0, weight) + price) * score;
                    if (j != 5 && q.forest && noLumber) {
                        score -= 10;
                    }
                }
                if (score > best) {
                    best = score;
                    bestJob = j;
                    bestAmount = amount;
                    bestSquare = s;
                }
            }
            C.squares[static_cast<std::size_t>(s)] = -1;
        }
        Place r;
        r.placed = bestJob >= 0;
        r.score = best;
        if (bestJob >= 0 && mode > -2) {
            C.squares[static_cast<std::size_t>(bestSquare)] = k;
            C.jobs[uk] = static_cast<std::uint8_t>(bestJob);
            r.amount = bestAmount;
        } else {
            C.jobs[uk] = static_cast<std::uint8_t>(old);
        }
        return r;
    };
    const auto unplace = [&](int k) {
        for (auto& w : C.squares) {
            if (w == k) {
                w = -1;
            }
        }
        C.jobs[static_cast<std::size_t>(k)] = job::kTeacher;
    };
    const auto setJob = [&](int k, int j) {
        C.jobs[static_cast<std::size_t>(k)] = static_cast<std::uint8_t>(j);
        if (j < static_cast<int>(kSquareWorkCount)) {
            return;
        }
        for (auto& w : C.squares) {
            if (w == k) {
                w = -1;
            }
        }
    };
    const auto finish = [&](int k) {
        done[static_cast<std::size_t>(k)] = true;
        bal = balance();
    };

    // Food: farmers and fishermen by skill first, while the colony is small
    // or has horses to feed; then anyone who is not an expert.
    const bool small = pop < 2 * A0;
    const int horses = stock(cargo::kHorses);
    const bool herd = horses > 1 && horses < storage;
    const bool wantFood = small || herd;
    bool skipFood = !wantFood && stock(cargo::kFood) >= 75;
    if (!skipFood) {
        bool out = false;
        for (int k = 0; k < pop && !out; ++k) {
            if (!small && bal.made[cargo::kFood] > bal.need[cargo::kFood] && !herd) {
                break;
            }
            const auto uk = static_cast<std::size_t>(k);
            if (done[uk] || !(skill[uk] == job::kFarmer || (skill[uk] == job::kFisherman && hasBuilding(C, 6)))) {
                continue;
            }
            const auto r = place(k, skill[uk]);
            if (!r.placed) {
                continue;
            }
            if (r.amount >= 3) {
                finish(k);
                continue;
            }
            unplace(k);
            out = true;
        }
        skipFood = out;
    }
    if (!skipFood) {
        bool out = false;
        for (int pass = 0; pass < 2 && !out; ++pass) {
            for (int k = 0; k < pop; ++k) {
                const int food = stock(cargo::kFood);
                if (!((bal.deficit[cargo::kFood] != 0 && food <= 16 * bal.deficit[cargo::kFood]) || wantFood)) {
                    break;
                }
                const auto uk = static_cast<std::size_t>(k);
                if (done[uk]) {
                    continue;
                }
                const bool plenty = food > 16 * bal.deficit[cargo::kFood];
                if (isExpert(skill[uk]) && (plenty || pass == 0)) {
                    continue;
                }
                if (plenty && pass == 0 && skill[uk] != skill::kConvert) {
                    continue;
                }
                const auto r = place(k, -1);
                if (!r.placed) {
                    continue;
                }
                if (r.amount < 3 || (plenty && r.amount < 5)) {
                    unplace(k);
                    out = true;
                    break;
                }
                finish(k);
            }
        }
    }
    late = true;

    // (As read: this step weighs everyone with the "only weigh" mode, which
    // leaves the amount at nothing, so it never puts anyone to work.)
    bool lumberjack = false;
    const int halls =
        std::min(2, static_cast<int>((C.buildings >> 10) & 1) + static_cast<int>((C.buildings >> 11) & 1));
    const int target = halls == 0 ? 8 : halls == 1 ? 12 : 32;
    if (!(view.water != 0 && view.squares - view.water <= pop) && target > pop) {
        int best = -1;
        int pick = -1;
        for (int k = 0; k < pop; ++k) {
            const auto uk = static_cast<std::size_t>(k);
            if (done[uk]) {
                continue;
            }
            const auto r = place(k, -2);
            if (!r.placed) {
                continue;
            }
            const int s = 4 * r.score + (isExpert(skill[uk]) ? 0 : 1) + (skill[uk] == 0 ? 2 : 0);
            if (s > best && r.amount >= 2) {
                best = s;
                pick = k;
            }
        }
        if (pick >= 0 && place(pick, -1).placed) {
            finish(pick);
            lumberjack = C.jobs[static_cast<std::size_t>(pick)] == 5;
        }
    }

    // Lumber and carpenters, unless the colony builds little.
    bool lumberTaken = false; // an expert lumberjack may still go out
    if ((C.raw[0x1D] & 0x80) == 0) {
        lumberTaken = true;
        if (!lumberjack && stock(cargo::kLumber) < 10) {
            for (int pass = 0; pass < 3; ++pass) {
                for (int k = 0; k < pop; ++k) {
                    if (bal.made[cargo::kLumber] != 0) {
                        break;
                    }
                    const auto uk = static_cast<std::size_t>(k);
                    if (done[uk] || (pass == 0 && skill[uk] != 5) || (pass == 1 && isExpert(skill[uk]))) {
                        continue;
                    }
                    if (place(k, 5).placed) {
                        lumberjack = true;
                        finish(k);
                    }
                }
            }
        }
        // Short of lumber: 100 bought every eighth turn, at 200 gold.
        if (!lumberjack && stock(cargo::kLumber) < 2 && (save_.turn & 7) == 0) {
            C.stock[cargo::kLumber] += 100;
            if (nation.gold >= 200) {
                nation.gold -= 200;
            }
        }
        if (stock(cargo::kLumber) + bal.made[cargo::kLumber] >= 2) {
            for (int pass = 0; pass < 4; ++pass) {
                for (int k = 0; k < pop; ++k) {
                    if (bal.made[cargo::kHammers] != 0) {
                        break;
                    }
                    const auto uk = static_cast<std::size_t>(k);
                    if (done[uk]) {
                        continue;
                    }
                    const int sk = skill[uk];
                    if ((pass == 0 && sk != job::kCarpenter) || (pass == 1 && sk != skill::kFreeColonist) ||
                        (pass == 2 && sk != skill::kServant)) {
                        continue;
                    }
                    if (sk == skill::kCriminal || sk == skill::kServant) {
                        C.skills[uk] = skill::kFreeColonist;
                    }
                    // Now and then a colonist learns the trade outright.
                    if (!isExpert(sk) && view.skills[job::kCarpenter] == 0 && pop >= 6 &&
                        aiRandom(0, 16 - save_.difficulty) == 0) {
                        C.skills[uk] = job::kCarpenter;
                    }
                    setJob(k, job::kCarpenter);
                    finish(k);
                }
            }
        }
    }

    // Experts on the land at their own work, while the store has room.
    for (int k = 0; k < pop; ++k) {
        const auto uk = static_cast<std::size_t>(k);
        const int sk = skill[uk];
        if (done[uk] || !isExpert(sk) || sk >= 9 || sk == job::kFarmer || sk == job::kFisherman) {
            continue;
        }
        if (sk == 5) {
            if (!lumberTaken) {
                lumberTaken = true;
            } else if ((C.raw[kNeeds] & kForestToClear) == 0 && bal.shortfall[cargo::kLumber] == 0) {
                continue;
            }
        }
        if (stock(sk) > storage) {
            continue;
        }
        if (place(k, sk).placed) {
            finish(k);
        }
    }

    // Experts in the buildings at their own trade.
    const bool independence = (save_.header[18] & 0x01) != 0;
    for (int k = 0; k < pop; ++k) {
        const auto uk = static_cast<std::size_t>(k);
        const int sk = skill[uk];
        if (done[uk] || !isExpert(sk) || sk < 9 || sk >= 19 || !buildingThere[static_cast<std::size_t>(sk)] ||
            given[static_cast<std::size_t>(sk)] >= 3) {
            continue;
        }
        bool go = false;
        switch (sk) {
        case 9:
        case 10:
        case 11:
        case 12:
        case 14:
        case 15: {
            const int raw = kUses[static_cast<std::size_t>(sk)];
            go = bal.shortfall[static_cast<std::size_t>(raw)] == 0 &&
                 (stock(raw) > 0 || bal.made[static_cast<std::size_t>(raw)] != 0);
            break;
        }
        case job::kCarpenter:
            if ((C.raw[0x1D] & 0x80) != 0 && view.skills[job::kCarpenter] > 1) {
                // A colony that builds little needs fewer master carpenters.
                C.skills[uk] = skill::kFreeColonist;
                skill[uk] = skill::kFreeColonist;
                --view.skills[job::kCarpenter];
                continue;
            }
            go = bal.shortfall[cargo::kLumber] == 0 && stock(cargo::kLumber) + bal.made[cargo::kLumber] != 0;
            break;
        case job::kPreacher:
            go = !independence;
            if (independence) {
                C.skills[uk] = skill::kFreeColonist;
                skill[uk] = skill::kFreeColonist;
                --view.skills[job::kPreacher];
                continue;
            }
            break;
        case job::kStatesman:
            go = !independence;
            break;
        default:
            break;
        }
        if (go) {
            setJob(k, sk);
            ++given[static_cast<std::size_t>(sk)];
            finish(k);
        }
    }

    // The rest: inside where it is worth more than the best work on the
    // land, else on the land, else carpenter (or preacher).
    int available = 0; // (as read: kept from the last job that uses a good)
    for (int k = 0; k < pop; ++k) {
        const auto uk = static_cast<std::size_t>(k);
        if (done[uk]) {
            continue;
        }
        const auto outside = place(k, -2);
        int best = 0;
        int pick = job::kCarpenter;
        for (int j = 9; j < 19; ++j) {
            const auto uj = static_cast<std::size_t>(j);
            if (!buildingThere[uj] || j == job::kTeacher || given[uj] >= 3) {
                continue;
            }
            setJob(k, j);
            int uses = kFeeds[uj];
            if (j == job::kGunsmith) {
                uses = cargo::kTools;
            }
            if (j == job::kCarpenter) {
                uses = cargo::kLumber;
            }
            if (uses >= 0) {
                const auto ui = static_cast<std::size_t>(uses);
                available = stock(uses) - bal.need[ui] + bal.made[ui];
                if (available < 0) {
                    continue;
                }
                if (available == 0) {
                    available = 1;
                }
            }
            const auto building = buildingForJob(j);
            const int level = building ? std::max(1, buildingLevel(C, *building)) : 1;
            const int good = jobOutput(j);
            const int amount = std::min(good >= 0 ? buildingYield(level, C.skills[uk], j, modifier) : 0, available);
            int worth = 3;
            if (good >= 0 && good < 16) {
                worth = bidPrice(nation, good);
                if (good != cargo::kTools && good != cargo::kMuskets) {
                    worth -= bidPrice(nation, good - 8);
                } else {
                    worth += 4;
                    if (save_.turn >= 50 && rankHuman <= rankHere) {
                        worth *= 2;
                    }
                }
            } else if (good >= 16) {
                const auto ug = static_cast<std::size_t>(good);
                if (j == job::kStatesman) {
                    worth = 4 * levelsFrom(C, 19) + view.tories + 7 + 4 * C.raw[0x96];
                    if (view.tories >= 10) {
                        worth *= 2;
                    }
                    if (hasFather(n, 15)) {
                        worth *= 2;
                    }
                    if (save_.year < 1540) {
                        worth = 0;
                    }
                    if (save_.year > 1600) {
                        worth *= 2;
                    }
                    if (save_.year > 1700) {
                        worth *= 2;
                    }
                    if (independence) {
                        worth = 0;
                    }
                    if (pop <= 3) {
                        worth >>= 1;
                    }
                    if (pop < 6) {
                        worth >>= 1;
                    }
                    if (rankHuman < rankHere) {
                        worth >>= 1;
                    }
                    if (rankHuman > rankHere) {
                        worth *= 2;
                    }
                    if ((nation.raw[0] & 0x04) != 0) {
                        worth >>= 1;
                    }
                    worth = std::clamp(worth - bal.made[ug], 1, 100);
                }
                if (j == job::kCarpenter) {
                    worth = 5 - bal.made[ug] / 3;
                    if ((C.raw[0x1D] & 0x80) != 0) {
                        worth >>= 1;
                    }
                    worth = std::max(1, worth);
                }
                if (j == job::kPreacher) {
                    worth = std::max(1, worth - (bal.made[ug] / 2 + save_.turn / 100 - 6));
                }
            }
            const int score = (8 * amount + 5) * worth;
            if (score > best) {
                best = score;
                pick = j;
            }
        }
        setJob(k, job::kTeacher);
        if (outside.score >= best) {
            if (place(k, -1).amount == 0) {
                const bool preach = hasBuilding(C, 37) &&
                                    stock(cargo::kLumber) + bal.made[cargo::kLumber] > bal.need[cargo::kLumber] &&
                                    given[job::kPreacher] < 3;
                pick = preach ? job::kPreacher : job::kCarpenter;
                setJob(k, pick);
                ++given[static_cast<std::size_t>(pick)];
            }
        } else {
            setJob(k, pick);
            ++given[static_cast<std::size_t>(pick)];
        }
        bal = balance();
    }
}

void Game::aiTeach(int c, const AiColonyView& view) {
    // A schoolhouse teaches someone every so many turns: the colony's
    // food workers' trade while it is small and short of food experts,
    // else (as read) the number of the last craft whose raw good it makes
    // and has no master of, as a skill; else the job they do.
    auto& C = save_.colonies[static_cast<std::size_t>(c)];
    const int level = levelsFrom(C, 12);
    if (level == 0) {
        return;
    }
    const int turns = 4 * (level + (level == 3 ? 1 : 0));
    if (turns > C.raw[kSchoolTurns]) {
        return;
    }
    const int pop = std::min(C.population, kMaxColonists);
    int foodWorkers = 0;
    int foodExperts = 0;
    for (int k = 0; k < pop; ++k) {
        const auto uk = static_cast<std::size_t>(k);
        foodWorkers += C.jobs[uk] == job::kFarmer || C.jobs[uk] == job::kFisherman ? 1 : 0;
        foodExperts += C.skills[uk] == job::kFarmer || C.skills[uk] == job::kFisherman ? 1 : 0;
    }
    int what = -1;
    if (pop < 10 && foodExperts <= foodWorkers) {
        int fishers = 0;
        for (int k = 0; k < pop; ++k) {
            fishers += C.skills[static_cast<std::size_t>(k)] == job::kFisherman ? 1 : 0;
        }
        what = fishers < view.ocean ? job::kFisherman : job::kFarmer;
    }
    const Balance bal = balanceOf(C, save_.map, rules_, save_.difficulty, nationEffects(save_, C.nation));
    for (std::size_t k = 0; k < kCrafts.size(); ++k) {
        const auto& craft = kCrafts[k];
        const int needed = craft.building == 3 ? 1 : 2;
        if (needed <= levelsFrom(C, craft.building) && bal.made[static_cast<std::size_t>(craft.raw)] != 0 &&
            view.skills[static_cast<std::size_t>(craft.job)] == 0) {
            what = static_cast<int>(k);
        }
    }
    // Who may learn: anyone who is not already an expert at their work,
    // converts excepted; the units on the square too.
    std::vector<int> people;
    const auto here = [&] {
        std::vector<int> list;
        for (const int i : unitsAt(C.x, C.y)) {
            if (roleOf(unit(i).type) >= 0) {
                list.push_back(i);
            }
        }
        return list;
    }();
    const int total = pop + static_cast<int>(here.size());
    for (int k = 0; k < total; ++k) {
        const bool colonist = k < pop;
        const int role = colonist ? C.jobs[static_cast<std::size_t>(k)]
                                  : roleOf(unit(here[static_cast<std::size_t>(k - pop)]).type);
        const int sk = colonist ? C.skills[static_cast<std::size_t>(k)]
                                : unit(here[static_cast<std::size_t>(k - pop)]).profession;
        if ((isExpert(sk) && (sk == role || role == kRoleColonist)) || sk == skill::kConvert ||
            static_cast<int>(people.size()) >= 25) {
            continue;
        }
        people.push_back(k);
    }
    if (people.empty()) {
        return;
    }
    const int k = people[static_cast<std::size_t>(aiRandom(0, static_cast<int>(people.size()) - 1))];
    int learnt = what;
    if (learnt < 0) {
        learnt = k < pop ? static_cast<int>(C.jobs[static_cast<std::size_t>(k)])
                         : roleOf(unit(here[static_cast<std::size_t>(k - pop)]).type);
    }
    if (learnt == kRoleDragoon) {
        learnt = kRoleSoldier;
    }
    if (k < pop) {
        C.skills[static_cast<std::size_t>(k)] = static_cast<std::uint8_t>(learnt);
    } else {
        save_.units[static_cast<std::size_t>(here[static_cast<std::size_t>(k - pop)])].profession = learnt;
    }
    C.raw[kSchoolTurns] = 0;
}

void Game::aiTrain(int c) {
    // A colony short of food, in a nation with low taxes and gold to spare,
    // has its last colonist who is no expert trained as a fisherman (with
    // docks and none yet) or a farmer (with none yet).
    auto& C = save_.colonies[static_cast<std::size_t>(c)];
    const int n = C.nation;
    auto& nation = save_.nations[static_cast<std::size_t>(n)];
    const int pop = std::min(C.population, kMaxColonists);
    int foodWorkers = 0;
    for (int k = 0; k < pop; ++k) {
        const int j = C.jobs[static_cast<std::size_t>(k)];
        foodWorkers += j == job::kFarmer || j == job::kFisherman ? 1 : 0;
    }
    const Balance bal = balanceOf(C, save_.map, rules_, save_.difficulty, nationEffects(save_, C.nation));
    const bool hungry = ((pop >> 1) < foodWorkers && foodWorkers > 1) || bal.shortfall[cargo::kFood] != 0;
    const auto price = [&](int row) {
        return row >= 0 && static_cast<std::size_t>(row) < rules_.jobs.size()
                   ? rules_.jobs[static_cast<std::size_t>(row)].europeCost
                   : 0;
    };
    if (!hungry || nation.taxRate > 25 || nation.gold < price(job::kFarmer)) {
        return;
    }
    int pick = -1;
    bool farmer = false;
    bool fisher = false;
    for (int k = 0; k < pop; ++k) {
        const int sk = C.skills[static_cast<std::size_t>(k)];
        farmer = farmer || sk == job::kFarmer;
        fisher = fisher || sk == job::kFisherman;
        if (!isExpert(sk) && sk != skill::kConvert) {
            pick = k;
        }
    }
    if (pick < 0) {
        return;
    }
    int learnt = -1;
    if (!fisher && hasBuilding(C, 6)) {
        learnt = job::kFisherman;
    } else if (!farmer) {
        learnt = job::kFarmer;
    }
    if (learnt < 0) {
        return;
    }
    C.skills[static_cast<std::size_t>(pick)] = static_cast<std::uint8_t>(learnt);
    // (As read: the price is the one listed at the colonist's own number,
    // so an expert that cannot be bought, -1, gives a gold piece back.)
    nation.gold -= price(pick);
}

} // namespace viceroy
