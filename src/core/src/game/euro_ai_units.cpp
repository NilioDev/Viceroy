// SPDX-License-Identifier: GPL-3.0-or-later
//
// The units of a European nation the computer plays: the plan it makes at
// the start of its turn (the places worth going to and what each region is
// to it), then what each of its units does. Read from the original with a
// debugger and a disassembler (observed only); the rules are in our own
// words in docs/rules/european-ai.md. Attacks are still to come: a square
// held by another nation or tribe is left alone.
#include "viceroy/game/colony.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"

#include <algorithm>
#include <climits>
#include <cstdlib>

namespace viceroy {

namespace {

constexpr int kRegions = 16;
constexpr int kTypesCounted = 19;
constexpr int kFirstShip = 13;
constexpr int kLastShip = 18;
constexpr int kMerchantman = 14;
constexpr int kGalleon = 15;
constexpr int kPrivateer = 16;
constexpr int kFrigate = 17;
constexpr int kManOWar = 18;
constexpr int kVeteranSoldier = 0x15;
constexpr int kConvert = 0x1B;
constexpr int kHorsesGood = 8;
constexpr int kSeaLane = 26;

// The block after the tribe records.
constexpr std::size_t kExtra = static_cast<std::size_t>(kTribes) * kTribeRecordSize;
constexpr std::size_t kUnitsAll = kExtra + 12;
constexpr std::size_t kColonyCount = kExtra + 16;
constexpr std::size_t kHoldsAll = kExtra + 36;
constexpr std::size_t kByType = kExtra + 64;
constexpr std::size_t kRegionSettlements = kExtra + 140; // of the tribe counted last
constexpr std::size_t kRegionFlags = kExtra + 156;
constexpr std::size_t kRegionUnits = kExtra + 172;
constexpr std::size_t kRegionColonies = kExtra + 236;
constexpr std::size_t kRegionPeople = kExtra + 364;
constexpr std::size_t kRegionCombat = kExtra + 428;
constexpr std::size_t kRegionAttack = kExtra + 492;
constexpr std::size_t kTribeRegionDefence = kExtra + 589;
constexpr std::size_t kBlockEnd = kExtra + 727;

// In the block after the map, words by region.
constexpr std::size_t kFarmland = 540;
constexpr std::size_t kLandSize = 572;

// Nation record.
constexpr std::size_t kRelations = 52;
constexpr std::size_t kFirstColonyTurn = 70; // word

// Unit record.
constexpr std::size_t kFlags = 4;
constexpr std::size_t kHome = 6; // the colony (or settlement) it belongs to
constexpr std::size_t kGoal = 7;
constexpr std::size_t kDestX = 9;
constexpr std::size_t kDestY = 10;
constexpr std::size_t kLastStep = 11;
constexpr std::size_t kSearches = 16; // site searches so far
constexpr std::size_t kWander = 17;   // turns left on a wander
constexpr std::size_t kRing = 18;     // which way it wanders
constexpr std::size_t kTrading = 20;  // a wagon train off to trade
constexpr std::uint8_t kDamaged = 0x80;

// Colony record.
constexpr std::size_t kThreat = 0x1B;
constexpr std::size_t kMarks = 0x1C;
constexpr std::uint8_t kCoastal = 0x40;
constexpr std::size_t kGarrisonCount = 0x1E;
constexpr std::size_t kWantedGood = 0x8D;
constexpr std::size_t kGarrison = 0x8E;
constexpr std::size_t kPickupBonus = 0x8F;
constexpr std::size_t kExports = 0x90; // word
constexpr std::size_t kWarehouseLevel = 0x95;

// The orders the computer gives.
constexpr int kOrdersBuild = 7;
constexpr int kOrdersThreat = 10;
constexpr int kOrdersGo = 11;
constexpr int kOrdersStep = 12;

// The 20 squares around a square at distance 1 and 2, in the original's
// order, and its 8 steps plus the square itself.
constexpr std::array<int, 20> kRingX{0, 1, 0, -1, -1, 1, 1, -1, 0, 2, 0, -2, -1, 1, -1, 1, -2, -2, 2, 2};
constexpr std::array<int, 20> kRingY{-1, 0, 1, 0, -1, -1, 1, 1, -2, 0, 2, 0, -2, -2, 2, 2, -1, 1, -1, 1};
constexpr std::array<int, 9> kStepX{0, 1, 1, 1, 0, -1, -1, -1, 0};
constexpr std::array<int, 9> kStepY{-1, -1, 0, 1, 1, 1, 0, -1, 0};

bool shipType(int type) {
    return type >= kFirstShip && type <= kLastShip;
}

bool personType(int type) {
    return (type >= 0 && type <= 5) || type == 7 || type == 9;
}

// The original reckons in 16 bits.
int wrap16(int v) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(v & 0xFFFF));
}

int signedByte(std::uint8_t b) {
    return b < 0x80 ? b : b - 256;
}

int word(const Bytes& block, std::size_t at) {
    return at + 1 < block.size() ? static_cast<std::int16_t>(block[at] | (block[at + 1] << 8)) : 0;
}

int colonyWord(const SaveColony& c, std::size_t at) {
    return c.raw[at] | (c.raw[at + 1] << 8);
}

int distanceBetween(int x0, int y0, int x1, int y1) {
    const int dx = std::abs(x1 - x0);
    const int dy = std::abs(y1 - y0);
    return std::max(dx, dy) + std::min(dx, dy) / 2;
}

// How many people a colony is meant to have: more with a larger town
// hall (building bits 10 and 11).
int colonyTarget(const SaveColony& colony) {
    const int halls = static_cast<int>((colony.buildings >> 10) & 1) + static_cast<int>((colony.buildings >> 11) & 1);
    return halls == 0 ? 8 : halls == 1 ? 12 : 32;
}

int sign(int v) {
    return v > 0 ? 1 : v < 0 ? -1 : 0;
}

bool waterOfSea(const SaveMap& map, int x, int y) {
    // A water square of the open sea (region 1 of the water).
    return map.inside(x, y) && map.at(x, y).isWater() && (map.at(x, y).region & 0x0F) == 1;
}

struct Nearest {
    int index = -1;
    int distance = 9999;
};

// The nearest colony of a nation (-1: any) in a region (-1: any; -2: the
// land region of the square, colonies by the water only). Ties go to the
// later colony.
Nearest nearestColonyTo(const SaveGame& save, int x, int y, int nation, int region) {
    Nearest best;
    int want = region;
    if (region == -2) {
        want = landRegion(save.map, x, y);
    }
    for (std::size_t i = 0; i < save.colonies.size(); ++i) {
        const auto& c = save.colonies[i];
        if (nation >= 0 && c.nation != nation) {
            continue;
        }
        if (region == -2 && (c.raw[kMarks] & kCoastal) == 0) {
            continue;
        }
        if (want >= 0 && landRegion(save.map, c.x, c.y) != want) {
            continue;
        }
        if (region == -2 && want < 0) {
            continue;
        }
        const int d = distanceBetween(x, y, c.x, c.y);
        if (d <= best.distance) {
            best = {static_cast<int>(i), d};
        }
    }
    return best;
}

Nearest nearestSettlementTo(const SaveGame& save, int x, int y, int region) {
    Nearest best;
    for (std::size_t i = 0; i < save.settlements.size(); ++i) {
        const auto& s = save.settlements[i];
        if (region >= 0 && landRegion(save.map, s.x, s.y) != region) {
            continue;
        }
        const int d = distanceBetween(x, y, s.x, s.y);
        if (d <= best.distance) {
            best = {static_cast<int>(i), d};
        }
    }
    return best;
}

int alarmFor(const SaveSettlement& s, int nation) {
    if (nation < 0 || nation >= kEuropeanNations) {
        return 0;
    }
    const auto at = static_cast<std::size_t>(10 + 2 * nation);
    return static_cast<std::int16_t>(s.raw[at] | (s.raw[at + 1] << 8));
}

} // namespace

// --- Small helpers -----------------------------------------------------------

int Game::aiRelation(int n, int other) const {
    if (n < 0 || n >= kEuropeanNations || other < 0 || other >= kEuropeanNations + kTribes) {
        return 0;
    }
    return save_.nations[static_cast<std::size_t>(n)].raw[kRelations + static_cast<std::size_t>(other)];
}

void Game::aiAddTarget(int n, int x, int y, int kind, int prio) {
    // Kept in order of weight; a place already listed as heavily is not
    // listed again, and the lightest entry falls off the end.
    auto& list = ai_.targets[static_cast<std::size_t>(n)];
    prio = signedByte(static_cast<std::uint8_t>(prio));
    for (const auto& t : list) {
        if (t.kind == kind && t.x == x && t.y == y && t.prio >= prio) {
            return;
        }
    }
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].kind < 0 || list[i].prio < prio) {
            for (std::size_t j = list.size() - 1; j > i; --j) {
                list[j] = list[j - 1];
            }
            list[i] = {x, y, kind, prio};
            return;
        }
    }
}

int Game::aiTargetPrio(int n, int x, int y, int kind) const {
    int best = 0;
    for (const auto& t : ai_.targets[static_cast<std::size_t>(n)]) {
        if (t.kind == kind && t.x == x && t.y == y) {
            best = std::max(best, t.prio);
        }
    }
    return best;
}

void Game::aiAddPickup(int colony, int value, int loads, int military) {
    auto& list = ai_.pickups;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].colony < 0 || list[i].value < value) {
            for (std::size_t j = list.size() - 1; j > i; --j) {
                list[j] = list[j - 1];
            }
            list[i] = {colony, value, loads, military};
            return;
        }
    }
}

int Game::aiStack(int index, int category) const {
    // Counts over the units on the square of the given unit.
    if (index < 0 || index >= unitCount()) {
        return 0;
    }
    const auto& units = rules_.units;
    int total = 0;
    for (const int i : unitsAt(unit(index).x, unit(index).y)) {
        const auto& u = unit(i);
        if (u.type < 0 || static_cast<std::size_t>(u.type) >= units.size()) {
            continue;
        }
        const auto& t = units[static_cast<std::size_t>(u.type)];
        const bool ship = shipType(u.type);
        switch (category) {
        case 0: // strength
            total += t.cost;
            break;
        case 2: // everyone
            ++total;
            break;
        case 3:
            total += u.type == unit_type::kPioneer ? 1 : 0;
            break;
        case 4: // soldiers, dragoons and the regulars
            total += u.type == 1 || u.type == 4 || (u.type >= 6 && u.type <= 9) ? 1 : 0;
            break;
        case 5:
            total += u.type == unit_type::kScout ? 1 : 0;
            break;
        case 6: // the same, and veterans of any kind
            total += u.type == 1 || u.type == 4 || u.profession == kVeteranSoldier ? 1 : 0;
            total += u.type >= 6 && u.type <= 9 ? 1 : 0;
            break;
        case 10: // armed land units
            total += !ship && t.attack > 1 ? 1 : 0;
            break;
        case 12:
            total += u.type == kArtillery ? 1 : 0;
            break;
        case 13: // the ships' holds
            total += ship ? t.cargoSlots : 0;
            break;
        case 14: // the largest ship's holds
            if (ship && (u.raw[kFlags] & kDamaged) == 0) {
                total = std::max(total, t.cargoSlots);
            }
            break;
        default:
            break;
        }
    }
    return total;
}

int Game::aiHostile(int n, int other, bool tribes, int index) const {
    // Another European nation always counts; a tribe only when it is
    // furious with the nation or the brave's village is alarmed.
    if (other < kEuropeanNations) {
        return other;
    }
    if (!tribes) {
        return -1;
    }
    if (tribeTension(save_, other - kEuropeanNations, n) >= 75) {
        return other;
    }
    if (index >= 0 && index < unitCount()) {
        const std::size_t home = unit(index).raw[kHome];
        if (home < save_.settlements.size() && alarmFor(save_.settlements[home], n) >= 0x80) {
            return other;
        }
    }
    return -1;
}

int Game::aiThreatNear(int x, int y, int n, bool tribes) const {
    // Who threatens the square from next door, on the same land or water:
    // an armed ship next to one at sea, any unit or town of another nation
    // on land.
    const auto& map = save_.map;
    const bool water = map.inside(x, y) && map.at(x, y).isWater();
    int found = -1;
    for (int d = 0; d < 8 && found < 0; ++d) {
        const int sx = x + kStepX[static_cast<std::size_t>(d)];
        const int sy = y + kStepY[static_cast<std::size_t>(d)];
        if (!map.inside(sx, sy) || map.at(sx, sy).isWater() != water) {
            continue;
        }
        const auto here = unitsAt(sx, sy);
        const int owner = here.empty() ? -1 : unit(here.front()).nation;
        if (owner >= 0 && owner != n) {
            found = aiHostile(n, owner, tribes, owner < kEuropeanNations ? -1 : here.front());
            if (found >= 0 && water) {
                bool armed = false;
                for (const int i : here) {
                    const int t = unit(i).type;
                    armed = armed || (shipType(t) && rules_.units[static_cast<std::size_t>(t)].attack != 0);
                }
                if (!armed) {
                    found = -1;
                }
            }
        }
        int town = -1;
        if (const auto c = colonyAt(sx, sy)) {
            town = save_.colonies[static_cast<std::size_t>(*c)].nation;
        } else if (const auto s = settlementAt(sx, sy)) {
            town = save_.settlements[static_cast<std::size_t>(*s)].nation;
        }
        if (town >= 0 && town != n) {
            found = aiHostile(n, town, tribes, -1);
        }
    }
    return found;
}

bool Game::aiForeignNear(int x, int y, int n) const {
    // Anyone else's unit or town next to the square, on the same land or
    // water (from a town, on either).
    const auto& map = save_.map;
    if (!map.inside(x, y)) {
        return false;
    }
    const auto townOwner = [&](int sx, int sy) {
        if (const auto c = colonyAt(sx, sy)) {
            return save_.colonies[static_cast<std::size_t>(*c)].nation;
        }
        if (const auto s = settlementAt(sx, sy)) {
            return save_.settlements[static_cast<std::size_t>(*s)].nation;
        }
        return -1;
    };
    bool water = map.at(x, y).isWater();
    const bool inTown = townOwner(x, y) >= 0;
    for (int d = 0; d < 8; ++d) {
        const int sx = x + kStepX[static_cast<std::size_t>(d)];
        const int sy = y + kStepY[static_cast<std::size_t>(d)];
        if (!map.inside(sx, sy)) {
            continue;
        }
        const bool there = inTown ? water : map.at(sx, sy).isWater();
        int owner = townOwner(sx, sy);
        if (owner >= 0) {
            water = there;
        } else {
            const auto here = unitsAt(sx, sy);
            owner = here.empty() ? -1 : unit(here.front()).nation;
        }
        if (owner >= 0 && owner != n && there == water) {
            return true;
        }
    }
    return false;
}

int Game::aiRegionScore(int n, int r) const {
    // How much room a region still has for colonies of the nation.
    const auto& block = save_.tribesAndMore;
    if (r < 0 || r >= kRegions || block.size() < kBlockEnd) {
        return 0;
    }
    const auto ur = static_cast<std::size_t>(r);
    const int land = word(save_.tail, kLandSize + 2 * ur) / 12;
    int towns = block[kRegionSettlements + ur];
    for (std::size_t k = 0; k < kEuropeanNations; ++k) {
        towns += block[kRegionColonies + kRegions * k + ur];
    }
    int score = sign(land - towns);
    if (block[kRegionSettlements + ur] == towns) {
        score += 2;
    }
    if (block[kRegionColonies + kRegions * static_cast<std::size_t>(n) + ur] < 1) {
        score += 4;
    }
    return score;
}

int Game::aiFoundBias(int n, int index, int region) const {
    const auto& u = unit(index);
    const auto& block = save_.tribesAndMore;
    int s = 0;
    if (block.size() >= kBlockEnd && block[kColonyCount + static_cast<std::size_t>(n)] > 0) {
        const auto c = nearestColonyTo(save_, u.x, u.y, n, region);
        s = c.index >= 0 ? c.distance / 5 - 1 : 2;
    }
    switch (u.type) {
    case unit_type::kPioneer:
        s += 2;
        break;
    case unit_type::kSoldier:
        s -= 2;
        break;
    case unit_type::kDragoon:
        s -= 3;
        break;
    case unit_type::kColonist:
        s -= 2;
        if (isExpert(u.profession)) {
            s -= 2;
        }
        break;
    default:
        break;
    }
    if (u.profession == kConvert) {
        s -= 20;
    }
    if (aiColonyNeed(n) > 0) {
        const auto& raw = save_.nations[static_cast<std::size_t>(n)].raw;
        const int since = static_cast<std::int16_t>(raw[kFirstColonyTurn] | (raw[kFirstColonyTurn + 1] << 8));
        s += (save_.turn - since) >> 4;
    }
    return std::min(s, 0);
}

int Game::aiFoundScore(int n, int index) const {
    const auto& u = unit(index);
    const int region = landRegion(save_.map, u.x, u.y);
    return std::max(0, aiFoundBias(n, index, region) + aiRegionScore(n, region) + aiColonyNeed(n));
}

int Game::aiStepAround(int n, int x, int y, bool score, bool artillery) const {
    // The best square of land to step onto from (x, y), or 8 to stay.
    const auto& map = save_.map;
    int best = -1;
    int dir = 8;
    for (int d = 0; d < 9; ++d) {
        const int sx = x + kStepX[static_cast<std::size_t>(d)];
        const int sy = y + kStepY[static_cast<std::size_t>(d)];
        if (!map.inside(sx, sy)) {
            continue;
        }
        bool ok = false;
        const auto here = unitsAt(sx, sy);
        const bool town = colonyAt(sx, sy).has_value() || settlementAt(sx, sy).has_value();
        if (here.empty() && !town) {
            ok = true;
        } else if (!town && !here.empty() && unit(here.front()).nation == n && here.size() == 1) {
            ok = (unit(here.front()).type == kArtillery) != artillery;
        }
        if (map.at(sx, sy).isWater() || (d != 8 && !ok)) {
            continue;
        }
        const auto row = static_cast<std::size_t>(terrainRow(map.at(sx, sy)));
        int s = row < rules_.terrain.size() ? rules_.terrain[row].defenseBonus : 0;
        for (int e = 0; e < 8; ++e) {
            const int nx = sx + kStepX[static_cast<std::size_t>(e)];
            const int ny = sy + kStepY[static_cast<std::size_t>(e)];
            if (!map.inside(nx, ny) || map.at(nx, ny).isWater() || !unitsAt(nx, ny).empty()) {
                continue;
            }
            const int visitor = map.at(nx, ny).region >> 4;
            if (visitor < kEuropeanNations && save_.players[static_cast<std::size_t>(visitor)].control == 0 &&
                (aiRelation(n, visitor) & 0x40) != 0) {
                continue;
            }
            if (score) {
                s += 16 * aiRegionScore(n, landRegion(map, sx, sy)) + (map.at(nx, ny).seen & 0x0F);
            }
        }
        if (s > best) {
            best = s;
            dir = d;
        }
    }
    return dir;
}

void Game::aiGoTo(int index, char goal, int x, int y) {
    auto& u = save_.units[static_cast<std::size_t>(index)];
    u.raw[kGoal] = static_cast<std::uint8_t>(goal);
    u.orders = kOrdersGo;
    u.raw[kDestX] = static_cast<std::uint8_t>(x);
    u.raw[kDestY] = static_cast<std::uint8_t>(y);
}

void Game::aiToEurope(int index) {
    // The ship heads for the nearest sea lane and sails on from there.
    const auto& u = unit(index);
    const auto& map = save_.map;
    if (!map.inside(u.x, u.y)) {
        return;
    }
    if (map.at(u.x, u.y).terrain == kSeaLane) {
        sailToEurope(index);
        return;
    }
    int best = -1;
    int bx = 0;
    int by = 0;
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            if (map.at(x, y).terrain != kSeaLane) {
                continue;
            }
            const int d = distanceBetween(u.x, u.y, x, y);
            if (best < 0 || d < best) {
                best = d;
                bx = x;
                by = y;
            }
        }
    }
    if (best >= 0) {
        aiGoTo(index, 'E', bx, by);
        ++ai_.homeward[static_cast<std::size_t>(u.nation & 3)];
    }
}

void Game::aiEndMoves(int index) {
    save_.units[static_cast<std::size_t>(index)].movesUsed = movementAllowance(index);
}

void Game::aiJoinColony(int index, int c) {
    // (Which job the colony gives the newcomer belongs to its own plans,
    // still to come; it starts in the town hall.)
    auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    if (colony.population >= kMaxColonists) {
        aiEndMoves(index);
        return;
    }
    const auto at = static_cast<std::size_t>(colony.population);
    colony.jobs[at] = static_cast<std::uint8_t>(job::kStatesman);
    colony.skills[at] = static_cast<std::uint8_t>(unit(index).profession);
    ++colony.population;
    joinLiberty(colony, save_.map, rules_, save_.difficulty);
    removeUnit(index);
    refreshUnitFlags();
}

void Game::aiBoard(int index, int ship) {
    // A unit in a colony goes aboard: it joins the ship's chain and sails
    // with it.
    std::vector<int> chain;
    for (int at = unit(ship).prevInTransport, steps = 0; at >= 0 && at < unitCount() && steps < unitCount();
         at = unit(at).prevInTransport, ++steps) {
        if (at == index || isShip(at) || unit(at).x != unit(ship).x || unit(at).y != unit(ship).y) {
            break;
        }
        chain.insert(chain.begin(), at);
    }
    chain.push_back(index);
    save_.units[static_cast<std::size_t>(index)].orders = SaveUnit::kSentry;
    chainPassengers(ship, chain);
}

// --- The plan ------------------------------------------------------------------

void Game::aiPlan(int n) {
    auto& block = save_.tribesAndMore;
    if (n < 0 || n >= kEuropeanNations || block.size() < kBlockEnd) {
        return;
    }
    const auto un = static_cast<std::size_t>(n);
    const auto& map = save_.map;
    const auto& units = rules_.units;
    const auto at = [&](std::size_t base, int nation, int region) -> int {
        if (region < 0 || region >= kRegions) {
            return 0;
        }
        return block[base + kRegions * static_cast<std::size_t>(nation) + static_cast<std::size_t>(region)];
    };
    const auto land = [&](int region) {
        return region < 0 ? 0 : word(save_.tail, kLandSize + 2 * static_cast<std::size_t>(region));
    };
    const auto allColonies = [&](int region) {
        int sum = 0;
        for (int k = 0; k < kEuropeanNations; ++k) {
            sum += at(kRegionColonies, k, region);
        }
        return sum;
    };
    const auto isHuman = [&](int nation) {
        return nation < kEuropeanNations && save_.players[static_cast<std::size_t>(nation)].control == 0;
    };
    auto& plan = ai_;
    plan.blocks.fill(0);
    plan.founders.fill(0);
    plan.foreignPeople.fill(0);
    for (auto& t : plan.tribePeople) {
        t.fill(0);
    }
    for (auto& t : plan.targets[un]) {
        t = {};
    }
    plan.loadBase = std::clamp(block[kUnitsAll + un] >> 3, 3, 99);
    plan.load.fill(plan.loadBase);
    plan.shipSent = false;

    // The units: what they may land, and the other nations' ships worth
    // chasing. One ship a turn is picked to fetch recruits from Europe.
    const int merchantmen = block[kByType + kTypesCounted * un + kMerchantman];
    const int galleons = block[kByType + kTypesCounted * un + kGalleon];
    const int caravels = block[kByType + kTypesCounted * un + unit_type::kCaravel];
    int sendType = -1;
    if (merchantmen + galleons >= 2 && merchantmen > 0) {
        sendType = kMerchantman;
    } else if (caravels > 1) {
        sendType = unit_type::kCaravel;
    }
    for (int i = 0; i < unitCount(); ++i) {
        auto& u = save_.units[static_cast<std::size_t>(i)];
        if (u.type < 0 || static_cast<std::size_t>(u.type) >= units.size()) {
            continue;
        }
        const bool ship = shipType(u.type);
        if (u.nation != n) {
            if (ship && u.nation < kEuropeanNations && onMap(i) && u.type != kFrigate &&
                (u.raw[3] & (0x10 << n)) != 0 && ((aiRelation(n, u.nation) & 0x60) == 0x20 || u.type == kPrivateer)) {
                aiAddTarget(n, u.x, u.y, 0, 3);
            }
            continue;
        }
        if (u.raw[kGoal] == 'A') {
            u.raw[kGoal] = 'G';
        }
        u.raw[kFlags] &= 0xD1;
        if (u.orders == SaveUnit::kFortify || u.orders == SaveUnit::kFortified) {
            u.raw[kFlags] |= 0x02;
        }
        const bool armed = aiStack(i, 4) > 1 || aiStack(i, 6) > 0;
        const bool pioneers = aiStack(i, 3) > 0;
        if (armed || pioneers) {
            bool full = true;
            if (ship) {
                full = u.holds >= units[static_cast<std::size_t>(u.type)].cargoSlots;
                for (int j = 0; j < i && full; ++j) {
                    const auto& o = unit(j);
                    if (shipType(o.type) && o.nation == n && o.x == u.x && o.y == u.y &&
                        o.holds < units[static_cast<std::size_t>(o.type)].cargoSlots) {
                        full = false;
                    }
                }
            }
            if (full) {
                if (armed) {
                    u.raw[kFlags] |= 0x0C;
                }
                if (pioneers) {
                    u.raw[kFlags] |= 0x04;
                }
            }
        }
        if (!plan.shipSent && u.type == sendType && (u.raw[kFlags] & 0x0C) == 0) {
            u.raw[kFlags] |= 0x20;
            plan.shipSent = true;
        }
        if (!onMap(i)) {
            if (!ship) {
                u.orders = SaveUnit::kSentry;
            }
            continue;
        }
        const std::size_t cell = static_cast<std::size_t>((u.x >> 2) * 18 + (u.y >> 2));
        if (cell < plan.blocks.size()) {
            plan.blocks[cell] = static_cast<std::uint8_t>(plan.blocks[cell] | (personType(u.type) ? 5 : 1));
        }
        if (u.orders == SaveUnit::kSentry || u.orders == 2 || u.orders == kGoToOrders ||
            (u.orders >= kOrdersThreat && u.raw[kGoal] != '1')) {
            u.orders = SaveUnit::kNoOrders;
        }
        if (aiThreatNear(u.x, u.y, n, true) >= 0) {
            u.orders = kOrdersThreat;
        }
        if (!ship && map.at(u.x, u.y).isWater()) {
            u.orders = SaveUnit::kSentry;
        }
    }

    // The colonies: the others' to watch, blockade or land beside, our
    // own to guard and to fetch goods from.
    for (auto& p : plan.pickups) {
        p.colony = -1;
    }
    plan.biggerRegions = 0;
    plan.emptyRegions = 0;
    const int difficulty = save_.difficulty;
    const int turn = save_.turn;
    for (std::size_t ci = 0; ci < save_.colonies.size(); ++ci) {
        auto& colony = save_.colonies[ci];
        const int c = static_cast<int>(ci);
        const int r = landRegion(map, colony.x, colony.y);
        const auto here = unitsAt(colony.x, colony.y);
        const int top = here.empty() ? -1 : here.front();
        const int people = colony.population;
        if (colony.nation != n) {
            const int owner = colony.nation;
            const bool seen = map.at(colony.x, colony.y).seenBy(n);
            const bool hidden = difficulty * turn <= 180 && !seen && isHuman(owner);
            if (!hidden) {
                const bool present = at(kRegionUnits, n, r) != 0 || at(kRegionColonies, n, r) != 0;
                const bool peaceful = (aiRelation(n, owner) & 0x48) == 0x40;
                if (present && ((c + turn) & 3) != 0 && 6 - turn / 50 < aiStack(top, 2) + people) {
                    aiAddTarget(n, colony.x, colony.y, 4, peaceful ? 3 : 5);
                }
                if (peaceful) {
                    continue;
                }
                if ((colony.raw[kMarks] & kCoastal) != 0 && ((top + turn) % 4 != 0 || aiStack(top, 13) > 0)) {
                    // A square of the open sea two away that looks on the
                    // colony's waters.
                    int best = 0;
                    int bx = 0;
                    int by = 0;
                    for (int dy = -2; dy <= 2; ++dy) {
                        for (int dx = -2; dx <= 2; ++dx) {
                            const int sx = colony.x + dx;
                            const int sy = colony.y + dy;
                            if (std::max(std::abs(dx), std::abs(dy)) != 2 || !waterOfSea(map, sx, sy)) {
                                continue;
                            }
                            int count = 0;
                            for (int d = 0; d < 8; ++d) {
                                const int nx = sx + kStepX[static_cast<std::size_t>(d)];
                                const int ny = sy + kStepY[static_cast<std::size_t>(d)];
                                if (waterOfSea(map, nx, ny) && std::abs(nx - colony.x) <= 1 &&
                                    std::abs(ny - colony.y) <= 1) {
                                    ++count;
                                }
                            }
                            if (count > best) {
                                best = count;
                                bx = sx;
                                by = sy;
                            }
                        }
                    }
                    if (best > 0) {
                        aiAddTarget(n, bx, by, 0, std::min(2, (people + 4) >> 3) + 2);
                    }
                }
            }
            if (owner >= kEuropeanNations || (difficulty * turn <= 200 && !seen)) {
                continue;
            }
            const int ours = at(kRegionColonies, n, r);
            const bool bigger = at(kRegionColonies, owner, r) > ours && at(kRegionPeople, owner, r) >= 8;
            const bool empty = ours == 0 && at(kRegionPeople, owner, r) < 8;
            if (!bigger && !empty) {
                continue;
            }
            // Where to land beside it: open sea up to three away with the
            // most of the region's land next to it.
            int best = -99;
            int lx = 0;
            int ly = 0;
            for (int dx = -3; dx <= 3; ++dx) {
                for (int dy = -3; dy <= 3; ++dy) {
                    const int sx = colony.x + dx;
                    const int sy = colony.y + dy;
                    if (!waterOfSea(map, sx, sy)) {
                        continue;
                    }
                    int count = 0;
                    for (int d = 0; d < 8; ++d) {
                        const int nx = sx + kStepX[static_cast<std::size_t>(d)];
                        const int ny = sy + kStepY[static_cast<std::size_t>(d)];
                        if (map.inside(nx, ny) && !map.at(nx, ny).isWater() && landRegion(map, nx, ny) == r) {
                            ++count;
                        }
                    }
                    if (count == 0) {
                        continue;
                    }
                    const int score = 2 * (std::abs(dx) + std::abs(dy) + count);
                    if (score >= best) {
                        best = score;
                        lx = sx;
                        ly = sy;
                    }
                }
            }
            if (best <= 0 || !unitsAt(lx, ly).empty() || r < 0) {
                continue;
            }
            if (bigger) {
                plan.biggerRegions |= 1 << r;
            } else {
                plan.emptyRegions |= 1 << r;
            }
            int prio = bigger ? 3 : 2;
            if (isHuman(owner)) {
                ++prio;
                if (allColonies(r) == at(kRegionColonies, owner, r)) {
                    prio += land(r) >= 16 ? 1 : 0;
                    prio += land(r) >= 64 ? 1 : 0;
                }
            }
            if (16 * allColonies(r) > land(r)) {
                --prio;
            }
            if ((aiRelation(n, owner) & 0x60) == 0x20) {
                ++prio;
            }
            if (turn < 150) {
                prio *= 2;
            }
            if (6 - turn / 50 >= aiStack(top, 2) + people) {
                continue;
            }
            aiAddTarget(n, lx, ly, bigger ? 7 : 1, prio);
            continue;
        }

        const std::size_t cell = static_cast<std::size_t>((colony.x >> 2) * 18 + (colony.y >> 2));
        if (cell < plan.blocks.size()) {
            plan.blocks[cell] |= 2;
        }
        if ((colony.raw[kMarks] & kCoastal) == 0) {
            continue;
        }
        if ((colony.raw[kThreat] & 0x03) != 0) {
            aiAddTarget(n, colony.x, colony.y, 0, (colony.raw[kThreat] & 0x02) != 0 ? 8 : 5);
        }
        // What a ship could fetch here.
        int value = 0;
        int loads = 0;
        int military = 0;
        bool worth = false;
        const int status = r >= 0 ? plan.status[un][static_cast<std::size_t>(r)] : 0;
        for (const int i : here) {
            const auto& u = unit(i);
            const auto& t = units[static_cast<std::size_t>(u.type)];
            if (u.type == unit_type::kPioneer && (colony.raw[kThreat] & 0x80) == 0) {
                value += 800;
                ++loads;
                worth = true;
            }
            if (status == 0 && !shipType(u.type) && t.attack > 1 && u.raw[kGoal] != 'G' && u.raw[kGoal] != 'A') {
                value += 1500;
                ++loads;
                military = 1;
                worth = true;
            }
        }
        const int floor = 100 * (colony.raw[kWarehouseLevel] + 1);
        const auto& nation = save_.nations[un];
        for (int g = 0; g < kGoodsKinds; ++g) {
            int a = colony.stock[static_cast<std::size_t>(g)];
            if (a < floor) {
                if (g == kHorsesGood) {
                    a = std::max(0, a + 25 - floor);
                }
            } else {
                a *= 2;
            }
            const int lots = (std::min(a, floor) + 25) / 100;
            if (g == 0 || g == 5 || g == 13) {
                continue;
            }
            if (g == 14 || g == 15) {
                if ((colonyWord(colony, kExports) & (1 << g)) == 0) {
                    continue;
                }
                a -= 100;
            }
            if (a >= 75) {
                worth = true;
            }
            if (a >= 0) {
                value += bidPrice(nation, g) * a;
                loads += lots;
            }
        }
        if (worth) {
            ++plan.pickupCalls[un];
            value = std::min(value + 8 * colony.raw[kPickupBonus], 32767);
            aiAddPickup(c, value, loads, military);
        }
        if (signedByte(colony.raw[kGarrison]) > 0) {
            const int guards = aiStack(top, 10);
            if (colony.raw[kGarrison] > guards) {
                aiAddTarget(n, colony.x, colony.y, 3, colony.raw[kGarrison] - guards + 2);
            }
            // Who stays to guard it, the artillery first.
            const auto guardPass = [&](int pass, const SaveUnit& u) {
                const bool veteran = u.profession == kVeteranSoldier;
                switch (pass) {
                case 0:
                    return u.type == kArtillery;
                case 1:
                    return u.type == unit_type::kSoldier && !veteran;
                case 2:
                    return u.type == unit_type::kSoldier && veteran;
                case 3:
                    return u.type == unit_type::kDragoon && !veteran;
                default:
                    return u.type == unit_type::kDragoon && veteran;
                }
            };
            for (int pass = 0; pass < 5; ++pass) {
                for (const int i : here) {
                    if (signedByte(colony.raw[kGarrison]) <= 0) {
                        break;
                    }
                    auto& u = save_.units[static_cast<std::size_t>(i)];
                    if (u.nation != n || u.raw[kGoal] == 'A' || !guardPass(pass, u)) {
                        continue;
                    }
                    u.raw[kGoal] = 'A';
                    --colony.raw[kGarrison];
                    if (colony.raw[kGarrisonCount] > 0) {
                        --colony.raw[kGarrisonCount];
                    }
                }
            }
        }
    }

    // The settlements: the angry ones to deal with, and landings for
    // regions without a colony of the nation.
    for (const auto& s : save_.settlements) {
        const int tribe = tribeOf(s);
        const int r = landRegion(map, s.x, s.y);
        if (r < 0 || tribe < 0 || tribe >= kTribes) {
            continue;
        }
        plan.tribePeople[static_cast<std::size_t>(tribe)][static_cast<std::size_t>(r)] += s.population;
        if ((at(kRegionUnits, n, r) != 0 || at(kRegionColonies, n, r) != 0) &&
            (tribeTension(save_, tribe, n) >= 75 || (aiRelation(n, s.nation) & 0x02) != 0)) {
            aiAddTarget(n, s.x, s.y, 4, signedByte(s.raw[5]) < 0 ? 2 : 4);
        }
        if ((((plan.biggerRegions | plan.emptyRegions) >> r) & 1) == 0 && at(kRegionColonies, n, r) == 0) {
            int best = -1;
            int bx = 0;
            int by = 0;
            for (int d = 0; d < 8; ++d) {
                const int sx = s.x + kStepX[static_cast<std::size_t>(d)];
                const int sy = s.y + kStepY[static_cast<std::size_t>(d)];
                if (!waterOfSea(map, sx, sy)) {
                    continue;
                }
                int count = 0;
                for (int e = 0; e < 8; ++e) {
                    const int nx = sx + kStepX[static_cast<std::size_t>(e)];
                    const int ny = sy + kStepY[static_cast<std::size_t>(e)];
                    if (map.inside(nx, ny) && !map.at(nx, ny).isWater() && landRegion(map, nx, ny) == r) {
                        ++count;
                    }
                }
                if (count > best) {
                    best = count;
                    bx = sx;
                    by = sy;
                }
            }
            if (bx > 0) {
                aiAddTarget(n, bx, by, 1, 2);
                plan.emptyRegions |= 1 << r;
            }
        }
    }

    // What each region is to the nation: 0 crowded, 6 open, 4 where it is
    // the stronger or has nobody, 3 where it is the weaker.
    for (int r = 0; r < kRegions; ++r) {
        const auto ur = static_cast<std::size_t>(r);
        int all = 0;
        int stronger = 0;
        int weaker = 0;
        const int ourCombat = at(kRegionCombat, n, r);
        const int ourColonies = at(kRegionColonies, n, r);
        for (int k = 0; k < kEuropeanNations; ++k) {
            all += at(kRegionColonies, k, r);
            if (k == n || (at(kRegionColonies, k, r) == 0 && at(kRegionUnits, k, r) == 0)) {
                continue;
            }
            const int rel = aiRelation(n, k);
            if ((rel & 0x60) != 0x20 && (rel & 0x48) == 0x40) {
                continue;
            }
            if (ourCombat > at(kRegionCombat, k, r) || ourColonies == 0) {
                ++stronger;
            } else {
                ++weaker;
            }
        }
        for (int t = 0; t < kTribes; ++t) {
            const int theirs = block[kTribeRegionDefence + kRegions * static_cast<std::size_t>(t) + ur];
            if (theirs == 0 && plan.tribePeople[static_cast<std::size_t>(t)][ur] == 0) {
                continue;
            }
            if (tribeTension(save_, t, n) < 75 && (aiRelation(n, kEuropeanNations + t) & 0x02) == 0) {
                continue;
            }
            if (ourCombat > theirs || ourColonies == 0) {
                ++stronger;
            } else {
                ++weaker;
            }
        }
        auto& s = plan.status[un][ur];
        s = 20 * (ourColonies + all) > land(r) ? 0 : 6;
        if (stronger != 0) {
            s = 4;
        }
        if (weaker != 0) {
            s = 3;
        }
        if (at(kRegionUnits, n, r) == 0 && ourColonies == 0) {
            s = 4;
        }
        int largest = 0;
        for (const auto& colony : save_.colonies) {
            if (colony.nation != n && landRegion(map, colony.x, colony.y) == r) {
                largest = std::max(largest, colony.population);
            }
        }
        int others = 0;
        for (int k = 0; k < kEuropeanNations; ++k) {
            others += k == n ? 0 : at(kRegionUnits, k, r);
        }
        plan.foreignPeople[ur] = std::max(largest, std::min(others, 4));
    }

    // Each unit free to go takes the place it does best for, by weight,
    // distance and how many are on the way there already.
    const int loadBase = plan.loadBase;
    for (int i = 0; i < unitCount(); ++i) {
        auto& u = save_.units[static_cast<std::size_t>(i)];
        if (u.nation != n || u.raw[kGoal] == 'A' || !onMap(i)) {
            continue;
        }
        if (u.orders < kOrdersThreat) {
            u.raw[kGoal] = '?';
        }
        if (u.orders != SaveUnit::kNoOrders && u.orders != SaveUnit::kFortify && u.orders != SaveUnit::kFortified) {
            continue;
        }
        if (u.raw[kGoal] == 't' || u.raw[kGoal] == 'i') {
            u.raw[kGoal] = '?';
        }
        const int region = landRegion(map, u.x, u.y);
        if (u.type == unit_type::kSoldier || u.type == unit_type::kDragoon) {
            const int mine = at(kRegionUnits, n, region);
            if (mine < 2 || (mine == 2 && at(kRegionColonies, n, region) == 0)) {
                continue;
            }
        }
        const bool ship = shipType(u.type);
        const int roles = units[static_cast<std::size_t>(u.type)].aiRoles;
        const bool fortified = !ship && (u.orders == SaveUnit::kFortify || u.orders == SaveUnit::kFortified);
        int best = INT_MAX;
        int pick = -1;
        const auto& list = plan.targets[un];
        for (std::size_t e = 0; e < list.size(); ++e) {
            const auto& t = list[e];
            if (t.kind < 0 || (roles & (1 << t.kind)) == 0) {
                continue;
            }
            if (!ship && landRegion(map, t.x, t.y) != region) {
                continue;
            }
            if ((t.kind == 1 && (u.raw[kFlags] & 0x04) == 0) || (t.kind == 7 && (u.raw[kFlags] & 0x08) == 0)) {
                continue;
            }
            const int score = plan.load[e] * distanceBetween(u.x, u.y, t.x, t.y) / std::max(1, t.prio + 1);
            if (fortified) {
                if (colonyAt(u.x, u.y) || t.prio <= 2) {
                    continue;
                }
                if (!(t.prio * loadBase >= score || plan.load[e] == loadBase)) {
                    continue;
                }
            }
            if (score < best && (3 * t.prio) / 2 >= score / loadBase) {
                best = score;
                pick = static_cast<int>(e);
            }
        }
        if (pick >= 0) {
            const auto& t = list[static_cast<std::size_t>(pick)];
            aiGoTo(i, t.kind == 1 ? 't' : t.kind == 7 ? 'i' : '1', t.x, t.y);
            if (t.kind != 4) {
                ++plan.load[static_cast<std::size_t>(pick)];
            }
        }
    }
}

// --- The units -------------------------------------------------------------------

void Game::aiMoveUnits(int n) {
    // Rounds: the first unit with moves left among the treasure, artillery
    // and wagon trains (from the last unit down), then the first of any
    // kind; each takes its steps until its moves are gone.
    const auto eligible = [&](int i) {
        if (i < 0 || i >= unitCount()) {
            return false;
        }
        const auto& u = unit(i);
        if (u.nation != n || !onMap(i)) {
            return false;
        }
        if ((u.raw[kFlags] & kDamaged) != 0 && u.type != kArtillery) {
            return false;
        }
        return u.movesUsed < movementAllowance(i);
    };
    const auto run = [&](int i, bool special) {
        const int before = unitCount();
        for (int steps = 0; eligible(i); ++steps) {
            if (steps >= 20) {
                aiEndMoves(i);
                break;
            }
            const int count = unitCount();
            aiUnitStep(n, i);
            if (unitCount() != count) {
                return;
            }
        }
        if (special && unitCount() == before && i < unitCount() && movesLeft(i) <= 0) {
            const int type = unit(i).type;
            aiAddTarget(n, unit(i).x, unit(i).y, 2, type == unit_type::kTreasure ? 2 : type == kArtillery ? 3 : 1);
        }
    };
    for (int rounds = 0; rounds < 4 * kMaxUnits; ++rounds) {
        for (int i = unitCount() - 1; i >= 0; --i) {
            const int type = unit(i).type;
            if ((type == unit_type::kTreasure || type == kArtillery || type == unit_type::kWagonTrain) && eligible(i)) {
                run(i, true);
                break;
            }
        }
        bool any = false;
        for (int i = unitCount() - 1; i >= 0; --i) {
            if (eligible(i)) {
                run(i, false);
                any = true;
                break;
            }
        }
        if (!any) {
            break;
        }
    }
}

bool Game::aiUnitStep(int n, int i) {
    // A unit on its way goes on without thinking again, unless it is a
    // warship with someone next to it.
    auto& u = save_.units[static_cast<std::size_t>(i)];
    if (u.movesUsed != 0 && u.orders == kOrdersGo) {
        const int roles = rules_.units[static_cast<std::size_t>(u.type)].aiRoles;
        if ((roles & 0x01) == 0 || !aiForeignNear(u.x, u.y, n)) {
            aiExecute(n, i);
            return true;
        }
        if (u.raw[kGoal] == 'E') {
            auto& homeward = ai_.homeward[static_cast<std::size_t>(n)];
            homeward = std::max(0, homeward - 1);
        }
    }
    if (aiDecide(n, i) != 0) {
        return false;
    }
    aiExecute(n, i);
    return true;
}

void Game::aiExecute(int n, int i) {
    auto& u = save_.units[static_cast<std::size_t>(i)];
    switch (u.orders) {
    case kOrdersBuild:
        if (foundColony(i, aiColonyName(n)) != FoundResult::Founded) {
            save_.units[static_cast<std::size_t>(i)].orders = SaveUnit::kNoOrders;
            aiEndMoves(i);
        }
        break;
    case SaveUnit::kPlow:
    case SaveUnit::kRoad:
        if (u.workTurns == 0) {
            const auto result = startWork(i, u.orders);
            if (result != WorkResult::Started && result != WorkResult::Finished) {
                save_.units[static_cast<std::size_t>(i)].orders = SaveUnit::kNoOrders;
                aiEndMoves(i);
            }
        } else {
            aiEndMoves(i);
        }
        break;
    case kOrdersGo:
    case kOrdersStep:
        aiGoToStep(n, i);
        break;
    default:
        aiEndMoves(i);
        break;
    }
}

void Game::aiGoToStep(int n, int i) {
    auto& u = save_.units[static_cast<std::size_t>(i)];
    const int orders = u.orders;
    const int dir = routeStep(i, orders == kOrdersStep ? -1 : n);
    if (dir < 0 || dir >= 8) {
        u.orders = SaveUnit::kNoOrders;
        return;
    }
    const int count = unitCount();
    const int toX = u.x + kSteps[static_cast<std::size_t>(dir)].dx;
    const int toY = u.y + kSteps[static_cast<std::size_t>(dir)].dy;
    const auto result = move(i, kSteps[static_cast<std::size_t>(dir)]);
    if (unitCount() != count || result == MoveResult::Lost) {
        return;
    }
    if (result == MoveResult::Occupied && orders == kOrdersStep) {
        // The direction choice steps onto someone else's square only to
        // attack.
        save_.units[static_cast<std::size_t>(i)].orders = SaveUnit::kNoOrders;
        aiAttack(i, toX, toY);
        return;
    }
    if (result == MoveResult::Occupied) {
        // A village the unit was sent to: a scout speaks with the chief, a
        // colonist asks to learn, a missionary founds a mission, a wagon
        // train trades. (As for the player; how the original's computer
        // players do it is not read yet.)
        const auto s = settlementAt(toX, toY);
        auto& v = save_.units[static_cast<std::size_t>(i)];
        v.orders = SaveUnit::kNoOrders;
        if (s && v.raw[kDestX] == toX && v.raw[kDestY] == toY) {
            if (canSpeakWithChief(i, *s)) {
                speakWithChief(i, *s);
            } else if (canLiveAmongNatives(i, *s)) {
                if (liveAmongNatives(i, *s).section == "LEARNSTAY") {
                    learnFromNatives(i, *s);
                }
            } else if (canEstablishMission(i, *s)) {
                establishMission(i, *s);
            } else if (unit(i).type == unit_type::kWagonTrain && canTradeWithVillage(i, *s)) {
                while (unit(i).holds > 0) {
                    const auto sale = offerToVillage(i, *s, 0);
                    if (sale.price <= 0) {
                        break;
                    }
                    sellToVillage(i, *s, sale);
                }
                save_.units[static_cast<std::size_t>(i)].raw[kTrading] = 0;
                aiEndMoves(i);
            } else {
                aiEndMoves(i);
            }
        } else {
            aiEndMoves(i);
        }
        return;
    }
    if (result != MoveResult::Moved && result != MoveResult::HighSeas && result != MoveResult::Boarded) {
        if (result == MoveResult::Blocked) {
            save_.units[static_cast<std::size_t>(i)].orders = SaveUnit::kNoOrders;
        }
        return;
    }
    auto& m = save_.units[static_cast<std::size_t>(i)];
    if (m.x != m.raw[kDestX] || m.y != m.raw[kDestY]) {
        return;
    }
    if (shipType(m.type) && save_.map.at(m.x, m.y).terrain == kSeaLane && orders != kOrdersStep) {
        sailToEurope(i);
        return;
    }
    if (m.type == unit_type::kPioneer) {
        m.raw[kWander] = 0;
        m.raw[kRing] = 0xFF;
    }
    if (orders == kOrdersGo) {
        aiEndMoves(i);
    }
    if (orders != 2 && orders != kOrdersStep) {
        m.orders = SaveUnit::kNoOrders;
    }
}

int Game::aiDecide(int n, int i) {
    const auto& map = save_.map;
    const auto& block = save_.tribesAndMore;
    const auto un = static_cast<std::size_t>(n);
    const auto U = [&]() -> SaveUnit& { return save_.units[static_cast<std::size_t>(i)]; };
    const auto regionByte = [&](std::size_t base, int nation, int region) -> int {
        if (region < 0 || region >= kRegions || block.size() < kBlockEnd) {
            return 0;
        }
        return block[base + kRegions * static_cast<std::size_t>(nation) + static_cast<std::size_t>(region)];
    };
    const auto land = [&](int region) {
        return region < 0 ? 0 : word(save_.tail, kLandSize + 2 * static_cast<std::size_t>(region));
    };
    const auto isHuman = [&](int nation) {
        return nation >= 0 && nation < kEuropeanNations && save_.players[static_cast<std::size_t>(nation)].control == 0;
    };
    const auto colonyHere = [&](int x, int y) { return colonyAt(x, y).value_or(-1); };

    // The end of every decision: a unit left without orders fortifies, and
    // one at its goal stops for the turn.
    const auto finish = [&]() {
        auto& u = U();
        if (u.orders == kOrdersThreat || u.orders == SaveUnit::kNoOrders) {
            u.raw[kGoal] = '0';
            u.orders = SaveUnit::kFortify;
        }
        if (u.orders == SaveUnit::kFortify) {
            for (int d = 0; d < 8; ++d) {
                const int c =
                    colonyHere(u.x + kStepX[static_cast<std::size_t>(d)], u.y + kStepY[static_cast<std::size_t>(d)]);
                if (c >= 0 && (aiRelation(n, save_.colonies[static_cast<std::size_t>(c)].nation) & 0x40) != 0) {
                    u.orders = SaveUnit::kNoOrders;
                }
            }
        }
        if (u.orders == kOrdersGo && u.x == u.raw[kDestX] && u.y == u.raw[kDestY]) {
            if (shipType(u.type) && u.raw[kGoal] == '1') {
                u.raw[kGoal] = 'B';
            }
            aiEndMoves(i);
        }
        return 0;
    };
    const auto goTo = [&](char goal, int x, int y) {
        aiGoTo(i, goal, x, y);
        return finish();
    };
    // A step to a neighbour (or 8: stay and fortify).
    const auto step = [&](int dir) {
        auto& u = U();
        u.raw[kLastStep] = static_cast<std::uint8_t>(dir);
        if (dir == 8) {
            if (u.orders != SaveUnit::kFortify && u.orders != SaveUnit::kFortified) {
                u.orders = SaveUnit::kFortify;
            }
            if ((u.raw[kFlags] & 0x02) != 0) {
                u.orders = SaveUnit::kFortified;
            }
        } else {
            u.orders = kOrdersStep;
            u.raw[kDestX] = static_cast<std::uint8_t>(u.x + kStepX[static_cast<std::size_t>(dir)]);
            u.raw[kDestY] = static_cast<std::uint8_t>(u.y + kStepY[static_cast<std::size_t>(dir)]);
        }
        return finish();
    };
    const auto stay = [&]() { return step(8); };

    {
        const int orders = U().orders;
        if (!(orders == SaveUnit::kNoOrders || orders == SaveUnit::kFortify || orders == SaveUnit::kFortified ||
              orders >= kOrdersThreat)) {
            return finish();
        }
        if (!onMap(i)) {
            U().raw[kGoal] = '@';
            return finish();
        }
    }

    // What the unit sees about it.
    const int X = U().x;
    const int Y = U().y;
    const int type = U().type;
    const auto& ut = rules_.units[static_cast<std::size_t>(type)];
    const int roles = ut.aiRoles;
    const int attack = ut.attack;
    const int capacity = ut.cargoSlots;
    const bool ship = shipType(type);
    const auto ncAny = nearestColonyTo(save_, X, Y, -1, -1);
    const auto ncOwn = nearestColonyTo(save_, X, Y, n, -1);
    const int ncOwnReg = ncOwn.index >= 0 ? landRegion(map, save_.colonies[static_cast<std::size_t>(ncOwn.index)].x,
                                                       save_.colonies[static_cast<std::size_t>(ncOwn.index)].y)
                                          : -2;
    const auto nSet = nearestSettlementTo(save_, X, Y, -1);
    int setTension = 0;
    int setAlarm = 0;
    if (nSet.index >= 0) {
        const auto& s = save_.settlements[static_cast<std::size_t>(nSet.index)];
        setTension = tensionLevel(tribeTension(save_, tribeOf(s), n));
        setAlarm = alarmFor(s, n);
    }
    const auto& square = map.at(X, Y);
    const bool onWater = square.isWater();
    const bool river = square.river();
    const bool townRoad = (square.flags & (MapSquare::kHasColony | MapSquare::kRoad)) != 0;
    const int REG = landRegion(map, X, Y);
    const int status = REG < 0 ? 5 : ai_.status[un][static_cast<std::size_t>(REG)];
    const int foundSc = aiFoundScore(n, i);
    const bool goalTI = U().raw[kGoal] == 't' || U().raw[kGoal] == 'i';
    const bool inOwnColony = ncOwn.index >= 0 && ncOwn.distance == 0;
    const auto terrainOf = [&](int x, int y) -> const TerrainType* {
        const auto row = static_cast<std::size_t>(terrainRow(map.at(x, y)));
        return row < rules_.terrain.size() ? &rules_.terrain[row] : nullptr;
    };

    // Whether it should look for a site for a colony.
    bool wantFound = false;
    if (type == unit_type::kColonist || type == unit_type::kPioneer) {
        wantFound = true;
    }
    if (U().profession == kConvert) {
        wantFound = false;
    }
    if (type == unit_type::kSoldier || type == unit_type::kDragoon) {
        if (U().orders == SaveUnit::kNoOrders) {
            wantFound = true;
        }
        if (U().orders == kOrdersGo && distanceBetween(X, Y, U().raw[kDestX], U().raw[kDestY]) > 12) {
            wantFound = true;
        }
        if (aiRegionScore(n, REG) > 2) {
            wantFound = true;
        }
        if (aiThreatNear(X, Y, n, false) >= 0) {
            wantFound = false;
        }
        if (U().profession == kVeteranSoldier) {
            wantFound = false;
        }
        if (regionByte(kRegionColonies, n, REG) == 0 && regionByte(kRegionAttack, n, REG) < 8) {
            wantFound = true;
        }
        if (type == unit_type::kDragoon && REG >= 0 && block.size() >= kBlockEnd &&
            (block[kRegionFlags + static_cast<std::size_t>(REG)] & 0x04) != 0) {
            wantFound = false;
        }
    }
    if (type == unit_type::kScout) {
        if (U().raw[kGoal] == '2') {
            wantFound = true;
        }
        if (status == 0) {
            wantFound = false;
        }
        if (regionByte(kRegionColonies, n, REG) == 0 && save_.turn % 15 == 0) {
            wantFound = true;
        }
        if (ncOwn.distance > 12 && ncAny.distance > 2) {
            wantFound = true;
        }
        if (aiThreatNear(X, Y, n, false) >= 0 || save_.year > 1650) {
            wantFound = false;
        }
    }
    if (!ship && attack > 0 && signedByte(U().raw[kHome]) < 0 && ncOwn.distance <= 8 && ncOwnReg == REG) {
        U().raw[kHome] = static_cast<std::uint8_t>(ncOwn.index);
    }
    wantFound = wantFound && foundSc != 0;
    if (type == unit_type::kColonist && inOwnColony &&
        (save_.colonies[static_cast<std::size_t>(ncOwn.index)].raw[kThreat] & 0x10) != 0) {
        wantFound = false;
    }
    if (wantFound && REG >= 0) {
        if (++ai_.founders[static_cast<std::size_t>(REG)] > (type == unit_type::kColonist ? 2 : 3)) {
            wantFound = false;
        }
    }

    // Ships: which may carry and which may take people aboard.
    const auto counts = [&](int t) -> int {
        return block.size() >= kBlockEnd ? block[kByType + kTypesCounted * un + static_cast<std::size_t>(t)] : 0;
    };
    bool shipOK = true;
    if (type == kManOWar) {
        shipOK = false;
    }
    if (type == kFrigate) {
        const int holds = block.size() >= kBlockEnd ? block[kHoldsAll + un] : 0;
        if (holds - 3 * counts(kFrigate) - counts(kPrivateer) >= 4 || aiForeignNear(X, Y, n)) {
            shipOK = false;
        }
    }
    if (type == kPrivateer) {
        shipOK = ai_.frigateThreat > 1 || ai_.frigatePeople > 6;
    }
    bool boardOK = shipOK;
    if (type == kManOWar) {
        boardOK = (i & 1) != 0 || counts(kManOWar) == 1;
    }

    // A guard stays in its colony.
    bool garrison = false;
    if (!ship && attack > 0 && type != unit_type::kDragoon && type != 8 && inOwnColony) {
        auto& colony = save_.colonies[static_cast<std::size_t>(ncOwn.index)];
        if (signedByte(colony.raw[kGarrison]) > 0 || U().raw[kGoal] == 'A') {
            int guards = 0;
            for (const int j : unitsAt(X, Y)) {
                const auto& o = unit(j);
                guards += !shipType(o.type) && rules_.units[static_cast<std::size_t>(o.type)].attack > 0 &&
                                  o.type != unit_type::kDragoon && o.type != 8
                              ? 1
                              : 0;
            }
            if (U().raw[kGoal] == 'A') {
                return stay();
            }
            if (guards <= 1) {
                --colony.raw[kGarrison];
                U().raw[kGoal] = 'G';
                return stay();
            }
            garrison = true;
        }
    }

    if (!garrison) {
        // Back home from a crowded region.
        if (status == 0 && !goalTI && !ship && (type == unit_type::kPioneer || attack > 0) && ncOwnReg == REG &&
            ncOwn.index >= 0) {
            const auto& colony = save_.colonies[static_cast<std::size_t>(ncOwn.index)];
            if (!inOwnColony) {
                return goTo('V', colony.x, colony.y);
            }
            U().raw[kGoal] = 'V';
            return stay();
        }
        // Next to a village: the scout to its chief, a colonist to learn.
        if (nSet.index >= 0 && nSet.distance == 1) {
            const auto& s = save_.settlements[static_cast<std::size_t>(nSet.index)];
            const int dir = [&] {
                for (int d = 0; d < 8; ++d) {
                    if (X + kStepX[static_cast<std::size_t>(d)] == s.x &&
                        Y + kStepY[static_cast<std::size_t>(d)] == s.y) {
                        return d;
                    }
                }
                return 8;
            }();
            if (type == unit_type::kScout && (s.raw[3] & kSettlementVisited) == 0 && setTension < 25 && setAlarm == 0 &&
                dir < 8) {
                U().raw[kGoal] = 'L';
                aiGoTo(i, 'L', s.x, s.y);
                U().orders = kOrdersStep;
                U().raw[kLastStep] = static_cast<std::uint8_t>(dir);
                return finish();
            }
            const int prof = U().profession;
            if (personType(type) && attack == 0 && type != unit_type::kScout && type != unit_type::kMissionary &&
                (prof == 0x1C || prof == 0x19) && (s.raw[3] & kSettlementTaught) == 0 && setTension < 25 &&
                setAlarm < 0x40 && dir < 8) {
                aiGoTo(i, 'L', s.x, s.y);
                U().orders = kOrdersStep;
                U().raw[kLastStep] = static_cast<std::uint8_t>(dir);
                return finish();
            }
        }
    }

    // The search for a site.
    const auto siteSearch = [&]() -> int {
        auto& u = U();
        if (u.raw[kWander] != 0) {
            --u.raw[kWander];
            return -1;
        }
        u.raw[kRing] = 0xFF;
        if (foundSc != 0 && u.raw[kSearches] < 0x7F) {
            ++u.raw[kSearches];
        }
        const int regionIndex = std::clamp(REG, 0, kRegions - 1);
        const int pattern = 8 * ai_.foreignPeople[static_cast<std::size_t>(regionIndex)] + u.raw[kSearches];
        const int farmland = REG < 0 ? 0 : word(save_.tail, kFarmland + 2 * static_cast<std::size_t>(REG));
        const int shift = farmland <= 8 ? 0 : farmland <= 24 ? 1 : farmland <= 48 ? 2 : 3;
        const int radius = pattern >= 64 ? 1 : pattern >= 32 ? 2 : 3;
        const int ownHere = regionByte(kRegionColonies, n, REG);
        int best = -999;
        int bestVal = 0;
        int bx = X;
        int by = Y;
        for (int y = Y - radius; y <= Y + radius; ++y) {
            for (int x = X - radius; x <= X + radius; ++x) {
                if (!map.inside(x, y) || landRegion(map, x, y) != REG || map.at(x, y).mountains()) {
                    continue;
                }
                const auto there = unitsAt(x, y);
                if (!there.empty() && unit(there.front()).nation != n) {
                    continue;
                }
                if (colonyAt(x, y) || settlementAt(x, y)) {
                    continue;
                }
                const int val = map.at(x, y).seen & 0x0F;
                int s = 4 * val + (x == 0 && y == 0 ? 16 : 0);
                bool coast = false;
                for (int d = 0; d < 8 && !coast; ++d) {
                    coast = waterOfSea(map, x + kStepX[static_cast<std::size_t>(d)],
                                       y + kStepY[static_cast<std::size_t>(d)]);
                }
                if (!coast) {
                    continue;
                }
                const auto near = nearestColonyTo(save_, x, y, -1, REG);
                if (near.index >= 0) {
                    const int d = near.distance;
                    if (d <= 1) {
                        continue;
                    }
                    if (save_.colonies[static_cast<std::size_t>(near.index)].nation != n) {
                        if (d == 2) {
                            s -= 20;
                        }
                        const int lim = ownHere >= 1 ? 7 : 5;
                        if (d < lim) {
                            s -= (lim - d) * (lim - d);
                        }
                    } else {
                        if (d == 2) {
                            continue;
                        }
                        if (d < 9) {
                            s -= (9 - d) * (9 - d);
                        }
                    }
                }
                bool building = false;
                for (int dy = -1; dy <= 1 && !building; ++dy) {
                    for (int dx = -1; dx <= 1 && !building; ++dx) {
                        if (!map.inside(x + dx, y + dy)) {
                            continue;
                        }
                        for (const int j : unitsAt(x + dx, y + dy)) {
                            building = building || (j != i && unit(j).orders == kOrdersBuild);
                        }
                    }
                }
                if (building) {
                    continue;
                }
                const auto ns = nearestSettlementTo(save_, x, y, -1);
                if (ns.index >= 0) {
                    const auto& st = save_.settlements[static_cast<std::size_t>(ns.index)];
                    const int sreg = landRegion(map, st.x, st.y);
                    const int tribe = tribeOf(st);
                    int d = ns.distance + (regionByte(kRegionColonies, n, sreg) == 0 ? 1 : 0);
                    if (d < 6) {
                        int p = 2 * (tribeLevel(save_, tribe) + tensionLevel(tribeTension(save_, tribe, n)) + 3);
                        if (sreg != REG) {
                            p /= 2;
                        }
                        int P = p / 2;
                        P += d <= 4 ? p : 0;
                        P += d <= 3 ? 2 * p : 0;
                        P += d <= 2 ? 4 * p : 0;
                        P += d <= 1 ? 8 * p : 0;
                        if ((st.raw[3] & kSettlementCapital) != 0) {
                            P *= 2;
                        }
                        if (n == 1) {
                            P /= 2;
                        }
                        if (hasFather(n, 16)) {
                            P /= 2;
                        }
                        if (n == 2) {
                            P /= 4;
                        }
                        if (pattern > 40) {
                            P /= 2;
                        }
                        P -= regionByte(kRegionAttack, n, sreg);
                        s -= std::max(0, P);
                    }
                }
                if (val >= 4) {
                    if (ownHere == 0) {
                        s += s / 2;
                    }
                    if (type == unit_type::kColonist) {
                        s *= 2;
                    }
                    s += pattern >> shift;
                }
                if (s >= best) {
                    best = s;
                    bestVal = val;
                    bx = x;
                    by = y;
                }
            }
        }
        if (bestVal > 0) {
            if (bx == X && by == Y) {
                U().orders = kOrdersBuild;
                return finish();
            }
            return goTo('2', bx, by);
        }
        return -1;
    };
    if (!garrison && wantFound) {
        const int r = siteSearch();
        if (r >= 0) {
            return r;
        }
    }

    // A colonist joins a colony that wants people.
    if (!garrison && type == unit_type::kColonist && !wantFound) {
        int best = INT_MAX;
        int pick = -1;
        for (std::size_t c = 0; c < save_.colonies.size(); ++c) {
            const auto& colony = save_.colonies[c];
            if (colony.nation != n || landRegion(map, colony.x, colony.y) != REG) {
                continue;
            }
            if ((colony.raw[kThreat] & 0x10) == 0 && U().profession != kConvert) {
                continue;
            }
            const int need = std::min(16, colonyTarget(colony));
            int s = distanceBetween(X, Y, colony.x, colony.y) / 2;
            if (need - colony.population > 0) {
                s *= need - colony.population;
            }
            if (colony.population >= need) {
                s *= 2;
            }
            const auto there = unitsAt(colony.x, colony.y);
            const int guards = there.empty() ? 0 : aiStack(there.front(), 2);
            if (guards + colony.population >= need + 2) {
                continue;
            }
            if (s < best) {
                best = s;
                pick = static_cast<int>(c);
            }
        }
        if (pick >= 0) {
            const auto& colony = save_.colonies[static_cast<std::size_t>(pick)];
            if (colony.x == X && colony.y == Y) {
                aiEndMoves(i);
                aiJoinColony(i, pick);
                return 1;
            }
            return goTo('3', colony.x, colony.y);
        }
        if (inOwnColony) {
            auto& u = U();
            u.raw[kGoal] = '=';
            u.type = unit_type::kPioneer;
            u.tools = 20;
            aiEndMoves(i);
            return finish();
        }
        wantFound = true;
        const int r = siteSearch();
        if (r >= 0) {
            return r;
        }
    }

    // Carriers in a colony of their own: unload, take people aboard, load.
    if (!garrison && capacity > 0 && inOwnColony && (ship || signedByte(U().raw[kHome]) == ncOwn.index)) {
        auto& colony = save_.colonies[static_cast<std::size_t>(ncOwn.index)];
        for (const int j : unitsAt(X, Y)) {
            if (save_.units[static_cast<std::size_t>(j)].orders == SaveUnit::kSentry && j != i) {
                save_.units[static_cast<std::size_t>(j)].orders = SaveUnit::kNoOrders;
            }
        }
        {
            auto& u = U();
            for (int h = 0; h < u.holds; ++h) {
                const int g = holdGood(u, h);
                if (g >= 0 && g < kGoodsKinds) {
                    colony.stock[static_cast<std::size_t>(g)] += holdAmount(u, h);
                }
                setHold(u, h, 0, 0);
            }
            u.holds = 0;
            if (ship) {
                u.raw[kHome] = 0xFF;
                colony.raw[kPickupBonus] = 0;
            }
        }
        int free = capacity - U().holds;
        if (type == unit_type::kWagonTrain) {
            free = std::min(free, 1);
        }
        if (ship && (colony.raw[kThreat] & 0x02) != 0 && type < kGalleon) {
            ++U().workTurns;
            if (10 - capacity > U().workTurns) {
                U().raw[kGoal] = 'C';
                return stay();
            }
        }
        if (ship && boardOK && (U().raw[kFlags] & 0x20) == 0) {
            auto& calls = ai_.pickupCalls[un];
            if (calls < 25) {
                for (const int j : unitsAt(X, Y)) {
                    const auto& o = unit(j);
                    if (j == i || shipType(o.type) || o.nation != n) {
                        continue;
                    }
                    const auto& ot = rules_.units[static_cast<std::size_t>(o.type)];
                    if (ot.size > free) {
                        continue;
                    }
                    const bool armedOne = ot.attack > 0 && o.raw[kGoal] != 'G' && o.raw[kGoal] != 'A' && status == 0;
                    const bool pioneer = o.type == unit_type::kPioneer && (foundSc != 0 || status == 0);
                    if (armedOne || pioneer) {
                        aiBoard(j, i);
                        free -= ot.size;
                    }
                }
            }
            calls = 0;
        }
        const auto& nation = save_.nations[un];
        const int floor = 100 * (colony.raw[kWarehouseLevel] + 1);
        while (free > 0 && (shipOK || type == unit_type::kWagonTrain)) {
            int bestScore = -1;
            int pick = -1;
            for (int g = 0; g < kGoodsKinds; ++g) {
                int a = colony.stock[static_cast<std::size_t>(g)];
                if (a < floor) {
                    if (g == kHorsesGood) {
                        a = std::max(0, a + 25 - floor - 2);
                    }
                } else if (g != 0) {
                    a *= 2;
                }
                if (g == 5) {
                    continue;
                }
                if (g == 14 || g == 15) {
                    if (!ship) {
                        continue;
                    }
                    if ((colonyWord(colony, kExports) & (1 << g)) == 0) {
                        continue;
                    }
                    a -= 100;
                }
                if (ship && (g == 13 || g == 0)) {
                    continue;
                }
                if (a <= 0) {
                    continue;
                }
                int s = 0;
                if (ship) {
                    s = bidPrice(nation, g) * a;
                } else {
                    int p = bidPrice(nation, g);
                    while (p >= 2 && aiRandom(0, 3) == 0) {
                        --p;
                    }
                    const int lim = g == 13 ? 8 : 4;
                    s = lim > p ? a * (lim - p) + 5 * (1 - p) : -1;
                    if (a < 50) {
                        s = -1;
                    }
                }
                if (s > bestScore) {
                    bestScore = s;
                    pick = g;
                }
            }
            if (pick < 0) {
                break;
            }
            auto& stock = colony.stock[static_cast<std::size_t>(pick)];
            const int amount = std::min(100, stock);
            stock -= amount;
            auto& u = U();
            setHold(u, u.holds, pick, amount);
            ++u.holds;
            --free;
            if (ship) {
                u.raw[kHome] = static_cast<std::uint8_t>(ncOwn.index);
            } else {
                u.raw[kTrading] = 1;
            }
        }
    }

    // Ships.
    int mask = 0;
    int mil = 0;
    int pio = 0;
    if (!garrison && ship) {
        std::array<int, kGoodsKinds> goods{};
        int maxKind = -1;
        {
            const auto& u = U();
            for (int h = 0; h < u.holds; ++h) {
                const int g = holdGood(u, h);
                if (g == kHorsesGood || (g >= 13 && g < kGoodsKinds)) {
                    goods[static_cast<std::size_t>(g)] += holdAmount(u, h);
                    maxKind = std::max(maxKind, g);
                }
            }
        }
        const int others = aiStack(i, 2) - 1;
        pio = aiStack(i, 3);
        mil = aiStack(i, 4);
        const int mil6 = aiStack(i, 6);
        const int sco = aiStack(i, 5);
        int rest = others - sco - mil - pio;
        const int here7 = aiTargetPrio(n, X, Y, 7);
        const int here1 = aiTargetPrio(n, X, Y, 1);
        int person = -1;
        for (const int j : unitsAt(X, Y)) {
            const int t = unit(j).type;
            if ((rules_.units[static_cast<std::size_t>(t)].aiRoles & 0x40) != 0 &&
                (person < 0 || t < unit(person).type)) {
                person = j;
            }
        }
        if (person >= 0) {
            if (aiFoundBias(n, person, -1) + aiColonyNeed(n) > 0) {
                pio += rest;
                rest = 0;
            } else if (here1 == 0) {
                rest += pio;
                pio = 0;
            }
        }
        bool skipToCargo = others == 0;
        bool goalTIShip = goalTI;
        if (!skipToCargo) {
            if (pio != 0 || mil != 0 || sco != 0) {
                for (int d = 0; d < 8; ++d) {
                    const int sx = X + kStepX[static_cast<std::size_t>(d)];
                    const int sy = Y + kStepY[static_cast<std::size_t>(d)];
                    if (!map.inside(sx, sy) || map.at(sx, sy).isWater()) {
                        continue;
                    }
                    const auto there = unitsAt(sx, sy);
                    if ((!there.empty() && unit(there.front()).nation != n) || settlementAt(sx, sy) ||
                        (colonyAt(sx, sy) && save_.colonies[static_cast<std::size_t>(*colonyAt(sx, sy))].nation != n)) {
                        continue;
                    }
                    const int r = landRegion(map, sx, sy);
                    if (r < 0 || ai_.status[un][static_cast<std::size_t>(r)] == 0 || sy < 2 || sy > map.height - 3) {
                        continue;
                    }
                    mask = 0;
                    const auto& u = U();
                    if (u.orders == kOrdersGo && landRegion(map, u.raw[kDestX], u.raw[kDestY]) == r) {
                        mask = 0xFFFF;
                    }
                    const int units = regionByte(kRegionUnits, n, r);
                    const int colonies = regionByte(kRegionColonies, n, r);
                    if (sco != 0 && ((colonies == 0 && land(r) > 10) || units < land(r) / 8)) {
                        mask |= 0x20;
                    }
                    if (pio != 0) {
                        if (aiRegionScore(n, r) > 0 || units == 0) {
                            mask |= 0x40;
                        }
                        bool near = true;
                        if (colonies != 0) {
                            if (8 - colonies > ncOwn.distance) {
                                mask &= ~0x40;
                            }
                            if (ncOwn.distance >= 12) {
                                near = false;
                            }
                        }
                        if (near && ai_.openRegions != 0 && 4 * colonies + units > save_.turn >> 4 &&
                            ai_.pickupCalls[un] < 20) {
                            mask &= ~0x40;
                        }
                        if (wantFound && ai_.founders[static_cast<std::size_t>(r)] > 1) {
                            mask &= ~0x40;
                        }
                        if (here7 != 0 || here1 != 0 || ((ai_.emptyRegions >> r) & 1) != 0) {
                            mask |= 0x40;
                        }
                    }
                    if (mil != 0) {
                        if (ai_.status[un][static_cast<std::size_t>(r)] == 4) {
                            mask |= 0x10;
                        }
                        if (mil6 == 0 && ai_.openRegions != 0 && 4 * colonies + units > save_.turn >> 4 &&
                            ai_.pickupCalls[un] < 20) {
                            mask &= ~0x10;
                        }
                        const int flags =
                            block.size() >= kBlockEnd ? block[kRegionFlags + static_cast<std::size_t>(r)] : 0;
                        if (colonies == 0 && (flags & 0x04) != 0 && ncAny.distance < 7) {
                            mask |= 0x10;
                        }
                        if ((flags & 0x08) != 0) {
                            mask |= 0x10;
                        }
                        if (here7 != 0 || here1 != 0 || ((ai_.biggerRegions >> r) & 1) != 0) {
                            mask |= 0x10;
                        }
                    }
                }
            }
            if (here1 != 0 || here7 != 0) {
                goalTIShip = false;
            }
            // Those aboard whose work lies ashore go ashore.
            bool anyMoved = false;
            for (bool again = true; again && mask != 0;) {
                again = false;
                for (const int j : unitsAt(X, Y)) {
                    if (shipType(unit(j).type)) {
                        continue;
                    }
                    const int dir = aiStepAround(n, X, Y, (mask & 0x40) != 0, unit(j).type == kArtillery);
                    if (dir == 8) {
                        break;
                    }
                    if ((rules_.units[static_cast<std::size_t>(unit(j).type)].aiRoles & mask) == 0) {
                        continue;
                    }
                    save_.units[static_cast<std::size_t>(j)].movesUsed = 0;
                    const int count = unitCount();
                    move(j, kSteps[static_cast<std::size_t>(dir)]);
                    if (unitCount() != count) {
                        return 1;
                    }
                    aiEndMoves(j);
                    anyMoved = true;
                    if (unit(j).x != X || unit(j).y != Y) {
                        again = true;
                        break;
                    }
                }
            }
            if (anyMoved) {
                aiEndMoves(i);
            }
            // The passengers to a colony of the nation.
            const bool skip = rest == 0 && ((pio == others && ai_.pickupCalls[un] < 25) || mask != 0);
            if (!goalTIShip && !skip) {
                int best = -9999;
                int tx = -1;
                int ty = -1;
                for (std::size_t c = 0; c < save_.colonies.size(); ++c) {
                    const auto& C = save_.colonies[c];
                    if (C.nation != n || static_cast<int>(c) == (inOwnColony ? ncOwn.index : -1)) {
                        continue;
                    }
                    const int reg = landRegion(map, C.x, C.y);
                    const int st = reg < 0 ? 0 : ai_.status[un][static_cast<std::size_t>(reg)];
                    if (pio != 0 && st == 0) {
                        continue;
                    }
                    int need = colonyTarget(C);
                    if (need > 12) {
                        need = 16;
                    }
                    bool humanThere = false;
                    for (const auto& o : save_.colonies) {
                        humanThere = humanThere || (isHuman(o.nation) && landRegion(map, o.x, o.y) == reg);
                    }
                    int s = 0;
                    if (mil == 0) {
                        const int pop = std::min(C.population, 16);
                        s = aiRandom(0, 8) + 4 * ((17 - pop) * (17 - pop) + 2) + 2 * (need - C.population) +
                            (humanThere ? 20 : 0) + ((C.raw[kThreat] & 0x10) != 0 ? 25 : -25);
                    } else {
                        if (st == 0) {
                            continue;
                        }
                        const int flags = block.size() >= kBlockEnd && reg >= 0
                                              ? block[kRegionFlags + static_cast<std::size_t>(reg)]
                                              : 0;
                        s = 8 * (flags & 7);
                        if (ai_.fortifiedRegions != 0 && mil > 1) {
                            s -= 8 * ai_.fortifiedRegions * mil;
                        }
                        if (humanThere) {
                            s += 50;
                        }
                        if ((C.raw[kThreat] & 0x40) != 0) {
                            s += 60;
                        } else if (ai_.openRegions > 1 && ai_.pickupCalls[un] < 20) {
                            s -= 45;
                        } else if ((C.raw[kThreat] & 0x08) != 0) {
                            s += 45;
                        } else {
                            s -= 15;
                        }
                    }
                    s += C.raw[kPickupBonus];
                    if ((C.raw[kThreat] & 0x02) != 0) {
                        if (type != kFrigate) {
                            s -= 50;
                        }
                    } else if ((C.raw[kThreat] & 0x01) != 0 && type < kFrigate) {
                        s += 2 * (ut.combat - 10);
                    }
                    int cx = C.x;
                    int cy = C.y;
                    if ((C.raw[kMarks] & kCoastal) == 0) {
                        const auto port = nearestColonyTo(save_, C.x, C.y, n, -2);
                        if (port.index < 0) {
                            continue;
                        }
                        cx = save_.colonies[static_cast<std::size_t>(port.index)].x;
                        cy = save_.colonies[static_cast<std::size_t>(port.index)].y;
                    }
                    s -= distanceBetween(X, Y, cx, cy) / 2 + 1;
                    if (s >= best) {
                        best = s;
                        tx = cx;
                        ty = cy;
                    }
                }
                if (tx >= 0 && best > (mil >= 1 ? 0 : -999)) {
                    return goTo('4', tx, ty);
                }
            }
        }
        // Cargo: deliver, fetch, or go home.
        const auto& u = U();
        const bool empty = u.holds == 0 && others == 0;
        if (empty && maxKind < 0 && boardOK && !goalTIShip && ai_.inPort[un] > ai_.homeward[un]) {
            aiToEurope(i);
            return finish();
        }
        if (u.holds > 0 && !goalTIShip && maxKind >= 0) {
            int best = -1;
            int pick = -1;
            for (std::size_t c = 0; c < save_.colonies.size(); ++c) {
                const auto& C = save_.colonies[c];
                if (C.nation != n || (C.raw[kMarks] & kCoastal) == 0) {
                    continue;
                }
                if ((inOwnColony && static_cast<int>(c) == ncOwn.index) ||
                    static_cast<int>(c) == signedByte(u.raw[kHome])) {
                    continue;
                }
                const int floor = 100 * (C.raw[kWarehouseLevel] + 1);
                if (maxKind == kHorsesGood && goods[kHorsesGood] + C.stock[kHorsesGood] > floor) {
                    continue;
                }
                int s = 0;
                bool excluded = false;
                for (int g = 0; g < kGoodsKinds && !excluded; ++g) {
                    const int carried = goods[static_cast<std::size_t>(g)];
                    if (carried <= 0) {
                        continue;
                    }
                    const int stock = C.stock[static_cast<std::size_t>(g)];
                    if ((colonyWord(C, kExports) & (1 << g)) != 0 && stock >= 100) {
                        excluded = true;
                        break;
                    }
                    if (carried + stock >= floor) {
                        s += 4 * bidPrice(save_.nations[un], g) * (floor - stock - carried);
                    }
                    if (signedByte(C.raw[kWantedGood]) == g) {
                        s += 4 * (C.raw[kPickupBonus] + 8);
                    }
                    s += floor - stock - 1;
                }
                if (excluded) {
                    continue;
                }
                const int reg = landRegion(map, C.x, C.y);
                if (maxKind == 15) {
                    for (const auto& o : save_.colonies) {
                        if (isHuman(o.nation) && landRegion(map, o.x, o.y) == reg) {
                            s += 16;
                            break;
                        }
                    }
                    for (std::size_t k = 0; k < kRingX.size(); ++k) {
                        const int sx = C.x + kRingX[k];
                        const int sy = C.y + kRingY[k];
                        if (!map.inside(sx, sy)) {
                            continue;
                        }
                        int owner = -1;
                        if (const auto sc = settlementAt(sx, sy)) {
                            owner = save_.settlements[static_cast<std::size_t>(*sc)].nation;
                        } else if (const auto cc = colonyAt(sx, sy)) {
                            owner = save_.colonies[static_cast<std::size_t>(*cc)].nation;
                        } else if (const auto there = unitsAt(sx, sy); !there.empty()) {
                            owner = unit(there.front()).nation;
                        }
                        if (owner < 0 || owner == n) {
                            continue;
                        }
                        s += owner >= kEuropeanNations
                                 ? 16 * tensionLevel(tribeTension(save_, owner - kEuropeanNations, n))
                                 : 24;
                    }
                }
                if ((C.raw[kThreat] & 0x02) != 0 && type < kPrivateer) {
                    s += 8 * (ut.combat - 10);
                } else if ((C.raw[kThreat] & 0x01) != 0 && type < kPrivateer) {
                    s += 2 * (ut.combat - 10);
                }
                s /= distanceBetween(X, Y, C.x, C.y) / 4 + 1;
                if (s >= best) {
                    best = s;
                    pick = static_cast<int>(c);
                }
            }
            if (pick >= 0) {
                const auto& C = save_.colonies[static_cast<std::size_t>(pick)];
                return goTo('P', C.x, C.y);
            }
            // Nobody wants it: sold where it is.
            auto& nation = save_.nations[un];
            auto& v = U();
            for (int h = 0; h < v.holds; ++h) {
                const int g = holdGood(v, h);
                const int amount = holdAmount(v, h);
                if (g >= 0 && g < kGoodsKinds && amount > 0) {
                    nation.gold += bidPrice(nation, g) * amount;
                    aiMarketBack(n, g, amount);
                }
                setHold(v, h, 0, 0);
            }
            v.holds = 0;
        }
        if (U().holds == 0 && others == 0 && !goalTIShip && (shipOK || boardOK)) {
            int best = -1;
            int pick = -1;
            for (std::size_t e = 0; e < ai_.pickups.size(); ++e) {
                const auto& p = ai_.pickups[e];
                if (p.colony < 0 || p.loads <= 0 || p.colony == signedByte(U().raw[kHome]) ||
                    static_cast<std::size_t>(p.colony) >= save_.colonies.size()) {
                    continue;
                }
                const auto& C = save_.colonies[static_cast<std::size_t>(p.colony)];
                if ((C.raw[kThreat] & 0x02) != 0 && type < kPrivateer) {
                    continue;
                }
                const int s = p.value / (distanceBetween(X, Y, C.x, C.y) / 4 + 1);
                if (s >= best && (shipOK || p.military != 0)) {
                    best = s;
                    pick = static_cast<int>(e);
                }
            }
            if (pick >= 0) {
                auto& p = ai_.pickups[static_cast<std::size_t>(pick)];
                const auto& C = save_.colonies[static_cast<std::size_t>(p.colony)];
                const int tx = C.x;
                const int ty = C.y;
                const int left = std::max(0, p.loads - (capacity - U().holds));
                p.value = left * p.value / p.loads;
                p.loads = left;
                if (left == 0) {
                    p.colony = -1;
                }
                return goTo('5', tx, ty);
            }
        }
        if (goalTIShip) {
            return finish();
        }
        if (U().holds == 0 && others == 0 && boardOK) {
            if ((U().raw[kFlags] & 0x20) != 0 || ((i + save_.turn) & 31) == 0) {
                aiToEurope(i);
                return finish();
            }
        }
    }

    if (!garrison && !ship) {
        // Wagon trains.
        if (type == unit_type::kWagonTrain) {
            if (U().raw[kTrading] != 0) {
                int best = INT_MAX;
                int pick = -1;
                for (std::size_t s = 0; s < save_.settlements.size(); ++s) {
                    const auto& st = save_.settlements[s];
                    if (landRegion(map, st.x, st.y) != REG) {
                        continue;
                    }
                    int d = distanceBetween(X, Y, st.x, st.y);
                    if ((st.raw[3] & kSettlementCapital) != 0) {
                        d = std::max(1, d / 2);
                    }
                    if (d < best) {
                        best = d;
                        pick = static_cast<int>(s);
                    }
                }
                if (pick >= 0) {
                    const auto& st = save_.settlements[static_cast<std::size_t>(pick)];
                    return goTo('4', st.x, st.y);
                }
                removeUnit(i);
                refreshUnitFlags();
                return 1;
            }
            if (U().holds == 0) {
                if (inOwnColony) {
                    if (signedByte(U().raw[kHome]) < 0) {
                        U().raw[kHome] = static_cast<std::uint8_t>(ncOwn.index);
                    }
                    if (signedByte(U().raw[kHome]) == ncOwn.index) {
                        U().raw[kGoal] = 'U';
                        return stay();
                    }
                } else {
                    const int home = signedByte(U().raw[kHome]);
                    if (home >= 0 && static_cast<std::size_t>(home) < save_.colonies.size()) {
                        const auto& C = save_.colonies[static_cast<std::size_t>(home)];
                        return goTo('5', C.x, C.y);
                    }
                    if (ncOwnReg == REG && ncOwn.index >= 0) {
                        U().raw[kHome] = static_cast<std::uint8_t>(ncOwn.index);
                        const auto& C = save_.colonies[static_cast<std::size_t>(ncOwn.index)];
                        return goTo('5', C.x, C.y);
                    }
                    removeUnit(i);
                    refreshUnitFlags();
                    return 1;
                }
            }
        }
        // Treasure goes home to the Crown.
        if (type == unit_type::kTreasure) {
            if (inOwnColony) {
                const int gold = 100 * U().profession;
                save_.nations[un].gold += gold;
                GameNotice notice{"LOOTFOREIGN", {"", ""}, -1, {gold}};
                notice.adjectives = {{0, n}};
                notice.ports = {{1, n}};
                notices_.push_back(notice);
                removeUnit(i);
                refreshUnitFlags();
                return 1;
            }
            if (ncOwnReg == REG && ncOwn.index >= 0) {
                U().raw[kHome] = static_cast<std::uint8_t>(ncOwn.index);
                const auto& C = save_.colonies[static_cast<std::size_t>(ncOwn.index)];
                return goTo('5', C.x, C.y);
            }
            int best = INT_MAX;
            int pick = -1;
            for (int j = 0; j < unitCount(); ++j) {
                const auto& o = unit(j);
                if (j == i || o.nation != n || !onMap(j)) {
                    continue;
                }
                const int d = distanceBetween(X, Y, o.x, o.y);
                if (d < best) {
                    best = d;
                    pick = j;
                }
            }
            if (pick >= 0 && landRegion(map, unit(pick).x, unit(pick).y) == REG) {
                return goTo('!', unit(pick).x, unit(pick).y);
            }
            if (aiThreatNear(X, Y, n, false) == human()) {
                removeUnit(i);
                refreshUnitFlags();
                return 1;
            }
        }
        // Missionaries look for a village to work in.
        if (type == unit_type::kMissionary) {
            int best = -999;
            int pick = -1;
            const int h = human();
            for (std::size_t s = 0; s < save_.settlements.size(); ++s) {
                const auto& st = save_.settlements[s];
                if (landRegion(map, st.x, st.y) != REG) {
                    continue;
                }
                const int tribe = tribeOf(st);
                if (missionOf(st) == n) {
                    // A village with a mission of its own is worth another
                    // only against a player who is ahead; how the original
                    // ranks the nations is not read yet, so it is passed by.
                    (void)h;
                    continue;
                }
                int s2 = 8 * tribeTension(save_, tribe, n) / (distanceBetween(X, Y, st.x, st.y) + 1);
                if ((st.raw[3] & kSettlementCapital) != 0) {
                    s2 += s2 / 2;
                }
                if (s2 > best) {
                    best = s2;
                    pick = static_cast<int>(s);
                }
            }
            if (pick >= 0) {
                const auto& st = save_.settlements[static_cast<std::size_t>(pick)];
                return goTo('J', st.x, st.y);
            }
            U().type = unit_type::kColonist;
        }
        // Scouts and pioneers in a crowded region go home.
        if ((type == unit_type::kScout || type == unit_type::kPioneer) && status == 0 && REG == ncOwnReg &&
            ncOwn.index >= 0) {
            const auto& C = save_.colonies[static_cast<std::size_t>(ncOwn.index)];
            return goTo('N', C.x, C.y);
        }
        // A wander towards one of 20 squares four away.
        if (wantFound) {
            auto& u = U();
            if (u.orders == kOrdersGo) {
                return finish();
            }
            if (signedByte(u.raw[kRing]) < 0) {
                u.raw[kRing] = static_cast<std::uint8_t>(aiRandom(1, 20) - 1);
            }
            int tx = 0;
            int ty = 0;
            if (u.raw[kGoal] == '8' && u.raw[kWander] != 0) {
                tx = u.raw[kDestX];
                ty = u.raw[kDestY];
            } else {
                const auto k = static_cast<std::size_t>(u.raw[kRing] % 20);
                tx = X + 4 * kRingX[k];
                ty = Y + 4 * kRingY[k];
            }
            const std::size_t cell = static_cast<std::size_t>((tx >> 2) * 18 + (ty >> 2));
            if (map.inside(tx, ty) && (cell >= ai_.blocks.size() || (ai_.blocks[cell] & 6) == 0) &&
                landRegion(map, tx, ty) == REG && unitsAt(tx, ty).empty()) {
                const int rx = std::abs(tx - X);
                const int ry = std::abs(ty - Y);
                u.raw[kWander] = static_cast<std::uint8_t>(std::max(4 * rx, 4 * ry));
                if (u.raw[kSearches] > 8) {
                    u.raw[kSearches] = static_cast<std::uint8_t>(u.raw[kSearches] - 8);
                }
                return goTo('8', tx, ty);
            }
        }
        // A soldier called by its colony goes back to it.
        if (attack > 0 && signedByte(U().raw[kHome]) >= 0 &&
            static_cast<std::size_t>(U().raw[kHome]) < save_.colonies.size()) {
            auto& C = save_.colonies[U().raw[kHome]];
            if (C.nation == n && (C.raw[kThreat] & 0x04) != 0 &&
                (C.raw[kGarrisonCount] != 0 || type != unit_type::kDragoon) && landRegion(map, C.x, C.y) == REG) {
                C.raw[kThreat] &= static_cast<std::uint8_t>(~0x04);
                if (C.raw[kGarrisonCount] > 0) {
                    --C.raw[kGarrisonCount];
                }
                return goTo('W', C.x, C.y);
            }
        }
        // A pioneer with nothing better builds a road.
        if (type == unit_type::kPioneer && !wantFound) {
            bool ok = true;
            if (nSet.index >= 0) {
                const auto& st = save_.settlements[static_cast<std::size_t>(nSet.index)];
                const int level = tribeLevel(save_, tribeOf(st));
                const int radius = level <= 1 ? 1 : level == 2 ? 2 : 3;
                if (nSet.distance <= radius && setTension < 3) {
                    ok = false;
                }
            }
            if (ncAny.index >= 0 && save_.colonies[static_cast<std::size_t>(ncAny.index)].nation != n &&
                ncAny.distance < 3) {
                ok = false;
            }
            if (ok) {
                U().orders = SaveUnit::kRoad;
                U().raw[kGoal] = 'R';
                return finish();
            }
        }
    }

    if (!garrison) {
        const int orders = U().orders;
        const bool atDest = orders == kOrdersGo && X == U().raw[kDestX] && Y == U().raw[kDestY];
        if (!(orders == SaveUnit::kNoOrders || orders == kOrdersThreat || orders == SaveUnit::kFortify ||
              orders == SaveUnit::kFortified) &&
            !atDest && !aiForeignNear(X, Y, n)) {
            return finish();
        }
    }

    // Which way: each neighbour is weighed by chance and by what lies there.
    const bool explore = aiThreatNear(X, Y, n, true) < 0 || (!ship && attack == 0);
    if (ship && explore && (mil + pio) != 0 && mask == 0) {
        auto& u = U();
        if ((u.raw[kFlags] & 0x10) == 0) {
            if (aiRandom(0, 16) == 0) {
                for (int tries = 0; tries < 100; ++tries) {
                    const int tx = aiRandom(2, map.width - 3);
                    const int ty = aiRandom(2, map.height - 3);
                    if (waterOfSea(map, tx, ty) && distanceBetween(X, Y, tx, ty) >= 8) {
                        u.raw[kFlags] |= 0x10;
                        return goTo('D', tx, ty);
                    }
                }
            }
        } else if (aiRandom(0, 48) == 0) {
            u.raw[kFlags] &= static_cast<std::uint8_t>(~0x10);
        }
    }
    int best = -999;
    int dir = 8;
    bool attackChosen = false;
    bool attackWeighed = false;
    const int last = U().raw[kLastStep];
    const bool aboard = !ship && onWater;
    for (int d = 0; d < 8; ++d) {
        const int sx = X + kStepX[static_cast<std::size_t>(d)];
        const int sy = Y + kStepY[static_cast<std::size_t>(d)];
        if (!map.inside(sx, sy)) {
            continue;
        }
        const auto& sq = map.at(sx, sy);
        if (sq.isWater()) {
            if (!ship || (sq.region & 0x0F) != 1) {
                continue;
            }
        } else if (ship) {
            continue;
        }
        const auto there = unitsAt(sx, sy);
        const auto colony = colonyAt(sx, sy);
        const auto village = settlementAt(sx, sy);
        const int top = there.empty() ? -1 : there.front();
        int occupant = -1;
        if (top >= 0) {
            occupant = unit(top).nation;
        } else if (colony) {
            occupant = save_.colonies[static_cast<std::size_t>(*colony)].nation;
        } else if (village) {
            occupant = save_.settlements[static_cast<std::size_t>(*village)].nation;
        }
        if (aboard && top >= 0 && occupant != n) {
            continue;
        }
        const TerrainType* t = terrainOf(sx, sy);
        const int move = t ? t->movementCost : 1;
        const int defence = t ? t->defenseBonus : 0;
        const auto roadRule = [&](int s) {
            if (river && sq.river() && d % 2 == 0) {
                return s + 2;
            }
            if (townRoad && (sq.flags & (MapSquare::kHasColony | MapSquare::kRoad)) != 0) {
                return s + 1;
            }
            return s - 3 * move;
        };
        int s = 0;
        const bool ownThere = !there.empty() && unit(there.front()).nation == n;
        if (type == unit_type::kScout) {
            s = roadRule(aiRandom(1, 8));
        } else if (wantFound) {
            s = (aiRandom(1, 4) + (sq.seen & 0x0F)) >> 1;
        } else if ((U().raw[3] & 0xF0) != 0 || onWater) {
            s = aiRandom(1, 5);
            if (!ownThere) {
                s += 4 * defence;
            }
        } else if ((roles & 0x30) != 0) {
            s = roadRule(aiRandom(1, 3));
        } else {
            s = aiRandom(1, 3) + defence;
        }
        if (sq.terrain == kSeaLane) {
            s -= 16;
        }
        if (!ship && attack > 1 && colony) {
            const auto& C = save_.colonies[static_cast<std::size_t>(*colony)];
            if (C.nation != n) {
                s += 16;
            } else if ((C.raw[kThreat] & 0x40) != 0) {
                s += 10;
            } else if ((C.raw[kThreat] & 0x04) != 0) {
                s += 6;
            } else if ((C.raw[kThreat] & 0x10) != 0) {
                s += 3;
            }
        }
        // Someone else there: an attack, weighed by its odds, or no way.
        bool attackFlag = false;
        if (occupant >= 0 && occupant != n) {
            const int rel = aiRelation(n, occupant);
            if (occupant >= kEuropeanNations) {
                if (tribeTension(save_, occupant - kEuropeanNations, n) < 75 && (rel & 0x02) == 0) {
                    continue;
                }
                if ((rel & 0x02) != 0) {
                    s = wrap16(s * 2);
                }
                if (regionByte(kRegionColonies, n, REG) == 0) {
                    continue;
                }
            } else if ((rel & 0x40) != 0 && type != kPrivateer && (top < 0 || unit(top).type != kPrivateer)) {
                continue;
            }
            if (attack == 0) {
                continue;
            }
            attackFlag = true;
            attackWeighed = true;
            int odds = aiAttackOdds(i, sx, sy);
            const int count = std::max(1, aiStack(top, 2));
            odds = wrap16(odds * ((aiStack(top, 0) + 1) / count));
            odds /= std::max(1, ut.cost);
            const bool fort = colony.has_value() || village.has_value();
            if (colony) {
                odds = wrap16(odds * 3);
            }
            if (village) {
                odds = wrap16(odds * 2);
            }
            if (type == kArtillery && !fort) {
                odds = 0;
            }
            if (n == human() && !fort && inOwnColony) {
                odds >>= 1;
            }
            if ((roles & 0x10) != 0 && status == 4) {
                odds = wrap16(odds * 3);
            }
            // Soldiers go for a colony with artillery only with more
            // artillery of their own around it.
            if ((type == unit_type::kSoldier || type == unit_type::kDragoon) && colony) {
                const int theirs = aiStack(top, 12);
                if (theirs != 0) {
                    int ours = 0;
                    for (int e = 0; e < 8; ++e) {
                        const int nx = sx + kStepX[static_cast<std::size_t>(e)];
                        const int ny = sy + kStepY[static_cast<std::size_t>(e)];
                        if (!map.inside(nx, ny)) {
                            continue;
                        }
                        const auto near = unitsAt(nx, ny);
                        if (!near.empty() && unit(near.front()).nation == n) {
                            ours += aiStack(near.front(), 12);
                        }
                    }
                    if (theirs >= ours) {
                        continue;
                    }
                }
            }
            if (odds >= 1000 || odds < 0) {
                odds = 1000;
            }
            if (odds < 12 && !ship) {
                s -= 999;
            } else {
                s += 4 * std::max(1, odds);
            }
        }
        if (last < 8) {
            int a = std::abs(last - d);
            if (a > 4) {
                a = 8 - a;
            }
            s -= 2 * a * a;
        }
        for (int e = 0; e < 8; ++e) {
            const int nx = sx + kStepX[static_cast<std::size_t>(e)];
            const int ny = sy + kStepY[static_cast<std::size_t>(e)];
            if (!map.inside(nx, ny)) {
                continue;
            }
            const auto near = unitsAt(nx, ny);
            if (attack == 0 && !near.empty()) {
                const int who = unit(near.front()).nation;
                if (who != n && who < kEuropeanNations && (aiRelation(n, who) & 0x60) == 0x20) {
                    s -= 10 * aiStack(near.front(), 10);
                }
            }
            if (ship) {
                if (const auto c = colonyAt(nx, ny)) {
                    const auto& C = save_.colonies[static_cast<std::size_t>(*c)];
                    if (C.nation != n && (aiRelation(n, C.nation) & 0x60) == 0x20) {
                        const int level = buildingLevel(C, BuildingKind::Fortification);
                        int pen = level >= 3 ? 40 : level == 2 ? 20 : 0;
                        for (const int j : unitsAt(nx, ny)) {
                            pen += unit(j).type == kArtillery ? 30 : 0;
                        }
                        s -= U().holds * pen;
                    }
                }
            }
        }
        if (explore) {
            const int fx = X + 4 * kStepX[static_cast<std::size_t>(d)];
            const int fy = Y + 4 * kStepY[static_cast<std::size_t>(d)];
            const std::size_t cell = static_cast<std::size_t>((fx >> 2) * 18 + (fy >> 2));
            if (map.inside(fx, fy) && !map.at(fx, fy).isWater() && cell < ai_.blocks.size() && ai_.blocks[cell] == 0) {
                s += 8;
            }
            if (ship && d >= 5 && d <= 7 && X > map.width / 2) {
                s += 4;
            }
            if (map.inside(fx, fy)) {
                for (int e = 0; e < 8; ++e) {
                    const int nx = fx + kStepX[static_cast<std::size_t>(e)];
                    const int ny = fy + kStepY[static_cast<std::size_t>(e)];
                    if (!map.inside(nx, ny)) {
                        continue;
                    }
                    const auto& nsq = map.at(nx, ny);
                    if (!nsq.seenBy(n) && (!nsq.isWater() || ship)) {
                        s += 2;
                    }
                    if (!unitsAt(nx, ny).empty()) {
                        s -= 2;
                    }
                    if (wantFound) {
                        const TerrainType* nt = terrainOf(nx, ny);
                        s += nt ? nt->value : 0;
                    }
                }
            }
        }
        if (s > best) {
            best = s;
            dir = d;
            attackChosen = attackFlag;
        }
    }
    if (attackChosen) {
        // An attack waits for a full move.
        if (movesLeft(i) < 3) {
            dir = 8;
        }
        U().raw[kGoal] = '9';
        return step(dir);
    }
    if (garrison) {
        auto& C = save_.colonies[static_cast<std::size_t>(ncOwn.index)];
        --C.raw[kGarrison];
        U().raw[kGoal] = 'G';
        return stay();
    }
    if ((roles & 0x01) != 0 && !inOwnColony) {
        const int p = aiTargetPrio(n, X, Y, 0);
        if (p >= 1 && p <= 4 && aiStack(i, 2) < 2) {
            U().raw[kGoal] = 'B';
            return stay();
        }
    }
    if ((roles & 0x04) != 0 && aiTargetPrio(n, X, Y, 2) != 0 && aiStack(i, 2) < 2) {
        U().raw[kGoal] = 'e';
        return stay();
    }
    // An armed unit that weighed an attack and stands next to someone
    // else's colony waits there.
    if (!ship && attack > 1 && attackWeighed && !aboard) {
        for (int d = 0; d < 8; ++d) {
            const auto c = colonyAt(X + kStepX[static_cast<std::size_t>(d)], Y + kStepY[static_cast<std::size_t>(d)]);
            if (c && save_.colonies[static_cast<std::size_t>(*c)].nation != n) {
                U().raw[kGoal] = 'F';
                return stay();
            }
        }
    }
    U().raw[kGoal] = '9';
    return step(dir);
}

int Game::aiAttackOdds(int i, int x, int y) {
    // The original's attack routine asked only for the odds: eight times
    // the attack over the defence plus one, before the steps for the easy
    // levels and for colonies. An empty colony is defended by one of its
    // colonists picked at random, as in a real attack.
    CombatStrengths s{};
    const int defender = defenderAt(x, y, i);
    const auto village = settlementAt(x, y);
    const auto colony = colonyAt(x, y);
    if (village) {
        s = villageStrengths(i, *village, true);
    } else if (defender >= 0) {
        s = combatStrengths(i, defender, true);
    } else if (colony && save_.colonies[static_cast<std::size_t>(*colony)].population > 0) {
        s = colonyStrengths(i, *colony, pickColonyDefender(*colony), true);
    } else {
        return 0;
    }
    return wrap16(s.attack * 8) / (s.defense + 1);
}

void Game::aiAttack(int i, int x, int y) {
    // The step the direction choice made onto someone else's square. With
    // less than a full move left the original does not attack.
    if (movesLeft(i) < 3 || !canAttack(i)) {
        aiEndMoves(i);
        return;
    }
    if (const auto village = settlementAt(x, y)) {
        attackVillageWar(i, *village);
        attackVillage(i, *village);
        return;
    }
    const int defender = defenderAt(x, y, i);
    if (defender >= 0) {
        attack(i, defender);
        return;
    }
    const auto colony = colonyAt(x, y);
    if (colony && !isShip(i) && colonyUndefended(*colony)) {
        attackColony(i, *colony, pickColonyDefender(*colony));
        return;
    }
    aiEndMoves(i);
}

} // namespace viceroy
