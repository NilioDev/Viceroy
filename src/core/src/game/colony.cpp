// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/game/colony.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>

namespace viceroy {

namespace {

constexpr int kTerrainArctic = 24;
constexpr int kTerrainOcean = 25;
constexpr int kTerrainSeaLane = 26;
constexpr int kRowArctic = 16;
constexpr int kRowOcean = 17;
constexpr int kRowSeaLane = 18;
constexpr int kRowMountains = 19;
constexpr int kRowHills = 20;
constexpr int kForestedRows = 8;

bool isExpert(int skill, int jobRow) {
    return skill == jobRow && jobRow < job::kTeacher;
}

int yieldOf(const Ruleset& rules, int row, int jobRow) {
    if (row < 0 || static_cast<std::size_t>(row) >= rules.terrain.size() || jobRow < 0 ||
        static_cast<std::size_t>(jobRow) >= kSquareWorkCount) {
        return 0;
    }
    return rules.terrain[static_cast<std::size_t>(row)].yield[static_cast<std::size_t>(jobRow)];
}

// Workers per building output: 3 for a colonist in a house or shop, more in
// a factory; experts make twice as much, servants and criminals less.
int buildingYield(int level, int skill, int jobRow) {
    int base = 3;
    if (skill == skill::kServant) {
        base = 2;
    } else if (skill == skill::kCriminal || skill == skill::kConvert) {
        base = 1;
    }
    if (isExpert(skill, jobRow)) {
        base *= 2;
    }
    if (jobRow == job::kCarpenter && level >= 2) {
        base *= 2; // the lumber mill
    }
    if (level >= 3) {
        base += base / 2; // factories
    }
    return base;
}

} // namespace

int buildingLevel(const SaveColony& colony, BuildingKind kind) {
    const auto& f = kBuildingFields[static_cast<std::size_t>(kind)];
    const auto v = (colony.buildings >> f.shift) & ((std::uint64_t{1} << f.bits) - 1);
    return std::popcount(v);
}

void setBuildingLevel(SaveColony& colony, BuildingKind kind, int level) {
    const auto& f = kBuildingFields[static_cast<std::size_t>(kind)];
    const std::uint64_t mask = ((std::uint64_t{1} << f.bits) - 1) << f.shift;
    const std::uint64_t bits = ((std::uint64_t{1} << std::clamp(level, 0, f.bits)) - 1) << f.shift;
    colony.buildings = (colony.buildings & ~mask) | bits;
}

int terrainRow(const MapSquare& square) {
    if (square.terrain == kTerrainOcean) {
        return kRowOcean;
    }
    if (square.terrain == kTerrainSeaLane) {
        return kRowSeaLane;
    }
    if (square.mountains()) {
        return kRowMountains;
    }
    if (square.hills()) {
        return kRowHills;
    }
    if (square.terrain == kTerrainArctic) {
        return kRowArctic;
    }
    if (square.isForest()) {
        return kForestedRows + (square.terrain & 7);
    }
    return square.terrain & 7;
}

int squareYield(const Ruleset& rules, const MapSquare& square, int jobRow, int skill) {
    const int row = terrainRow(square);
    int amount = yieldOf(rules, row, jobRow);
    if (amount <= 0) {
        return 0;
    }
    const bool plowed = (square.flags & MapSquare::kPlowed) != 0;
    const bool road = (square.flags & MapSquare::kRoad) != 0;
    switch (jobRow) {
    case job::kFarmer:
        amount += 1;
        if (plowed) {
            amount += 1;
        }
        break;
    case 1: // sugar
    case 2: // tobacco
    case 3: // cotton
        if (plowed) {
            amount += 1;
        }
        break;
    case 4: // furs
        if (road) {
            amount += 2;
        }
        break;
    case 5: // lumber
        amount *= 2;
        if (road) {
            amount += 2;
        }
        break;
    case 6: // ore
    case 7: // silver
        if (road) {
            amount += 1;
        }
        break;
    case job::kFisherman:
        amount += 2;
        break;
    default:
        break;
    }
    if (square.river()) {
        amount += 1;
    }
    if (isExpert(skill, jobRow)) {
        amount = jobRow == job::kFarmer ? amount + 2 : amount * 2;
    }
    return amount;
}

std::optional<BuildingKind> buildingForJob(int jobRow) {
    switch (jobRow) {
    case 9:
        return BuildingKind::Distiller;
    case 10:
        return BuildingKind::Tobacconist;
    case 11:
        return BuildingKind::Weaver;
    case 12:
        return BuildingKind::FurTrader;
    case 13:
        return BuildingKind::Carpenter;
    case 14:
        return BuildingKind::Blacksmith;
    case 15:
        return BuildingKind::Armory;
    case 16:
        return BuildingKind::Church;
    case 17:
        return BuildingKind::TownHall;
    case 18:
        return BuildingKind::Schoolhouse;
    default:
        return std::nullopt;
    }
}

int jobOutput(int jobRow) {
    static constexpr int kOutputs[] = {
        cargo::kFood,   cargo::kSugar,  cargo::kTobacco, cargo::kCotton,  cargo::kFurs,
        cargo::kLumber, cargo::kOre,    cargo::kSilver,  cargo::kFood,    cargo::kRum,
        cargo::kCigars, cargo::kCloth,  cargo::kCoats,   cargo::kHammers, cargo::kTools,
        cargo::kMuskets, cargo::kCrosses, cargo::kBells, -1};
    return jobRow >= 0 && jobRow < static_cast<int>(std::size(kOutputs)) ? kOutputs[jobRow] : -1;
}

int jobInput(int jobRow) {
    switch (jobRow) {
    case 9:
        return cargo::kSugar;
    case 10:
        return cargo::kTobacco;
    case 11:
        return cargo::kCotton;
    case 12:
        return cargo::kFurs;
    case 13:
        return cargo::kLumber;
    case 14:
        return cargo::kOre;
    case 15:
        return cargo::kTools;
    default:
        return -1;
    }
}

ColonyProduction colonyProduction(const SaveColony& colony, const SaveMap& map, const Ruleset& rules) {
    ColonyProduction p;
    const int population = std::clamp(colony.population, 0, kMaxColonists);
    p.colonists.resize(static_cast<std::size_t>(population));

    // The colony's own square works itself as if plowed: food, and the
    // best other crop it has (furs in a forest).
    if (map.inside(colony.x, colony.y)) {
        MapSquare centre = map.at(colony.x, colony.y);
        centre.flags = static_cast<std::uint8_t>(centre.flags | MapSquare::kPlowed);
        p.centreFood = squareYield(rules, centre, job::kFarmer, -1);
        const int row = terrainRow(centre);
        int best = -1;
        int bestAmount = 0;
        if (centre.isForest() && !centre.hills() && !centre.mountains()) {
            best = 4; // furs
        } else {
            for (const int j : {1, 2, 3, 6, 7}) {
                if (yieldOf(rules, row, j) > bestAmount) {
                    best = j;
                    bestAmount = yieldOf(rules, row, j);
                }
            }
        }
        if (best >= 0 && yieldOf(rules, row, best) > 0) {
            p.centreGood = jobOutput(best);
            p.centreAmount = yieldOf(rules, row, best) + 1;
        }
        p.produced[cargo::kFood] += p.centreFood;
        if (p.centreGood >= 0) {
            p.produced[static_cast<std::size_t>(p.centreGood)] += p.centreAmount;
        }
    }

    for (std::size_t s = 0; s < colony.squares.size(); ++s) {
        const int who = colony.squares[s];
        if (who < 0 || who >= population) {
            continue;
        }
        const int x = colony.x + kColonySquares[s].dx;
        const int y = colony.y + kColonySquares[s].dy;
        if (!map.inside(x, y)) {
            continue;
        }
        auto& work = p.colonists[static_cast<std::size_t>(who)];
        const int jobRow = colony.jobs[static_cast<std::size_t>(who)];
        work.square = static_cast<int>(s);
        work.good = jobOutput(jobRow);
        work.amount = squareYield(rules, map.at(x, y), jobRow, colony.skills[static_cast<std::size_t>(who)]);
    }

    for (int i = 0; i < population; ++i) {
        auto& work = p.colonists[static_cast<std::size_t>(i)];
        const int jobRow = colony.jobs[static_cast<std::size_t>(i)];
        if (work.square < 0) {
            work.building = buildingForJob(jobRow);
            if (work.building) {
                const int level = std::max(1, buildingLevel(colony, *work.building));
                work.good = jobOutput(jobRow);
                work.amount = work.good >= 0 ? buildingYield(level, colony.skills[static_cast<std::size_t>(i)], jobRow) : 0;
                const int input = jobInput(jobRow);
                if (input >= 0) {
                    p.needed[static_cast<std::size_t>(input)] += work.amount;
                }
            }
        }
        if (work.good >= 0) {
            p.produced[static_cast<std::size_t>(work.good)] += work.amount;
        }
    }

    // Every colony makes a bell and a cross of its own (its chapel); a
    // church makes one more (measured: Fort Orange in COLONY06).
    p.produced[cargo::kBells] += 1;
    p.produced[cargo::kCrosses] += 1 + buildingLevel(colony, BuildingKind::Church);
    p.foodEaten = 2 * population;
    return p;
}

bool squareFree(const SaveColony& colony, const SaveMap& map, const std::vector<SaveColony>& colonies, int square) {
    if (square < 0 || square >= 8 || colony.squares[static_cast<std::size_t>(square)] >= 0) {
        return false;
    }
    const int x = colony.x + kColonySquares[static_cast<std::size_t>(square)].dx;
    const int y = colony.y + kColonySquares[static_cast<std::size_t>(square)].dy;
    if (!map.inside(x, y)) {
        return false;
    }
    if (map.at(x, y).isWater() && buildingLevel(colony, BuildingKind::Docks) == 0) {
        return false;
    }
    // Worked by a neighbouring colony?
    for (const auto& other : colonies) {
        if (&other == &colony || std::abs(other.x - x) > 1 || std::abs(other.y - y) > 1) {
            continue;
        }
        if (other.x == x && other.y == y) {
            return false;
        }
        for (std::size_t s = 0; s < 8; ++s) {
            if (other.squares[s] >= 0 && other.x + kColonySquares[s].dx == x && other.y + kColonySquares[s].dy == y) {
                return false;
            }
        }
    }
    return true;
}

namespace {

int workersIn(const SaveColony& colony, BuildingKind kind, int except) {
    int n = 0;
    for (int i = 0; i < colony.population; ++i) {
        if (i == except) {
            continue;
        }
        const bool outside = std::find(colony.squares.begin(), colony.squares.end(), i) != colony.squares.end();
        const auto b = buildingForJob(colony.jobs[static_cast<std::size_t>(i)]);
        if (!outside && b && *b == kind) {
            ++n;
        }
    }
    return n;
}

int currentSquare(const SaveColony& colony, int colonist) {
    for (std::size_t s = 0; s < 8; ++s) {
        if (colony.squares[s] == colonist) {
            return static_cast<int>(s);
        }
    }
    return -1;
}

JobChoice choiceFor(const SaveColony& colony, const SaveMap& map, const Ruleset& rules,
                    const std::vector<SaveColony>& colonies, int colonist, int jobRow) {
    JobChoice choice;
    choice.job = jobRow;
    const int skillOf = colony.skills[static_cast<std::size_t>(colonist)];
    if (jobRow < static_cast<int>(kSquareWorkCount)) {
        const int here = currentSquare(colony, colonist);
        for (int s = 0; s < 8; ++s) {
            if (s != here && !squareFree(colony, map, colonies, s)) {
                continue;
            }
            const auto& sq = map.at(colony.x + kColonySquares[static_cast<std::size_t>(s)].dx,
                                    colony.y + kColonySquares[static_cast<std::size_t>(s)].dy);
            if (sq.isWater() != (jobRow == job::kFisherman)) {
                continue;
            }
            const int amount = squareYield(rules, sq, jobRow, skillOf);
            if (amount > choice.amount) {
                choice.amount = amount;
                choice.square = s;
            }
        }
        return choice;
    }
    const auto building = buildingForJob(jobRow);
    if (!building || buildingLevel(colony, *building) == 0 || workersIn(colony, *building, colonist) >= 3) {
        return choice;
    }
    choice.amount = jobRow == job::kTeacher ? 0 : buildingYield(buildingLevel(colony, *building), skillOf, jobRow);
    return choice;
}

} // namespace

std::vector<JobChoice> jobChoices(const SaveColony& colony, const SaveMap& map, const Ruleset& rules,
                                  const std::vector<SaveColony>& colonies, int colonist) {
    std::vector<JobChoice> out;
    if (colonist < 0 || colonist >= colony.population) {
        return out;
    }
    for (int j = 0; j < job::kTeacher; ++j) {
        // Work in a building is offered only where the building stands.
        const auto building = buildingForJob(j);
        if (building && buildingLevel(colony, *building) == 0) {
            continue;
        }
        out.push_back(choiceFor(colony, map, rules, colonies, colonist, j));
    }
    return out;
}

bool assignJob(SaveColony& colony, const SaveMap& map, const Ruleset& rules,
               const std::vector<SaveColony>& colonies, int colonist, int jobRow) {
    if (colonist < 0 || colonist >= colony.population || jobRow < 0 || jobRow > job::kTeacher) {
        return false;
    }
    const JobChoice choice = choiceFor(colony, map, rules, colonies, colonist, jobRow);
    const bool onLand = jobRow < static_cast<int>(kSquareWorkCount);
    if (onLand ? choice.square < 0 : (!buildingForJob(jobRow) ||
                                       buildingLevel(colony, *buildingForJob(jobRow)) == 0 ||
                                       workersIn(colony, *buildingForJob(jobRow), colonist) >= 3)) {
        return false;
    }
    const int here = currentSquare(colony, colonist);
    if (here >= 0) {
        colony.squares[static_cast<std::size_t>(here)] = -1;
    }
    if (onLand) {
        colony.squares[static_cast<std::size_t>(choice.square)] = colonist;
    }
    colony.jobs[static_cast<std::size_t>(colonist)] = static_cast<std::uint8_t>(jobRow);
    return true;
}

int storageLimit(const SaveColony& colony) {
    // Raw byte 149: 0 none, 1 warehouse, 2 warehouse expansion.
    return 100 * (1 + std::clamp<int>(colony.raw[149], 0, 2));
}

ColonyTurn runColonyTurn(SaveColony& colony, const SaveMap& map, const Ruleset& rules) {
    ColonyTurn turn;
    const auto p = colonyProduction(colony, map, rules);
    std::array<int, cargo::kCount> made = p.produced;

    // Workshops: as much as their raw good allows, from this turn's
    // harvest first and then from the warehouse.
    std::array<int, cargo::kCount> wanted{};
    std::array<int, cargo::kCount> outputOf{};
    for (std::size_t i = 0; i < p.colonists.size(); ++i) {
        const auto& work = p.colonists[i];
        if (!work.building || work.good < 0) {
            continue;
        }
        const int input = jobInput(colony.jobs[i]);
        if (input >= 0) {
            wanted[static_cast<std::size_t>(input)] += work.amount;
            outputOf[static_cast<std::size_t>(input)] = work.good;
        }
    }
    for (int raw = 0; raw < cargo::kCount; ++raw) {
        const auto r = static_cast<std::size_t>(raw);
        if (wanted[r] == 0) {
            continue;
        }
        const int available = made[r] + (raw < kGoodsKinds ? std::max(0, colony.stock[r]) : 0);
        const int used = std::min(wanted[r], available);
        const int shortfall = wanted[r] - used;
        made[static_cast<std::size_t>(outputOf[r])] -= shortfall;
        made[r] -= used; // negative: taken from the warehouse
    }

    // Food.
    made[cargo::kFood] -= p.foodEaten;
    for (int good = 0; good < kGoodsKinds; ++good) {
        colony.stock[static_cast<std::size_t>(good)] += made[static_cast<std::size_t>(good)];
    }
    auto& food = colony.stock[cargo::kFood];
    if (food >= 200) {
        food -= 200;
        turn.grew = true;
    } else if (food < 0) {
        food = 0;
        turn.starved = colony.population > 1;
    }

    // Construction.
    colony.hammers += std::max(0, made[cargo::kHammers]);
    if (colony.construction >= 0 && static_cast<std::size_t>(colony.construction) < rules.buildings.size()) {
        const auto& b = rules.buildings[static_cast<std::size_t>(colony.construction)];
        const int tools = b.tools * 10;
        if (colony.hammers >= b.hammers && colony.stock[cargo::kTools] >= tools) {
            for (int k = 0; k < kBuildingKinds; ++k) {
                const auto& f = kBuildingFields[static_cast<std::size_t>(k)];
                if (f.firstRow >= 0 && colony.construction >= f.firstRow && colony.construction < f.firstRow + f.bits) {
                    setBuildingLevel(colony, static_cast<BuildingKind>(k), colony.construction - f.firstRow + 1);
                }
            }
            colony.hammers -= b.hammers;
            colony.stock[cargo::kTools] -= tools;
            turn.built = colony.construction;
            colony.construction = -1;
        }
    }

    // What the warehouse cannot hold is lost.
    const int limit = storageLimit(colony);
    for (int good = 1; good < kGoodsKinds; ++good) {
        auto& amount = colony.stock[static_cast<std::size_t>(good)];
        if (amount > limit) {
            turn.wasted[static_cast<std::size_t>(good)] = amount - limit;
            amount = limit;
        }
        amount = std::max(0, amount);
    }
    return turn;
}

int sonsOfLibertyPercent(const SaveColony& colony) {
    if (colony.rebelDivisor <= 0) {
        return 0;
    }
    return std::clamp(static_cast<int>(100LL * colony.rebelDividend / colony.rebelDivisor), 0, 100);
}

} // namespace viceroy
