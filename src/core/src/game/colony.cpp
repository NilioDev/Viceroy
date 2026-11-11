// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/game/colony.hpp"

#include <algorithm>
#include <bit>

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
        amount += 1;
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

    // Every colony makes a bell and a cross of its own.
    p.produced[cargo::kBells] += 1;
    p.produced[cargo::kCrosses] += 1;
    p.foodEaten = 2 * population;
    return p;
}

int sonsOfLibertyPercent(const SaveColony& colony) {
    if (colony.rebelDivisor <= 0) {
        return 0;
    }
    return std::clamp(static_cast<int>(100LL * colony.rebelDividend / colony.rebelDivisor), 0, 100);
}

} // namespace viceroy
