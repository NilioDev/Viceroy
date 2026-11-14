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

// Rows of @FATHERS that change what colonies make.
constexpr int kHudson = 8;
constexpr int kJefferson = 15;
constexpr int kPaine = 17;
constexpr int kBolivar = 18;
constexpr int kPenn = 21;

constexpr std::size_t kJobTurnsAt = 96;

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

NationEffects nationEffects(const SaveGame& save, int nation) {
    NationEffects e;
    if (nation < 0 || nation >= kEuropeanNations) {
        return e;
    }
    const auto& n = save.nations[static_cast<std::size_t>(nation)];
    constexpr std::size_t kFatherBits = 7;
    for (std::size_t i = 0; i < 4; ++i) {
        e.fathers |= static_cast<std::uint32_t>(n.raw[kFatherBits + i]) << (8 * i);
    }
    e.tax = n.taxRate;
    int wagons = 0;
    for (const auto& u : save.units) {
        wagons += u.nation == nation && u.type == 12 ? 1 : 0; // wagon trains
    }
    for (const auto& c : save.colonies) {
        e.colonies += c.nation == nation ? 1 : 0;
    }
    e.wagonsFull = wagons >= e.colonies;
    return e;
}

WorkModifier workModifier(const SaveColony& colony, int difficulty, bool tories, int libertyExtra) {
    WorkModifier m;
    m.bonus = std::clamp(colony.libertyBonus, 0, 2);
    if (!tories) {
        return m;
    }
    const int toryCount = std::max(0, colony.population - rebelCount(colony, libertyExtra));
    m.penalty = toryCount / std::max(1, 10 - std::clamp(difficulty, 0, 4));
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
        return jobRow == 4 || jobRow == job::kFarmer ? 2 : 0;
    case Resource::Beaver:
        return jobRow == 4 ? 3 : 0;
    case Resource::HillsOre:
        return jobRow == 6 ? 2 : 0;
    case Resource::Minerals:
        return jobRow == 6 ? 3 : 0;
    case Resource::Silver:
        return jobRow == 7 ? 2 : 0;
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

// What a worker in a building makes. Measured: a colonist makes 3 in a
// house, a shop (or the lumber mill) doubles that and a factory adds half
// again (3, 6, 9); servants make 2 and criminals and converts 1. In the
// craft workshops (rum to muskets) the liberty bonus and the tories'
// penalty go in after the shop and before an expert doubles (a colonist in
// a shop with +1: 7; an expert in a house with +2: 10); for carpenters,
// statesmen and preachers they are added once at the end.
int buildingYield(int level, int skill, int jobRow, WorkModifier modifier, bool withFactory) {
    int base = 3;
    if (skill == skill::kServant) {
        base = 2;
    } else if (skill == skill::kCriminal || skill == skill::kConvert) {
        base = 1;
    }
    if (level >= 2) {
        base *= 2;
    }
    const bool craft = jobRow >= 9 && jobRow <= 15 && jobRow != job::kCarpenter;
    const int modified = modifier.bonus - modifier.penalty;
    if (craft) {
        base += modified;
    }
    if (isExpert(skill, jobRow)) {
        base *= 2;
    }
    if (level >= 3 && withFactory) {
        base += base / 2;
    }
    if (!craft) {
        base += modified;
    }
    return std::max(0, base);
}

int squareYield(const Ruleset& rules, const MapSquare& square, int jobRow, int skill, WorkModifier modifier,
                bool nearLand, Resource resource) {
    const int row = terrainRow(square);
    int amount = yieldOf(rules, row, jobRow);
    if (amount <= 0) {
        return 0;
    }
    // Measured: mountains give silver only with a deposit (pattern square)
    // or with mask bit 2, which on a pattern square is a worked-out deposit.
    constexpr std::uint8_t kSilverMark = 0x04;
    if (jobRow == 7 && square.mountains() && resource != Resource::Silver && (square.flags & kSilverMark) == 0) {
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
    // Read from the original: with Henry Hudson the land's furs count
    // twice.
    if (jobRow == 4 && modifier.hudson) {
        amount *= 2;
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
    // Measured: for lumber a road adds after an expert doubles (San
    // Salvador's expert lumberjack on a rain forest with a road: 2 x 2 x 2 +
    // 2 = 10), for furs and ore before (Santo Domingo, New Amsterdam).
    if (road && jobRow == 5) {
        amount += 2;
    }
    // Indian converts make 1 more food, crops and furs (San Salvador: a
    // convert farmer and a convert fisherman).
    if (skill == skill::kConvert && (jobRow <= 4 || jobRow == job::kFisherman)) {
        amount += 1;
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
                                  int difficulty, bool tories, const NationEffects& nation) {
    ColonyProduction p;
    WorkModifier modifier = workModifier(colony, difficulty, tories, nation.libertyExtra(tories));
    modifier.hudson = nation.has(kHudson);
    // The colony's own square gets the bonus but not the penalty.
    const WorkModifier centreModifier{modifier.bonus, 0};
    const int population = std::clamp(colony.population, 0, kMaxColonists);
    p.colonists.resize(static_cast<std::size_t>(population));

    // The colony's own square works itself: food, and its best crop or
    // ore. Measured, see docs/rules/production.md.
    if (map.inside(colony.x, colony.y)) {
        const MapSquare centre = map.at(colony.x, colony.y);
        const Resource resource = resourceAt(map, colony.x, colony.y);
        p.centreFood = centreFoodYield(rules, centre, difficulty) + resourceAdds(resource, job::kFarmer) +
                       centreModifier.bonus;
        const int row = terrainRow(centre);
        // Measured: the first of the largest yields, resource included, in
        // the order sugar, tobacco, cotton, furs, ore, silver.
        int best = -1;
        int bestAmount = 0;
        for (const int j : {1, 2, 3, 4, 6, 7}) {
            int amount = yieldOf(rules, row, j);
            if (amount <= 0) {
                continue;
            }
            amount += resourceAdds(resource, j);
            if (resourceDoubles(resource, j)) {
                amount *= 2;
            }
            if (amount > bestAmount) {
                best = j;
                bestAmount = amount;
            }
        }
        if (best >= 0) {
            p.centreGood = jobOutput(best);
            // Measured: 1 more on a minor river, 2 on a major one, and 1
            // more at Discoverer.
            const int river = centre.majorRiver() ? 2 : (centre.minorRiver() ? 1 : 0);
            p.centreAmount = bestAmount + river + (difficulty <= 0 ? 1 : 0) + centreModifier.bonus;
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
                const int skillOf = colony.skills[static_cast<std::size_t>(i)];
                const int base = buildingYield(level, skillOf, jobRow, modifier);
                work.amount = work.good >= 0 ? base : 0;
                // Measured: a factory's extra half needs no raw goods.
                work.uses = work.good >= 0 ? buildingYield(level, skillOf, jobRow, modifier, false) : 0;
                const int input = jobInput(jobRow);
                if (input >= 0) {
                    p.needed[static_cast<std::size_t>(input)] += work.uses;
                }
            }
        }
        // Read from the original: William Penn adds half to each
        // preacher's crosses.
        if (work.square < 0 && work.good == cargo::kCrosses && nation.has(kPenn)) {
            work.amount += work.amount / 2;
        }
        if (work.good >= 0) {
            p.produced[static_cast<std::size_t>(work.good)] += work.amount;
        }
    }

    // Every colony makes a bell and a cross of its own (its chapel); a
    // church makes one more (measured: Fort Orange in COLONY06).
    p.produced[cargo::kBells] += 1;
    p.produced[cargo::kCrosses] += 1 + buildingLevel(colony, BuildingKind::Church);
    // Read from the original, in its order: Thomas Jefferson adds half the
    // bells, Thomas Paine the tax rate in percent of them, Simon Bolivar a
    // fifth of the people (rounded up from 2) in a computer nation's colony,
    // and the printing press half or the newspaper as much again.
    auto& bells = p.produced[cargo::kBells];
    if (nation.has(kJefferson)) {
        bells += bells / 2;
    }
    if (nation.has(kPaine)) {
        bells += bells * std::max(0, nation.tax) / 100;
    }
    if (nation.has(kBolivar) && !tories) {
        bells += (population + 3) / 5;
    }
    const int press = buildingLevel(colony, BuildingKind::PrintingPress);
    p.pressBells = press >= 2 ? bells : press == 1 ? bells / 2 : 0;
    bells += p.pressBells;
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

// Read from the original: three to a building, counted by job, but only
// for the jobs after the distiller's (its house takes a fourth).
bool buildingFull(const SaveColony& colony, int jobRow, int except) {
    if (jobRow <= job::kDistiller) {
        return false;
    }
    int n = 0;
    for (int i = 0; i < colony.population; ++i) {
        n += i != except && colony.jobs[static_cast<std::size_t>(i)] == jobRow ? 1 : 0;
    }
    return n >= 3;
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
    if (!building || buildingLevel(colony, *building) == 0 || buildingFull(colony, jobRow, colonist)) {
        return choice;
    }
    const int base = buildingYield(buildingLevel(colony, *building), skillOf, jobRow, modifier);
    choice.amount = jobRow == job::kTeacher ? 0 : base;
    return choice;
}

} // namespace

std::vector<JobChoice> jobChoices(const SaveColony& colony, const SaveMap& map, const Ruleset& rules,
                                  const std::vector<SaveColony>& colonies, int colonist, int difficulty) {
    std::vector<JobChoice> out;
    if (colonist < 0 || colonist >= colony.population) {
        return out;
    }
    for (int j = 0; j <= job::kTeacher; ++j) {
        // Work in a building is offered only where the building stands.
        const auto building = buildingForJob(j);
        if (building && buildingLevel(colony, *building) == 0) {
            continue;
        }
        // Read from the original: teaching only for an expert the school
        // can take (a college for level 2, a university for level 3).
        if (j == job::kTeacher) {
            int skill = colony.skills[static_cast<std::size_t>(colonist)];
            if (skill == skill::kFreeColonist) {
                skill = skill::kServant;
            }
            const int level = skill >= 0 && static_cast<std::size_t>(skill) < rules.jobs.size()
                                  ? rules.jobs[static_cast<std::size_t>(skill)].skillLevel
                                  : 4;
            const int school = buildingLevel(colony, BuildingKind::Schoolhouse);
            if (level >= 4 || (level == 3 && school < 3) || (level == 2 && school < 2)) {
                continue;
            }
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
    if (onLand ? choice.square < 0
               : (!buildingForJob(jobRow) || buildingLevel(colony, *buildingForJob(jobRow)) == 0 ||
                  buildingFull(colony, jobRow, colonist))) {
        return false;
    }
    const int here = currentSquare(colony, colonist);
    if (here >= 0) {
        colony.squares[static_cast<std::size_t>(here)] = -1;
    }
    if (onLand) {
        colony.squares[static_cast<std::size_t>(choice.square)] = colonist;
    }
    if (colony.jobs[static_cast<std::size_t>(colonist)] != jobRow) {
        setJobTurns(colony, colonist, 0);
    }
    colony.jobs[static_cast<std::size_t>(colonist)] = static_cast<std::uint8_t>(jobRow);
    return true;
}

int jobTurns(const SaveColony& colony, int colonist) {
    if (colonist < 0 || colonist >= kMaxColonists) {
        return 0;
    }
    const int b = colony.raw[kJobTurnsAt + static_cast<std::size_t>(colonist / 2)];
    return (colonist % 2 == 0 ? b : b >> 4) & 0x0F;
}

void setJobTurns(SaveColony& colony, int colonist, int turns) {
    if (colonist < 0 || colonist >= kMaxColonists) {
        return;
    }
    auto& b = colony.raw[kJobTurnsAt + static_cast<std::size_t>(colonist / 2)];
    const int v = std::clamp(turns, 0, 15);
    b = static_cast<std::uint8_t>(colonist % 2 == 0 ? (b & 0xF0) | v : (b & 0x0F) | (v << 4));
}

bool assignJobAt(SaveColony& colony, const SaveMap& map, const std::vector<SaveColony>& colonies, int colonist,
                 int jobRow, int square) {
    if (colonist < 0 || colonist >= colony.population || jobRow < 0 || jobRow >= static_cast<int>(kSquareWorkCount) ||
        square < 0 || square >= 8) {
        return false;
    }
    const int here = currentSquare(colony, colonist);
    if (here != square && !squareFree(colony, map, colonies, square)) {
        return false;
    }
    if (here >= 0) {
        colony.squares[static_cast<std::size_t>(here)] = -1;
    }
    colony.squares[static_cast<std::size_t>(square)] = colonist;
    if (colony.jobs[static_cast<std::size_t>(colonist)] != jobRow) {
        setJobTurns(colony, colonist, 0);
    }
    colony.jobs[static_cast<std::size_t>(colonist)] = static_cast<std::uint8_t>(jobRow);
    return true;
}

bool exportTicked(const SaveColony& colony, int good) {
    constexpr std::size_t kExports = 138; // a bit for each good, from byte 138
    return good >= 0 && good < kGoodsKinds &&
           (colony.raw[kExports + static_cast<std::size_t>(good) / 8] & (1 << (good % 8))) != 0;
}

void setExportTicked(SaveColony& colony, int good, bool on) {
    constexpr std::size_t kExports = 138;
    if (good < 0 || good >= kGoodsKinds) {
        return;
    }
    auto& byte = colony.raw[kExports + static_cast<std::size_t>(good) / 8];
    const auto bit = static_cast<std::uint8_t>(1 << (good % 8));
    byte = static_cast<std::uint8_t>(on ? (byte | bit) : (byte & ~bit));
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

int constructionUnit(int construction) {
    if (construction < kFirstUnitConstruction || construction >= kFirstUnitConstruction + kUnitConstructionRows) {
        return -1;
    }
    return 11 + (construction - kFirstUnitConstruction);
}

int constructionHammers(const Ruleset& rules, int construction) {
    if (const int unit = constructionUnit(construction); unit >= 0) {
        if (static_cast<std::size_t>(unit) >= rules.units.size()) {
            return 0;
        }
        constexpr int kWagonTrain = 12;
        return unit == kWagonTrain ? 40 : rules.units[static_cast<std::size_t>(unit)].cost * 32;
    }
    if (construction < 0 || static_cast<std::size_t>(construction) >= rules.buildings.size()) {
        return 0;
    }
    return rules.buildings[static_cast<std::size_t>(construction)].hammers;
}

int constructionTools(const Ruleset& rules, int construction) {
    if (const int unit = constructionUnit(construction); unit >= 0) {
        return static_cast<std::size_t>(unit) < rules.units.size() ? rules.units[static_cast<std::size_t>(unit)].tools * 10
                                                                    : 0;
    }
    if (construction < 0 || static_cast<std::size_t>(construction) >= rules.buildings.size()) {
        return 0;
    }
    return rules.buildings[static_cast<std::size_t>(construction)].tools;
}

std::string constructionName(const Ruleset& rules, int construction) {
    if (const int unit = constructionUnit(construction); unit >= 0) {
        return static_cast<std::size_t>(unit) < rules.units.size() ? rules.units[static_cast<std::size_t>(unit)].name
                                                                    : std::string{};
    }
    if (construction < 0 || static_cast<std::size_t>(construction) >= rules.buildings.size()) {
        return {};
    }
    return rules.buildings[static_cast<std::size_t>(construction)].name;
}

BuildLimits buildLimits(const SaveGame& save, int nation) {
    BuildLimits limits;
    const NationEffects e = nationEffects(save, nation);
    constexpr int kSmith = 0, kStuyvesant = 3; // rows of @FATHERS
    limits.smith = e.has(kSmith);
    limits.stuyvesant = e.has(kStuyvesant);
    limits.colonies = 0;
    for (const auto& c : save.colonies) {
        limits.colonies += c.nation == nation ? 1 : 0;
    }
    constexpr int kWagonTrainType = 12;
    for (const auto& u : save.units) {
        limits.wagonTrains += u.nation == nation && u.type == kWagonTrainType ? 1 : 0;
    }
    return limits;
}

std::vector<BuildOption> buildOptions(const SaveColony& colony, const SaveMap& map, const Ruleset& rules,
                                      const BuildLimits& limits) {
    std::vector<BuildOption> options;
    const int population = colony.population;
    auto bigEnough = [&](int row) {
        return static_cast<std::size_t>(row) < rules.buildings.size() &&
               rules.buildings[static_cast<std::size_t>(row)].minColonySize <= population;
    };
    bool byWater = false;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if ((dx != 0 || dy != 0) && map.inside(colony.x + dx, colony.y + dy) &&
                map.at(colony.x + dx, colony.y + dy).isWater()) {
                byWater = true;
            }
        }
    }
    // Measured, and read from the original: the next level of each kind of
    // building. Never the town hall's upper levels or the capitol; the
    // factory level of the craft shops and the arsenal need Adam Smith, the
    // custom house Peter Stuyvesant, the drydock a colony on the coast.
    constexpr int kArsenal = 5;
    constexpr int kWarehouseRow = 15;
    constexpr std::size_t kColonyFlags = 28;
    constexpr std::uint8_t kCoastal = 0x40;
    auto offered = [&](int row) {
        switch (row) {
        case kArsenal:
        case 23: // textile mill
        case 26: // cigar factory
        case 29: // rum factory
        case 34: // fur factory
        case 41: // iron works
            return limits.smith;
        case 18: // custom house
            return limits.stuyvesant;
        case 7: // drydock
            return (colony.raw[kColonyFlags] & kCoastal) != 0;
        case 10:
        case 11: // town hall upper levels
        case 30:
        case 31: // capitol
            return false;
        case 6: // docks
            return byWater;
        default:
            return true;
        }
    };
    for (int row = 0; row < 42; ++row) {
        bool next = false;
        if (row == kWarehouseRow || row == kWarehouseRow + 1) {
            next = colony.raw[149] == row - kWarehouseRow;
        } else {
            for (int k = 0; k < kBuildingKinds; ++k) {
                const auto& f = kBuildingFields[static_cast<std::size_t>(k)];
                if (f.firstRow >= 0 && row >= f.firstRow && row < f.firstRow + f.bits) {
                    next = buildingLevel(colony, static_cast<BuildingKind>(k)) == row - f.firstRow;
                }
            }
        }
        if (row == colony.construction && !next) {
            options.push_back({row, true});
        } else if (next && offered(row) && bigEnough(row)) {
            options.push_back({row, false});
        }
    }
    // Units: artillery with an armory, a wagon train while the nation has
    // fewer of them than colonies, ships with a shipyard (the frigate too,
    // though it never gets built).
    const bool armory = buildingLevel(colony, BuildingKind::Armory) > 0;
    const bool shipyard = buildingLevel(colony, BuildingKind::Docks) >= 3;
    for (int value = kFirstUnitConstruction; value < kFirstUnitConstruction + kUnitConstructionRows; ++value) {
        const int unit = constructionUnit(value);
        const bool wagons = unit == 12 && limits.wagonTrains < limits.colonies;
        if ((unit == 11 && armory) || wagons || (unit >= 13 && shipyard)) {
            options.push_back({value, false});
        }
    }
    return options;
}

int constructionPrice(const SaveColony& colony, const Ruleset& rules, const SaveNation& nation) {
    const int hammers = constructionHammers(rules, colony.construction);
    if (hammers <= 0) {
        return 0;
    }
    const int missingHammers = std::max(0, hammers - colony.hammers);
    const int missingTools = std::max(0, constructionTools(rules, colony.construction) - colony.stock[cargo::kTools]);
    // Measured: 13 a hammer, and each tool 4 more than Europe asks for it;
    // the tax rate plays no part. Twice as much before the first hammer.
    const int burden = static_cast<std::size_t>(cargo::kTools) < rules.cargo.size()
                           ? rules.cargo[static_cast<std::size_t>(cargo::kTools)].burden
                           : 0;
    const int toolPrice = nation.prices[static_cast<std::size_t>(cargo::kTools)] + burden + 4;
    int price = 13 * missingHammers + toolPrice * missingTools;
    if (colony.hammers == 0) {
        price *= 2;
    }
    return price;
}

void removeColonist(SaveColony& colony, int colonist) {
    const int population = std::clamp(colony.population, 0, kMaxColonists);
    if (colonist < 0 || colonist >= population) {
        return;
    }
    for (int i = colonist; i + 1 < population; ++i) {
        colony.jobs[static_cast<std::size_t>(i)] = colony.jobs[static_cast<std::size_t>(i + 1)];
        colony.skills[static_cast<std::size_t>(i)] = colony.skills[static_cast<std::size_t>(i + 1)];
        setJobTurns(colony, i, jobTurns(colony, i + 1));
    }
    setJobTurns(colony, population - 1, 0);
    colony.jobs[static_cast<std::size_t>(population - 1)] = 0;
    colony.skills[static_cast<std::size_t>(population - 1)] = 0;
    // Measured: the squares of the starved are left to nobody and the
    // others keep theirs under their new numbers.
    for (auto& who : colony.squares) {
        if (who == colonist) {
            who = -1;
        } else if (who > colonist) {
            --who;
        }
    }
    colony.population = population - 1;
    // Read from the original: the Sons of Liberty total loses the 100 a
    // colonist brought.
    colony.rebelDivisor -= 100;
}

ColonyTurn runColonyTurn(SaveColony& colony, const SaveMap& map, const Ruleset& rules, int difficulty,
                         bool tories, const NationEffects& nation) {
    ColonyTurn turn;
    const auto before = colony.stock;
    turn.foodBefore = before[cargo::kFood];
    const auto p = colonyProduction(colony, map, rules, difficulty, tories, nation);
    std::array<int, cargo::kCount> made = p.produced;

    // Workshops: as much as their raw good allows, from this turn's
    // harvest first and then from the warehouse.
    std::array<int, cargo::kCount> wanted{};
    std::array<int, cargo::kCount> wantedOutput{};
    std::array<int, cargo::kCount> outputOf{};
    for (std::size_t i = 0; i < p.colonists.size(); ++i) {
        const auto& work = p.colonists[i];
        if (!work.building || work.good < 0) {
            continue;
        }
        const int input = jobInput(colony.jobs[i]);
        if (input >= 0) {
            wanted[static_cast<std::size_t>(input)] += work.uses;
            wantedOutput[static_cast<std::size_t>(input)] += work.amount;
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
        // Measured: with 1 fur for a fur trader the coat is made and nothing
        // is said; the turn after, with none, "has run out of furs".
        if (available <= 0 && raw < kGoodsKinds) {
            turn.ranOut[r] = true;
        }
        // A factory makes half as much again from what it gets.
        const int output = static_cast<int>(static_cast<long long>(wantedOutput[r]) * used / wanted[r]);
        made[static_cast<std::size_t>(outputOf[r])] -= wantedOutput[r] - output;
        made[r] -= used; // negative: taken from the warehouse
    }

    // Food, and the horses bred from what is left over.
    made[cargo::kFood] -= p.foodEaten;
    const int born = horsesBred(colony, made[cargo::kFood]);
    made[cargo::kFood] -= born;
    made[cargo::kHorses] += born;
    turn.horsesBred = born;
    turn.foodShort = std::max(0, -made[cargo::kFood]);
    for (int good = 0; good < kGoodsKinds; ++good) {
        colony.stock[static_cast<std::size_t>(good)] += made[static_cast<std::size_t>(good)];
    }
    // Read from the original: as each good comes in, a custom house sells
    // all but 50 of it once there are 100. The player ticks the goods; the
    // computer sells all but food, lumber, horses, tools and muskets, and
    // ore only without an armory while the colony makes no tools or
    // muskets.
    for (int good = 0; good < kGoodsKinds && nation.customs != NationEffects::Customs::None; ++good) {
        const auto g = static_cast<std::size_t>(good);
        bool sells = false;
        if (nation.customs == NationEffects::Customs::Ticked) {
            sells = exportTicked(colony, good);
        } else {
            sells = good != cargo::kFood && good != cargo::kLumber && good != cargo::kHorses && good != cargo::kTools &&
                    good != cargo::kMuskets;
            if (good == cargo::kOre) {
                sells = buildingLevel(colony, BuildingKind::Armory) == 0 && made[cargo::kTools] == 0 &&
                        made[cargo::kMuskets] == 0;
            }
        }
        if (sells && colony.stock[g] >= 100) {
            turn.exported[g] = colony.stock[g] - 50;
            colony.stock[g] = 50;
        }
    }
    auto& food = colony.stock[cargo::kFood];
    if (food >= 200) {
        food -= 200;
        turn.grew = true;
    } else if (food < 0) {
        turn.foodMissing = -food;
        food = 0;
        turn.foodRanOut = true;
    }

    // Construction. Measured: whatever hammers are left over are lost when
    // something is finished.
    colony.hammers += std::max(0, made[cargo::kHammers]);
    const int needHammers = constructionHammers(rules, colony.construction);
    if (needHammers > 0 && colony.hammers >= needHammers && colony.construction != kFrigateConstruction) {
        const int tools = constructionTools(rules, colony.construction);
        // Read from the original: the tools are taken before it checks
        // whether the thing may be built at all; a wagon train over the
        // limit, or a second warehouse expansion, keeps its hammers (and
        // loses those tools) and is tried again the next turn.
        constexpr int kWagonTrainUnit = 12;
        constexpr int kExpansionRow = 16;
        if (colony.stock[cargo::kTools] < tools) {
            turn.toolsNeeded = tools;
        } else if (constructionUnit(colony.construction) == kWagonTrainUnit && nation.wagonsFull) {
            colony.stock[cargo::kTools] -= tools;
            turn.noMoreWagons = true;
        } else if (colony.construction == kExpansionRow && colony.raw[149] >= 2) {
            colony.stock[cargo::kTools] -= tools;
            turn.noMoreWarehouse = true;
        } else if (const int unit = constructionUnit(colony.construction); unit >= 0) {
            colony.hammers = 0;
            colony.stock[cargo::kTools] -= tools;
            turn.builtUnit = unit;
        } else {
            for (int k = 0; k < kBuildingKinds; ++k) {
                const auto& f = kBuildingFields[static_cast<std::size_t>(k)];
                if (f.firstRow >= 0 && colony.construction >= f.firstRow && colony.construction < f.firstRow + f.bits) {
                    setBuildingLevel(colony, static_cast<BuildingKind>(k), colony.construction - f.firstRow + 1);
                }
            }
            // Measured: the expansion only raises byte 149 (1 warehouse,
            // 2 expansion), which sets what the warehouse holds.
            constexpr int kWarehouseRow = 15;
            if (colony.construction == kWarehouseRow || colony.construction == kWarehouseRow + 1) {
                colony.raw[149] = static_cast<std::uint8_t>(
                    std::max<int>(colony.raw[149], colony.construction - kWarehouseRow + 1));
            }
            colony.hammers = 0;
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
        // Measured: 99 to 102 cotton, 199 to 200 and 299 to 300 each make
        // a new cargo; 150 down to 100 does not.
        const int was = std::max(0, before[static_cast<std::size_t>(good)]);
        turn.cargoReady[static_cast<std::size_t>(good)] = amount / 100 > was / 100;
    }

    // Bells: membership moves, and then the bonus the next turn works with.
    turn.libertyBefore = colony.libertyBonus;
    turn.bells = p.produced[cargo::kBells];
    gatherBells(colony, p.produced[cargo::kBells]);
    updateLibertyBonus(colony, nation.libertyExtra(tories));
    turn.libertyAfter = colony.libertyBonus;
    return turn;
}

int sonsOfLibertyPercent(const SaveColony& colony, int extra) {
    const int percent = colony.rebelDivisor <= 0
                            ? 0
                            : std::clamp(static_cast<int>(100LL * colony.rebelDividend / colony.rebelDivisor), 0, 100);
    return std::clamp(percent + extra, 0, 100);
}

int rebelCount(const SaveColony& colony, int extra) {
    // Measured: 5% of 10 is no rebel, 36% of 2 is one; halves round down.
    return (colony.population * sonsOfLibertyPercent(colony, extra) + 49) / 100;
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

void updateLibertyBonus(SaveColony& colony, int extra) {
    const int percent = sonsOfLibertyPercent(colony, extra);
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
