// SPDX-License-Identifier: GPL-3.0-or-later
//
// How a native unit chooses its step: it scores the eight squares around it
// and staying put, and takes the best. Read from the original with a
// debugger and checked against its answers; see docs/rules/natives.md
// ("How the braves move").
#include "viceroy/game/colony.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"
#include "viceroy/game/resource.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace viceroy {

namespace {

constexpr int kStay = 8;
// The original's directions: north, then clockwise; the ninth is staying.
constexpr std::array<int, 9> kDx{0, 1, 1, 1, 0, -1, -1, -1, 0};
constexpr std::array<int, 9> kDy{-1, -1, 0, 1, 1, 1, 0, -1, 0};

constexpr int kBrave = 19;
constexpr int kMountedBrave = 21;
constexpr int kFirstShip = 13;
constexpr int kLastShip = 18;
constexpr int kStepLimit = 20; // steps a unit may take in one turn

// Unit types, rows of @UNIT.
constexpr int kColonist = 0;
constexpr int kSoldier = 1;
constexpr int kPioneer = 2;
constexpr int kDragoon = 4;
constexpr int kScout = 5;
constexpr int kTreasure = 10;
constexpr int kArtillery = 11;
constexpr int kWagonTrain = 12;

// In each tribe's record.
constexpr std::size_t kMuskets = 7;
constexpr std::size_t kHorses = 10;  // 16 bits
constexpr std::size_t kWarning = 46; // 16 bits per nation: 1 attacked this turn, 2 met this turn
constexpr std::size_t kRelations = 58;
constexpr std::uint8_t kMet = 0x20;
constexpr int kHorsesForMount = 25;

// In each unit's record.
constexpr std::size_t kUnitFlags = 4;
constexpr std::uint8_t kVisitDue = 0x08;
constexpr std::size_t kHome = 6;
constexpr std::size_t kDestX = 9;
constexpr std::size_t kDestY = 10;
constexpr std::size_t kLastStep = 11;
constexpr std::size_t kStamp = 18; // 16 bits: the turn of its last visit

// The flags of a choice.
constexpr int kForeign = 0x01;  // someone else's unit or town is on the square
constexpr int kTaken = 0x02;    // ... and it is in the way
constexpr int kAtWar = 0x04;    // the tribe is at 75 or more against them
constexpr int kAttack = 0x08;   // the step is an attack
constexpr int kLaden = 0x10;    // a wagon train with goods
constexpr int kAngry = 0x40;    // the settlement or tribe is angry with them
constexpr int kVisiting = 0x80; // next to someone it may visit

int word(const std::uint8_t* p) {
    return static_cast<std::int16_t>(p[0] | (p[1] << 8));
}

void setWord(std::uint8_t* p, int value) {
    p[0] = static_cast<std::uint8_t>(value & 0xFF);
    p[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

// The original's distance: the longer side plus half the shorter.
int distance(int dx, int dy) {
    dx = std::abs(dx);
    dy = std::abs(dy);
    return std::max(dx, dy) + std::min(dx, dy) / 2;
}

int alarmOf(const SaveSettlement& s, int nation) {
    if (nation < 0 || nation >= kEuropeanNations) {
        return 0;
    }
    const auto at = static_cast<std::size_t>(10 + 2 * nation);
    return static_cast<std::int16_t>(s.raw[at] | (s.raw[at + 1] << 8));
}

bool withMuskets(int type) {
    return type == 1 || type == 4 || type == 11 || type == 20 || type == 22;
}

bool mounted(int type) {
    return type == 4 || type == 5 || type == 21 || type == 22;
}

bool isShipType(int type) {
    return type >= kFirstShip && type <= kLastShip;
}

// Whether two directions are neighbours on the compass.
bool nextTo(int a, int b) {
    return ((a + 1) & 7) == b || ((a + 7) & 7) == b;
}

} // namespace

int Game::nationPeople(int nation) const {
    // The original's count of a nation's people: its colonists on land or
    // aboard (not ships, artillery, wagon trains, treasure or the Crown's
    // troops) and everyone in its colonies, at most 255.
    int people = 0;
    for (const auto& u : save_.units) {
        const int t = u.type;
        if (u.nation == nation && ((t >= 0 && t <= 5) || t == 7 || t == 9)) {
            ++people;
        }
    }
    for (const auto& c : save_.colonies) {
        if (c.nation == nation) {
            people += c.population;
        }
    }
    return std::min(people, 255);
}

int Game::nativeUnitAI(int index) {
    auto& u = save_.units[static_cast<std::size_t>(index)];
    const auto& map = save_.map;
    const int own = u.nation;
    const int tribe = own - kEuropeanNations;
    auto& block = save_.tribesAndMore;
    if (tribe < 0 || tribe >= kTribes || block.size() < static_cast<std::size_t>(kTribes) * kTribeRecordSize) {
        return kStay;
    }
    std::uint8_t* t = block.data() + static_cast<std::size_t>(tribe) * kTribeRecordSize;
    const int turn = save_.turn;
    const int clock = turn - turn / 25;
    const int sinceVisit = clock - static_cast<std::int16_t>(u.raw[kStamp] | (u.raw[kStamp + 1] << 8));

    // A unit without a home is gone.
    const int homeIndex = u.raw[kHome] == 0xFF ? -1 : u.raw[kHome];
    if (homeIndex < 0 || homeIndex >= static_cast<int>(save_.settlements.size())) {
        removeUnit(index);
        return -1;
    }
    const auto& home = save_.settlements[static_cast<std::size_t>(homeIndex)];
    const int region = landRegion(map, u.x, u.y);
    // A home on other land counts as none for the pull home.
    const int homeHere = landRegion(map, home.x, home.y) == region ? homeIndex : -1;

    // As the original reads a square: the high four bits of its path
    // layer hold the nation that last came onto it (see refreshUnitFlags);
    // with the mask they tell whose unit or town is there.
    const auto visitor = [&](int x, int y) -> int {
        if (!map.inside(x, y)) {
            return -1;
        }
        const int who = map.at(x, y).region >> 4;
        return who == 0x0F ? -1 : who;
    };
    const auto townOwner = [&](int x, int y) -> int {
        return map.inside(x, y) && (map.at(x, y).flags & MapSquare::kHasColony) != 0 ? visitor(x, y) : -1;
    };
    const auto unitOwner = [&](int x, int y) -> int {
        return map.inside(x, y) && (map.at(x, y).flags & MapSquare::kHasUnit) != 0 ? visitor(x, y) : -1;
    };
    const auto occupant = visitor;
    const auto isWater = [&](int x, int y) { return !map.inside(x, y) || map.at(x, y).isWater(); };
    // The first neighbour (in the original's order) with a unit or town of
    // someone else on the same kind of square; `fromTown` lets a town on
    // the square itself see across water, as the original's second check
    // does.
    const auto foreignNext = [&](int x, int y, bool townsCountAcross) -> int {
        const bool water = isWater(x, y);
        const bool hereTown = townOwner(x, y) >= 0;
        for (int d = 0; d < 8; ++d) {
            const int nx = x + kDx[static_cast<std::size_t>(d)];
            const int ny = y + kDy[static_cast<std::size_t>(d)];
            if (!map.inside(nx, ny)) {
                continue;
            }
            const int town = townOwner(nx, ny);
            const bool sameKind = isWater(nx, ny) == water;
            if (townsCountAcross) {
                const int who = town >= 0 ? town : unitOwner(nx, ny);
                if (who >= 0 && who != own && (town >= 0 || hereTown || sameKind)) {
                    return who;
                }
            } else {
                int who = -1;
                const int units = unitOwner(nx, ny);
                if (units >= 0 && units != own && sameKind) {
                    who = units;
                }
                if (town >= 0 && town != own && sameKind) {
                    who = town;
                }
                if (who >= 0) {
                    return who;
                }
            }
        }
        return -1;
    };
    const auto tension = [&](int nation) { return tribeTension(save_, tribe, nation); };
    const auto relation = [&](int nation) { return t[kRelations + static_cast<std::size_t>(nation)]; };
    const auto unitsOn = [&](int x, int y) { return static_cast<int>(unitsAt(x, y).size()); };

    // Someone else next to the unit (not when it stands in a town).
    const int besideNation = townOwner(u.x, u.y) >= 0 ? -1 : foreignNext(u.x, u.y, false);

    // The nearest colony on this land, if it is close to home for its size.
    const int colony = nearestColony(save_, u.x, u.y, -1, region);
    int nearHome = -1;
    if (colony >= 0) {
        const auto& c = save_.colonies[static_cast<std::size_t>(colony)];
        nearHome = distance(home.x - c.x, home.y - c.y);
        if (std::max(2, c.population >> 1) < nearHome) {
            nearHome = -1;
        }
    }

    // A threat at home: two or more armed men of one nation around it (a
    // colony's own people count against it).
    int threat = 0;
    int threatNation = -1;
    for (int d = 0; d < 8; ++d) {
        const int x = home.x + kDx[static_cast<std::size_t>(d)];
        const int y = home.y + kDy[static_cast<std::size_t>(d)];
        if (!map.inside(x, y)) {
            continue;
        }
        const int who = occupant(x, y);
        if (who < 0 || who >= kEuropeanNations || isWater(x, y)) {
            continue;
        }
        int armed = 0;
        for (const int other : unitsAt(x, y)) {
            const int type = unit(other).type;
            if (!isShipType(type) && type >= 0 && type < static_cast<int>(rules_.units.size()) &&
                rules_.units[static_cast<std::size_t>(type)].attack > 1) {
                ++armed;
            }
        }
        if (armed > 0) {
            if (const auto c = colonyAt(x, y)) {
                armed -= static_cast<std::int8_t>(save_.colonies[static_cast<std::size_t>(*c)].population) >> 2;
            }
        }
        if (armed > 0) {
            threat += armed;
            threatNation = who;
        }
    }
    if (threat < 2) {
        threat = 0;
        threatNation = -1;
    }

    // How many grudges: a tension of 75, or a home alarm of 128.
    int grudges = 0;
    for (int n = 0; n < kEuropeanNations; ++n) {
        grudges += tension(n) >= 75 ? 1 : 0;
        grudges += alarmOf(home, n) >= 0x80 ? 1 : 0;
    }

    const int level = tribeLevel(save_, tribe);
    const int full = movementAllowance(index);
    const int aloneHere = unitsOn(u.x, u.y) == 1 ? 1 : 0;
    const bool hereRoad = (map.at(u.x, u.y).flags & (MapSquare::kRoad | MapSquare::kHasColony)) != 0;
    const bool hereRiver = map.at(u.x, u.y).river();

    int best = -1;
    int bestDir = kStay;
    int bestFlags = 0;
    bool attackSeen = false;
    bool angry = false; // kept from one square to the next, as in the original
    bool war = false;
    int alarm = 0;
    for (int dir = 0; dir <= kStay; ++dir) {
        const int x = u.x + kDx[static_cast<std::size_t>(dir)];
        const int y = u.y + kDy[static_cast<std::size_t>(dir)];
        if (!map.inside(x, y)) {
            continue;
        }
        const auto& sq = map.at(x, y);
        if (sq.isWater() || rumourAt(map, x, y) || sq.terrain == 24) {
            continue;
        }
        int score = 200;
        int flags = 0;
        const int occ = occupant(x, y);
        const int units = unitOwner(x, y);
        const int town = townOwner(x, y);
        const bool road = (sq.flags & (MapSquare::kRoad | MapSquare::kHasColony)) != 0;
        const bool special = resourceAt(map, x, y) != Resource::None && town < 0;
        if (occ < 0 || occ == own) {
            war = false;
            angry = false;
            alarm = 0;
        } else {
            flags |= kForeign;
            alarm = alarmOf(home, occ);
            if (occ >= kEuropeanNations) {
                war = false;
            } else {
                angry = alarm >= 0x80;
                war = tension(occ) >= 75;
                if (war || occ == threatNation) {
                    angry = true;
                }
            }
            if (war) {
                flags |= kAtWar;
            }
            if (angry) {
                flags |= kAngry;
            }
        }
        int owner = units >= 0 ? units : town;
        if (owner == own) {
            owner = -1;
        }
        const bool taken = owner >= 0;
        if (taken) {
            flags |= kTaken;
        }

        // Someone to visit next to the square: a town, or a wagon train.
        int visitNation = -1;
        int visitScore = -1;
        if (!taken) {
            for (int d = 0; d < 8; ++d) {
                const int nx = x + kDx[static_cast<std::size_t>(d)];
                const int ny = y + kDy[static_cast<std::size_t>(d)];
                if ((nx == u.x && ny == u.y) || !map.inside(nx, ny)) {
                    continue;
                }
                int value = 100;
                int who = townOwner(nx, ny);
                if (who < 0) {
                    const auto here = unitsAt(nx, ny);
                    if (here.empty() || unit(here.front()).nation == own || unit(here.front()).type != kWagonTrain) {
                        continue;
                    }
                    who = unit(here.front()).nation;
                    value >>= 1;
                }
                if (who < 0 || who >= kEuropeanNations) {
                    continue;
                }
                value += tension(who) / 2;
                if (value >= visitScore) {
                    visitScore = value;
                    visitNation = who;
                }
            }
        }
        const int fromHome = distance(x - home.x, y - home.y);
        if (visitNation >= 0) {
            alarm = alarmOf(home, visitNation);
            angry = alarm >= 0x80;
            war = tension(visitNation) >= 75;
            if (!angry && !war && sinceVisit < 2 * fromHome + 5) {
                continue;
            }
            score += visitScore + 2 * sinceVisit;
            if (!war) {
                flags |= kVisiting;
            }
        }
        if (!taken && visitNation < 0 && nearHome >= 0) {
            const auto& c = save_.colonies[static_cast<std::size_t>(colony)];
            if ((nationPeople(c.nation) >> 3) + 2 * nearHome + 5 <= sinceVisit) {
                const int d = distance(c.x - x, c.y - y);
                if (d < 12) {
                    score += (12 - d) * 10;
                }
            }
        }

        if (taken) {
            if (occ >= kEuropeanNations) {
                continue;
            }
            const int tense = std::min(tension(occ), 100);
            bool strike = false;
            bool prize = false;
            bool treasure = false;
            if (units >= 0) {
                const int row = terrainRow(sq);
                if (row >= 0 && row < static_cast<int>(rules_.terrain.size())) {
                    score += rules_.terrain[static_cast<std::size_t>(row)].defenseBonus * 4;
                }
                for (const int other : unitsAt(x, y)) {
                    const auto& v = unit(other);
                    switch (v.type) {
                    case kColonist:
                        score += 5;
                        strike = aloneHere != 0;
                        break;
                    case kSoldier:
                    case kDragoon:
                        if (fromHome <= 1) {
                            score += (threat + 5) * 4;
                        }
                        break;
                    case kPioneer:
                        score += 10;
                        strike = aloneHere != 0;
                        break;
                    case kScout:
                        score += 10;
                        break;
                    case kTreasure:
                        if (aloneHere != 0) {
                            score += 50;
                            strike = true;
                        }
                        treasure = true;
                        break;
                    case kArtillery:
                        score += 35;
                        prize = true;
                        strike = aloneHere != 0;
                        break;
                    case kWagonTrain:
                        score += 2;
                        if (v.holds != 0) {
                            prize = true;
                            flags |= kLaden;
                            score += 8;
                        }
                        if (prize || 50 + nextRandom(51) < tense) {
                            if (unitsOn(v.x, v.y) == 1) {
                                score += 10;
                                prize = true;
                                strike = true;
                            }
                        }
                        break;
                    default:
                        break;
                    }
                }
            }
            if (town >= 0) {
                if (tense >= 75) {
                    score += 20;
                }
                if (tense >= 50) {
                    score += 10;
                }
            }
            if (tense <= 25 && !angry) {
                // A calm tribe only goes for a lone treasure, now and then.
                if (!strike || !treasure || nextRandom(8) != 0) {
                    continue;
                }
            } else if (town < 0 && alarm >= 32) {
                angry = true;
            }
            score -= 50 - tense;
            if (!strike && !angry) {
                continue;
            }
            flags |= kAttack;
            attackSeen = true;
        }

        // Home: muskets and horses wait there.
        bool upgrade = false;
        if (town == own) {
            if (static_cast<std::int8_t>(t[kMuskets]) > 0 && (u.type == kBrave || u.type == kMountedBrave)) {
                score += 20;
                upgrade = true;
            }
            if (word(t + kHorses) >= kHorsesForMount && full <= 3) {
                score += 20;
                upgrade = true;
            }
        }
        if (dir != kStay) {
            if (units >= 0 && units == own) {
                if (unitsOn(x, y) >= 2 && town < 0) {
                    continue;
                }
                score -= 40;
            }
        } else {
            score += (1 - unitsOn(u.x, u.y)) * 40;
            if (attackSeen || nextRandom(4 * (level + 1) + 1) == 0) {
                score -= 25;
            }
        }
        if (dir != kStay) {
            const int last = static_cast<std::int8_t>(u.raw[kLastStep]);
            if (last == dir) {
                score += 4;
            } else if (last >= 0 && nextTo(dir, last)) {
                score += 3;
            } else if ((last ^ 4) == dir) {
                score -= 6;
            }
            if (road && hereRoad) {
                score += 4;
            } else if ((dir & 1) == 0 && sq.river() && hereRiver) {
                score += 4;
            }
        } else if (grudges == 0) {
            if (besideNation >= 0) {
                if (besideNation < kEuropeanNations && tensionLevel(tension(besideNation)) > 0) {
                    score += ((tension(besideNation) - 50) >> 1) + 8;
                }
            } else if (!upgrade) {
                continue;
            }
        } else if (besideNation >= 0 && besideNation < kEuropeanNations) {
            const int tense = tension(besideNation);
            if (tense >= 95) {
                score += 16;
            } else if (tensionLevel(tense) > 0) {
                score += ((tense - 50) >> 1) + 5;
            }
        }

        // Not too far from home.
        if (homeHere >= 0) {
            const int d = distance(home.x - x, home.y - y);
            if (d > 2) {
                int penalty = 3 * d;
                if (grudges != 0) {
                    penalty >>= 1;
                }
                if (withMuskets(u.type)) {
                    penalty >>= 1;
                }
                if (mounted(u.type)) {
                    penalty >>= 2;
                }
                score -= penalty;
            }
        }
        // Next to someone else: other natives keep them away; Europeans not
        // yet met draw them.
        const int neighbour = foreignNext(x, y, true);
        if (neighbour >= kEuropeanNations) {
            score -= 25;
        } else if (neighbour >= 0) {
            if ((relation(neighbour) & kMet) == 0) {
                score += 50;
            }
            const int tense = tension(neighbour);
            if (tensionLevel(tense) > 0) {
                score += (tense - 50) >> 2;
            }
        }

        const auto colonyPull = [&](bool hostile) {
            if (colony < 0) {
                return;
            }
            const auto& c = save_.colonies[static_cast<std::size_t>(colony)];
            const int d = distance(c.x - x, c.y - y);
            if (landRegion(map, x, y) != region) {
                return;
            }
            const int tense = tension(c.nation);
            if (!hostile || tense < 75) {
                if (d < 12) {
                    score += (std::max(1, tensionLevel(tense) + 1) * (12 - d)) >> 2;
                }
            } else if ((relation(c.nation) & kMet) != 0) {
                score -= 2 * d;
            }
        };
        if (grudges == 0) {
            if (occ < 0) {
                score += 5;
            }
            colonyPull(false);
        } else if (!angry && !war) {
            if (town >= 0) {
                continue;
            }
            colonyPull(true);
        } else {
            score += 5;
            if (special) {
                score += 10;
            }
            if (colonyAt(x, y)) {
                score += 500;
            } else if (units >= 0) {
                const int defender = defenderAt(x, y, index);
                if (defender >= 0) {
                    const int strength = unitForce(u, true) * 3 >> 1;
                    const auto& dv = unit(defender);
                    int guard = defenceStrength(index, dv, isShip(defender), isDamagedArtillery(defender), 0);
                    if (dv.type == kArtillery) {
                        guard >>= 3;
                    }
                    for (const int other : unitsAt(x, y)) {
                        switch (unit(other).type) {
                        case 0:
                            score += 4;
                            break;
                        case 1:
                            score -= 2;
                            break;
                        case 2:
                        case 3:
                        case 5:
                            score += 8;
                            break;
                        case 4:
                            score -= 1;
                            break;
                        case 10:
                        case 11:
                        case 12:
                            score += 16;
                            break;
                        default:
                            break;
                        }
                    }
                    score += guard > strength ? 2 * (strength - guard) : strength - guard + 30;
                }
            }
            colonyPull(true);
        }

        score = std::max(0, score + 1 + nextRandom(5));
        if (score > best) {
            best = score;
            bestDir = dir;
            bestFlags = flags;
        }
    }

    u.raw[kLastStep] = static_cast<std::uint8_t>(bestDir);
    const auto stay = [&]() {
        u.movesUsed = movementAllowance(index);
        return kStay;
    };
    if (bestDir == kStay) {
        u.orders = (u.orders == SaveUnit::kFortify || u.orders == SaveUnit::kFortified) ? SaveUnit::kFortified
                                                                                        : SaveUnit::kFortify;
        if (townOwner(u.x, u.y) == own) {
            // At home a brave takes up muskets and horses.
            if (static_cast<std::int8_t>(t[kMuskets]) > 0 && (u.type == kBrave || u.type == kMountedBrave)) {
                ++u.type;
                if (nextRandom(save_.difficulty + 1) == 0) {
                    --t[kMuskets];
                }
            }
            if (word(t + kHorses) >= kHorsesForMount && movementAllowance(index) <= 3) {
                u.type += 2;
                setWord(t + kHorses, word(t + kHorses) - kHorsesForMount);
            }
        }
    } else {
        u.orders = SaveUnit::kNoOrders;
    }
    if ((bestFlags & (kTaken | kAttack)) != 0) {
        // One attack on a nation a turn for the tribe, and none in the turn
        // they met.
        const int x = u.x + kDx[static_cast<std::size_t>(bestDir)];
        const int y = u.y + kDy[static_cast<std::size_t>(bestDir)];
        const int target = occupant(x, y);
        if (target >= 0 && target < kEuropeanNations) {
            auto* mark = t + kWarning + 2 * static_cast<std::size_t>(target);
            if (word(mark) == 2) {
                return stay();
            }
            setWord(mark, 1);
        }
    }
    const bool warStep = (bestFlags & kAtWar) != 0;
    if ((bestFlags & (kTaken | kAttack)) != 0 && movementAllowance(index) - u.movesUsed < 3) {
        return stay(); // an attack needs a full move
    }
    if ((bestFlags & kAttack) != 0) {
        // An attack on the player is announced: "Sioux make surprise raid
        // near Jamestown! ... Sioux chief denies involvement."
        const int x = u.x + kDx[static_cast<std::size_t>(bestDir)];
        const int y = u.y + kDy[static_cast<std::size_t>(bestDir)];
        const int target = occupant(x, y);
        const int near = target == human() ? nearestColony(save_, x, y, target) : -1;
        const auto tribeIndex = static_cast<std::size_t>(tribe);
        if (near >= 0 && tribeIndex < rules_.tribes.size()) {
            notices_.push_back(GameNotice{"INDIANSURPRISE",
                                          {rules_.tribes[tribeIndex].name,
                                           save_.colonies[static_cast<std::size_t>(near)].name,
                                           rules_.tribes[tribeIndex].adjective}});
        }
    }
    if ((bestFlags & (kTaken | kAttack)) != 0 || warStep) {
        u.raw[kUnitFlags] = static_cast<std::uint8_t>(u.raw[kUnitFlags] & ~kVisitDue);
        bestFlags &= ~kVisiting;
    }
    if ((u.raw[kUnitFlags] & kVisitDue) != 0) {
        // The visit it came for: it looks around as after a step (a
        // meeting, a visit).
        u.raw[kUnitFlags] = static_cast<std::uint8_t>(u.raw[kUnitFlags] & ~kVisitDue);
        meetNeighbours(index);
        return stay();
    }
    if ((bestFlags & kVisiting) != 0) {
        u.raw[kStamp] = static_cast<std::uint8_t>(turn & 0xFF);
        u.raw[kStamp + 1] = static_cast<std::uint8_t>((turn >> 8) & 0xFF);
        u.raw[kUnitFlags] = static_cast<std::uint8_t>(u.raw[kUnitFlags] | kVisitDue);
    }
    if ((bestFlags & (kVisiting | kAttack | kTaken)) == 0 && nearHome >= 0) {
        // Off to the colony near home, now and then.
        const auto& c = save_.colonies[static_cast<std::size_t>(colony)];
        if ((nationPeople(c.nation) >> 3) + 4 * nearHome + 10 <= sinceVisit) {
            u.raw[kDestX] = static_cast<std::uint8_t>(c.x);
            u.raw[kDestY] = static_cast<std::uint8_t>(c.y);
            const int step = stepToward(index, c.x, c.y);
            if (step >= 0) {
                const int x = u.x + kDx[static_cast<std::size_t>(step)];
                const int y = u.y + kDy[static_cast<std::size_t>(step)];
                const int town = townOwner(x, y);
                const int units = unitOwner(x, y);
                if ((town < 0 || town == own) && (units < 0 || units == own)) {
                    return step;
                }
            }
        }
    }
    if ((bestFlags & (kTaken | kAttack | kLaden)) == 0 && (bestFlags & kForeign) != 0 && u.movesUsed != 0) {
        return stay();
    }
    if (bestDir == kStay) {
        return stay();
    }
    return bestDir;
}

int Game::stepToward(int index, int toX, int toY) {
    // Not the original's route finder yet (the one Go To uses): the
    // neighbour on the same land that comes closest, the first of equals.
    auto& u = save_.units[static_cast<std::size_t>(index)];
    const auto& map = save_.map;
    const int region = landRegion(map, u.x, u.y);
    int bestDir = -1;
    int bestDistance = distance(toX - u.x, toY - u.y);
    if (bestDistance == 0) {
        return -1;
    }
    for (int d = 0; d < 8; ++d) {
        const int x = u.x + kDx[static_cast<std::size_t>(d)];
        const int y = u.y + kDy[static_cast<std::size_t>(d)];
        if (!map.inside(x, y) || map.at(x, y).isWater()) {
            continue;
        }
        const bool there = x == toX && y == toY;
        if (!there && landRegion(map, x, y) != region) {
            continue;
        }
        const int dist = distance(toX - x, toY - y);
        if (dist < bestDistance) {
            bestDistance = dist;
            bestDir = d;
        }
    }
    u.raw[kLastStep] = static_cast<std::uint8_t>(bestDir);
    if (bestDir < 0) {
        u.movesUsed = movementAllowance(index);
    }
    return bestDir;
}

void Game::nativeUnitTurn(int index) {
    const int dir = nativeUnitAI(index);
    if (dir < 0 || dir >= kStay) {
        return;
    }
    auto& u = save_.units[static_cast<std::size_t>(index)];
    const int x = u.x + kDx[static_cast<std::size_t>(dir)];
    const int y = u.y + kDy[static_cast<std::size_t>(dir)];
    const auto colony = colonyAt(x, y);
    const auto settlement = settlementAt(x, y);
    bool foreign = (colony && save_.colonies[static_cast<std::size_t>(*colony)].nation != u.nation) ||
                   (settlement && save_.settlements[static_cast<std::size_t>(*settlement)].nation != u.nation);
    bool european = colony.has_value();
    for (const int other : unitsAt(x, y)) {
        foreign = foreign || unit(other).nation != u.nation;
        european = european || unit(other).nation < kEuropeanNations;
    }
    if (foreign) {
        // As the original: short of a whole move the brave's turn ends;
        // otherwise it attacks.
        constexpr int kWholeMove = 3;
        if (!european || movementAllowance(index) - u.movesUsed < kWholeMove) {
            u.movesUsed = movementAllowance(index);
            return;
        }
        nativeAttack(index, x, y);
        return;
    }
    // As the original moves any unit: the cost is spent first. Short of
    // moves (and not on its first step), it gets there with a chance of the
    // moves left in the cost; otherwise its turn is simply over.
    const int cost = moveCost(index, u.x, u.y, x, y);
    const int left = movementAllowance(index) - u.movesUsed;
    const bool first = u.movesUsed == 0;
    u.movesUsed += cost;
    if (left < cost && !first && 1 + nextRandom(cost) > left) {
        return;
    }
    u.x = x;
    u.y = y;
    u.orders = SaveUnit::kNoOrders;
    refreshUnitFlags();
    meetNeighbours(index);
    auto& moved = save_.units[static_cast<std::size_t>(index)];
    if (++moved.workTurns >= kStepLimit) {
        moved.workTurns = 0;
        moved.movesUsed = movementAllowance(index);
    }
}

void Game::moveTribeUnits(int tribe) {
    const int nation = kEuropeanNations + tribe;
    for (auto& u : save_.units) {
        if (u.nation == nation) {
            u.workTurns = 0;
        }
    }
    const auto canMove = [&](int i) {
        const auto& u = unit(i);
        // A unit with bit 0x80 of byte 4 waits, except damaged artillery.
        return u.nation == nation && onMap(i) &&
               ((u.raw[kUnitFlags] & SaveUnit::kDamaged) == 0 || u.type == kArtillery) &&
               u.movesUsed < movementAllowance(i);
    };
    // As the original: the first of the tribe's units with moves left takes
    // its steps, then the search starts again from the first unit.
    bool moved = true;
    while (moved) {
        moved = false;
        for (int i = 0; i < unitCount() && !moved; ++i) {
            while (i < unitCount() && canMove(i)) {
                auto& u = save_.units[static_cast<std::size_t>(i)];
                if (++u.workTurns > kStepLimit) {
                    u.movesUsed = movementAllowance(i);
                    u.workTurns = 0;
                    continue;
                }
                nativeUnitTurn(i);
                moved = true;
            }
        }
    }
}

} // namespace viceroy
