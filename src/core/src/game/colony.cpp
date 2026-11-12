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

WorkModifier workModifier(const SaveColony& colony, int difficulty) {
    WorkModifier m;
    m.bonus = std::clamp(colony.libertyBonus, 0, 2);
    const int tories = std::max(0, colony.population - rebelCount(colony));
    m.penalty = tories / std::max(1, 10 - std::clamp(difficulty, 0, 4));
    return m;
}

int landAround(const SaveMap& map, int x, int y) {
    int n = 0;
    for (const auto& o : kColonySquares) {
        if (map.inside(x + o.dx, y + o.dy) && !map.at(x + o.dx, y + o.dy).isWater()) {
            ++n;
        }
    }
    return n;
}

namespace {

// Measured on test colonies: what a resource adds to the land's yield for
// the job it suits.
int resourceAdds(Resource resource, int jobRow) {
    switch (resource) {
    case Resource::Grain:
    case Resource::Oasis:
        return jobRow == job::kFarmer ? 2 : 0;
    case Resource::Fish:
        return jobRow == job::kFisherman ? 3 : 0;
    case Resource::Timber:
        return jobRow == 5 ? 2 : 0;
    case Resource::Deer:
        return jobRow == 4 ? 2 : 0;
    case Resource::Beaver:
        return jobRow == 4 ? 3 : 0;
    case Resource::HillsOre:
        return jobRow == 6 ? 2 : 0;
    case Resource::Minerals:
        return jobRow == 6 ? 3 : 0;
    default:
        return 0;
    }
}

// Cotton, tobacco and sugar double their crop instead.
bool resourceDoubles(Resource resource, int jobRow) {
    return (resource == Resource::Sugar && jobRow == 1) || (resource == Resource::Tobacco && jobRow == 2) ||
           (resource == Resource::Cotton && jobRow == 3);
}

} // namespace

int squareYield(const Ruleset& rules, const MapSquare& square, int jobRow, int skill, WorkModifier modifier,
                bool nearLand, Resource resource) {
    const int row = terrainRow(square);
    int amount = yieldOf(rules, row, jobRow);
    if (amount <= 0) {
        return 0;
    }
    // Measured: the bonus and most resources are added to the land's own
    // yield, so whatever doubles it (lumber, experts) doubles them too.
    const int added = modifier.bonus + resourceAdds(resource, jobRow);
    amount += added;
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
        // Measured: ocean (yield 3) gives a colonist 4 with land on 3 or
        // more of its 8 sides and 2 with less.
        amount += nearLand ? 1 : -1;
        break;
    default:
        break;
    }
    if (square.river()) {
        amount += 1;
    }
    if (resourceDoubles(resource, jobRow)) {
        amount *= 2;
    }
    if (isExpert(skill, jobRow)) {
        // Expert farmers and fishermen make 2 more (and the bonus and the
        // resource twice); the other experts make twice as much.
        const bool adds = jobRow == job::kFarmer || jobRow == job::kFisherman;
        amount = adds ? amount + 2 + added : amount * 2;
    }
    // The tories' penalty comes off the end.
    return std::max(0, amount - modifier.penalty);
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

int centreFoodYield(const Ruleset& rules, const MapSquare& square, int difficulty) {
    const int row = terrainRow(square);
    const int yield = yieldOf(rules, row, job::kFarmer);
    if (yield <= 0) {
        return 0;
    }
    // Measured on test colonies at Conquistador: the land's food and 1 more,
    // 1 less on desert, plains and savannah and their forests, at most 3.
    int food = yield + 1;
    const bool plainLand = !square.hills() && !square.mountains() && square.terrain < 24;
    const int open = square.terrain & 7;
    if (plainLand && (open == 1 || open == 2 || open == 5)) {
        food -= 1;
    }
    food = std::min(food, 3);
    if ((square.flags & MapSquare::kPlowed) != 0) {
        food += 1;
    }
    // Discoverer gives 2 more, Explorer 1.
    return food + std::max(0, 2 - difficulty);
}

ColonyProduction colonyProduction(const SaveColony& colony, const SaveMap& map, const Ruleset& rules,
                                  int difficulty) {
    ColonyProduction p;
    const WorkModifier modifier = workModifier(colony, difficulty);
    // The colony's own square gets the bonus but not the penalty.
    const WorkModifier centreModifier{modifier.bonus, 0};
    const int population = std::clamp(colony.population, 0, kMaxColonists);
    p.colonists.resize(static_cast<std::size_t>(population));

    // The colony's own square works itself: food, and the best other crop
    // it has (furs in a forest). Measured, see docs/rules/production.md.
    if (map.inside(colony.x, colony.y)) {
        const MapSquare centre = map.at(colony.x, colony.y);
        p.centreFood = centreFoodYield(rules, centre, difficulty) + centreModifier.bonus;
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
            // Measured: the land's own yield, 1 more on a minor river and 2
            // on a major one, and 1 more at Discoverer.
            const int river = centre.majorRiver() ? 2 : (centre.minorRiver() ? 1 : 0);
            p.centreAmount = yieldOf(rules, row, best) + river + (difficulty <= 0 ? 1 : 0) + centreModifier.bonus;
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
        work.amount = squareYield(rules, map.at(x, y), jobRow, colony.skills[static_cast<std::size_t>(who)], modifier,
                                  landAround(map, x, y) >= 3, resourceAt(map, x, y));
    }

    for (int i = 0; i < population; ++i) {
        auto& work = p.colonists[static_cast<std::size_t>(i)];
        const int jobRow = colony.jobs[static_cast<std::size_t>(i)];
        if (work.square < 0) {
            work.building = buildingForJob(jobRow);
            if (work.building) {
                const int level = std::max(1, buildingLevel(colony, *work.building));
                work.good = jobOutput(jobRow);
                // Measured: in a building the bonus and the penalty count
                // once for each worker, expert or not.
                const int base = buildingYield(level, colony.skills[static_cast<std::size_t>(i)], jobRow);
                work.amount = work.good >= 0 ? std::max(0, base + modifier.bonus - modifier.penalty) : 0;
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
                    const std::vector<SaveColony>& colonies, int colonist, int jobRow, int difficulty = 0) {
    JobChoice choice;
    const WorkModifier modifier = workModifier(colony, difficulty);
    choice.job = jobRow;
    const int skillOf = colony.skills[static_cast<std::size_t>(colonist)];
    if (jobRow < static_cast<int>(kSquareWorkCount)) {
        const int here = currentSquare(colony, colonist);
        for (int s = 0; s < 8; ++s) {
            if (s != here && !squareFree(colony, map, colonies, s)) {
                continue;
            }
            const int x = colony.x + kColonySquares[static_cast<std::size_t>(s)].dx;
            const int y = colony.y + kColonySquares[static_cast<std::size_t>(s)].dy;
            const auto& sq = map.at(x, y);
            if (sq.isWater() != (jobRow == job::kFisherman)) {
                continue;
            }
            const int amount =
                squareYield(rules, sq, jobRow, skillOf, modifier, landAround(map, x, y) >= 3, resourceAt(map, x, y));
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
    const int base = buildingYield(buildingLevel(colony, *building), skillOf, jobRow);
    choice.amount = jobRow == job::kTeacher ? 0 : std::max(0, base + modifier.bonus - modifier.penalty);
    return choice;
}

} // namespace

std::vector<JobChoice> jobChoices(const SaveColony& colony, const SaveMap& map, const Ruleset& rules,
                                  const std::vector<SaveColony>& colonies, int colonist, int difficulty) {
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
        out.push_back(choiceFor(colony, map, rules, colonies, colonist, j, difficulty));
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

int horsesBred(const SaveColony& colony, int foodSurplus) {
    // Measured: two horses for every started herd of 50 (25 with stables),
    // at most half the surplus rounded up, and nothing from a single horse.
    const int horses = colony.stock[cargo::kHorses];
    if (horses < 2 || foodSurplus <= 0) {
        return 0;
    }
    const int herd = buildingLevel(colony, BuildingKind::Stables) > 0 ? 25 : 50;
    const int bred = std::min(2 * ((horses + herd - 1) / herd), (foodSurplus + 1) / 2);
    return std::clamp(bred, 0, std::max(0, storageLimit(colony) - horses));
}

ColonyTurn runColonyTurn(SaveColony& colony, const SaveMap& map, const Ruleset& rules, int difficulty) {
    ColonyTurn turn;
    const auto p = colonyProduction(colony, map, rules, difficulty);
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

    // Food, and the horses bred from what is left over.
    made[cargo::kFood] -= p.foodEaten;
    const int born = horsesBred(colony, made[cargo::kFood]);
    made[cargo::kFood] -= born;
    made[cargo::kHorses] += born;
    turn.horsesBred = born;
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

    // Bells: membership moves, and then the bonus the next turn works with.
    turn.libertyBefore = colony.libertyBonus;
    gatherBells(colony, p.produced[cargo::kBells]);
    updateLibertyBonus(colony);
    turn.libertyAfter = colony.libertyBonus;
    return turn;
}

int sonsOfLibertyPercent(const SaveColony& colony) {
    if (colony.rebelDivisor <= 0) {
        return 0;
    }
    return std::clamp(static_cast<int>(100LL * colony.rebelDividend / colony.rebelDivisor), 0, 100);
}

int rebelCount(const SaveColony& colony) {
    // Measured: 5% of 10 is no rebel, 36% of 2 is one; halves round down.
    return (colony.population * sonsOfLibertyPercent(colony) + 49) / 100;
}

void gatherBells(SaveColony& colony, int bells) {
    const long long rebels = std::max(0, colony.rebelDividend);
    const long long total = std::max(0, colony.rebelDivisor);
    long long newTotal = total + 2LL * std::max(0, colony.population) - total / 64;
    long long newRebels = rebels + std::max(0, bells) - rebels / 64 - (total > 0 ? 5 * rebels / total : 0);
    newTotal = std::clamp<long long>(newTotal, 0, 0x7FFFFFFF);
    newRebels = std::clamp<long long>(newRebels, 0, newTotal);
    colony.rebelDivisor = static_cast<int>(newTotal);
    colony.rebelDividend = static_cast<int>(newRebels);
}

void updateLibertyBonus(SaveColony& colony) {
    const int percent = sonsOfLibertyPercent(colony);
    const int target = percent >= 100 ? 2 : percent >= 50 ? 1 : 0;
    if (colony.libertyBonus < target) {
        ++colony.libertyBonus;
    } else if (colony.libertyBonus > target) {
        --colony.libertyBonus;
    }
}

void startLiberty(SaveColony& colony, const SaveMap& map, const Ruleset& rules, int difficulty) {
    colony.rebelDividend = 0;
    colony.rebelDivisor = 200;
    colony.libertyBonus = 0;
    gatherBells(colony, colonyProduction(colony, map, rules, difficulty).produced[cargo::kBells]);
}

void joinLiberty(SaveColony& colony, const SaveMap& map, const Ruleset& rules, int difficulty) {
    colony.rebelDivisor += 100;
    gatherBells(colony, colonyProduction(colony, map, rules, difficulty).produced[cargo::kBells]);
}

} // namespace viceroy
