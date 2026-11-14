// SPDX-License-Identifier: GPL-3.0-or-later
//
// Moving people on the colony screen: between jobs, out of the colony and
// into it. Read from the original with a debugger and checked on its colony
// screen; see docs/rules/colony-screen.md, "Moving people".
#include "viceroy/game/colony.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/resource.hpp"

#include <algorithm>

namespace viceroy {

namespace {

constexpr std::uint8_t kPeace = 0x40;
constexpr int kStockadeKeeps = 3; // colonists a stockade keeps
constexpr int kPerBuilding = 3;
constexpr int kEquipment = 50; // muskets or horses a unit carries
constexpr int kToolsStep = 20;
constexpr int kMostTools = 100;
constexpr int kConvert = 27;  // the skill of an Indian convert
constexpr int kJesuit = 0x18; // the skill of a Jesuit missionary

// The job of a unit outside the colony, by its type (soldiers of the
// Continental Army count as soldiers, cavalry as dragoons).
int outsideJobOf(int type) {
    switch (type) {
    case unit_type::kColonist:
        return outside_job::kColonist;
    case unit_type::kSoldier:
    case 6:
    case 9:
        return outside_job::kSoldier;
    case unit_type::kPioneer:
        return outside_job::kPioneer;
    case unit_type::kMissionary:
        return outside_job::kMissionary;
    case unit_type::kDragoon:
    case 7:
    case 8:
        return outside_job::kDragoon;
    case unit_type::kScout:
        return outside_job::kScout;
    default:
        return -1;
    }
}

int unitTypeFor(int job) {
    switch (job) {
    case outside_job::kSoldier:
        return unit_type::kSoldier;
    case outside_job::kPioneer:
        return unit_type::kPioneer;
    case outside_job::kMissionary:
        return unit_type::kMissionary;
    case outside_job::kDragoon:
        return unit_type::kDragoon;
    case outside_job::kScout:
        return unit_type::kScout;
    default:
        return unit_type::kColonist;
    }
}

bool needsMuskets(int job) { return job == outside_job::kSoldier || job == outside_job::kDragoon; }

bool needsHorses(int job) { return job == outside_job::kScout || job == outside_job::kDragoon; }

// A skill as the colony keeps it (units keep a free colonist as 19).
int colonySkill(int profession) { return profession == skill::kFreeColonistUnit ? skill::kFreeColonist : profession; }

} // namespace

int Game::currentJob(int c, ColonyMover who) const {
    const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    if (who.colonist >= 0) {
        return colony.jobs[static_cast<std::size_t>(who.colonist)];
    }
    return who.unit >= 0 ? outsideJobOf(unit(who.unit).type) : -1;
}

bool Game::underSiege(int c) const {
    // Armed units (an attack of 2 or more, ships aside) on the colony's
    // square and the eight around it: its own nation's against those of the
    // European nations it has no peace with. Of each square only the nation
    // of the units there counts.
    const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    const auto armed = [&](int x, int y, int& nation) {
        int n = 0;
        nation = -1;
        for (const int u : unitsAt(x, y)) {
            const auto& su = unit(u);
            if (nation < 0) {
                nation = su.nation & 0x0F;
            }
            const int type = su.type;
            if (!isShip(u) && type >= 0 && type < static_cast<int>(rules_.units.size()) &&
                rules_.units[static_cast<std::size_t>(type)].attack > 1) {
                ++n;
            }
        }
        return n;
    };
    int nation = -1;
    int friendly = save_.map.inside(colony.x, colony.y) ? armed(colony.x, colony.y, nation) : 0;
    int enemy = 0;
    for (const auto& step : kSteps) {
        const int x = colony.x + step.dx;
        const int y = colony.y + step.dy;
        if (!save_.map.inside(x, y)) {
            continue;
        }
        const int n = armed(x, y, nation);
        if (nation < 0 || nation >= kEuropeanNations) {
            continue;
        }
        if (nation == colony.nation) {
            friendly += n;
        } else if ((relation(colony.nation, nation) & kPeace) == 0) {
            enemy += n;
        }
    }
    return enemy > friendly;
}

ColonyMove Game::checkColonyMove(int c, ColonyMover who, int job) const {
    const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    const int now = currentJob(c, who);
    if (now == job) {
        return ColonyMove::Same;
    }
    const bool inside = who.colonist >= 0;
    const bool toInside = job < outside_job::kColonist;
    const auto has = [&](BuildingKind kind) { return buildingLevel(colony, kind) > 0; };
    if (inside && !toInside) {
        if (has(BuildingKind::Fortification) && colony.population <= kStockadeKeeps) {
            return ColonyMove::KeepStockade;
        }
        if (job != outside_job::kSoldier && job != outside_job::kDragoon && underSiege(c)) {
            return ColonyMove::Siege;
        }
        if (colony.population == 1) {
            return ColonyMove::Abandon;
        }
    }
    if (!inside && toInside && colony.population >= kMaxColonists) {
        return ColonyMove::Full;
    }
    if (job == job::kFisherman && !has(BuildingKind::Docks)) {
        return ColonyMove::NoDocks;
    }
    if (job == job::kTeacher) {
        int teachers = 0;
        for (int i = 0; i < colony.population; ++i) {
            teachers += colony.jobs[static_cast<std::size_t>(i)] == job::kTeacher ? 1 : 0;
        }
        const int school = buildingLevel(colony, BuildingKind::Schoolhouse);
        if (school >= 3 && teachers >= 3) {
            return ColonyMove::Univ3;
        }
        if (school == 2 && teachers >= 2) {
            return ColonyMove::College2;
        }
        if (school == 1 && teachers >= 1) {
            return ColonyMove::School1;
        }
        // The free colonist's own row (19) says 4 as well: none may teach.
        int skill = inside ? colony.skills[static_cast<std::size_t>(who.colonist)] : unit(who.unit).profession;
        if (skill == skill::kFreeColonist) {
            skill = skill::kServant;
        }
        const int level = skill >= 0 && skill < static_cast<int>(rules_.jobs.size())
                              ? rules_.jobs[static_cast<std::size_t>(skill)].skillLevel
                              : 4;
        if (level > 3) {
            return ColonyMove::NoTeacher;
        }
        if (level == 3 && school < 3) {
            return ColonyMove::NeedUniversity;
        }
        if (level == 2 && school < 2) {
            return ColonyMove::NeedCollege;
        }
    }
    // Read from the original: three to a building, counted by job, for the
    // jobs after the distiller's. The distiller's house takes a fourth, as
    // in the original.
    if (job > job::kDistiller && toInside) {
        int there = 0;
        for (int i = 0; i < colony.population; ++i) {
            if (i != who.colonist && colony.jobs[static_cast<std::size_t>(i)] == job) {
                ++there;
            }
        }
        if (there >= kPerBuilding) {
            return ColonyMove::MoreThanThree;
        }
    }
    return ColonyMove::Allowed;
}

int Game::jobForSquare(int c, ColonyMover who, int square) const {
    // On water only fishing; on land the present job when it is work on the
    // land, else the work that yields most there.
    const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    const int x = colony.x + kColonySquares[static_cast<std::size_t>(square)].dx;
    const int y = colony.y + kColonySquares[static_cast<std::size_t>(square)].dy;
    if (!save_.map.inside(x, y)) {
        return -1;
    }
    const auto& sq = save_.map.at(x, y);
    if (sq.isWater()) {
        return job::kFisherman;
    }
    const int now = currentJob(c, who);
    if (now >= 0 && now < job::kFisherman) {
        return now;
    }
    const int skill = who.colonist >= 0 ? colony.skills[static_cast<std::size_t>(who.colonist)]
                                        : colonySkill(unit(who.unit).profession);
    const auto modifier = workModifier(colony, save_.difficulty);
    const bool coast = landAround(save_.map, x, y) >= 3;
    int best = job::kFarmer;
    int most = -1;
    for (int j = 0; j < job::kFisherman; ++j) {
        const int amount = squareYield(rules_, sq, j, skill, modifier, coast, resourceAt(save_.map, x, y));
        if (amount > most) {
            most = amount;
            best = j;
        }
    }
    return best;
}

std::vector<OutsideChoice> Game::outsideChoices(int c, ColonyMover who) const {
    const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    // What the person carries now counts as if it were in the warehouse.
    int muskets = colony.stock[cargo::kMuskets];
    int horses = colony.stock[cargo::kHorses];
    int tools = colony.stock[cargo::kTools];
    int skill = 0;
    if (who.colonist >= 0) {
        skill = colony.skills[static_cast<std::size_t>(who.colonist)];
    } else if (who.unit >= 0) {
        const auto& u = unit(who.unit);
        const int now = outsideJobOf(u.type);
        muskets += needsMuskets(now) ? kEquipment : 0;
        horses += needsHorses(now) ? kEquipment : 0;
        tools += now == outside_job::kPioneer ? u.tools : 0;
        skill = colonySkill(u.profession);
    }
    std::vector<OutsideChoice> out;
    const bool church = buildingLevel(colony, BuildingKind::Church) > 0;
    for (int job = outside_job::kColonist; job <= outside_job::kMissionary; ++job) {
        if (job != outside_job::kColonist && skill == kConvert) {
            continue; // converts go out only as colonists
        }
        if (job == outside_job::kMissionary && !church && skill != kJesuit) {
            continue;
        }
        OutsideChoice choice;
        choice.job = job;
        if (needsMuskets(job) && muskets < kEquipment) {
            choice.affordable = false;
        }
        if (needsHorses(job) && horses < kEquipment) {
            choice.affordable = false;
        }
        if (job == outside_job::kPioneer) {
            choice.tools = std::clamp(tools / kToolsStep * kToolsStep, kToolsStep, kMostTools);
            choice.affordable = choice.affordable && tools >= kToolsStep;
        }
        out.push_back(choice);
    }
    return out;
}

int Game::moveColonist(int c, ColonyMover who, int job, int square) {
    auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    const auto take = [&](int good, int amount) {
        auto& s = colony.stock[static_cast<std::size_t>(good)];
        const int taken = std::min(s, amount);
        s -= taken;
        return taken;
    };
    // A unit outside gives back what it carries before anything else.
    if (who.unit >= 0) {
        auto& u = save_.units[static_cast<std::size_t>(who.unit)];
        const int now = outsideJobOf(u.type);
        if (needsMuskets(now)) {
            colony.stock[cargo::kMuskets] += kEquipment;
        }
        if (needsHorses(now)) {
            colony.stock[cargo::kHorses] += kEquipment;
        }
        if (now == outside_job::kPioneer) {
            colony.stock[cargo::kTools] += u.tools;
            u.tools = 0;
        }
    }
    if (job >= outside_job::kColonist) {
        int index = who.unit;
        if (who.colonist >= 0) {
            const int skill = colony.skills[static_cast<std::size_t>(who.colonist)];
            addColonist(colony.x, colony.y, colony.nation);
            index = unitCount() - 1;
            save_.units[static_cast<std::size_t>(index)].profession =
                static_cast<std::uint8_t>(skill == skill::kFreeColonist ? skill::kFreeColonistUnit : skill);
            removeColonist(colony, who.colonist);
        }
        auto& u = save_.units[static_cast<std::size_t>(index)];
        u.type = static_cast<std::uint8_t>(unitTypeFor(job));
        if (needsMuskets(job)) {
            take(cargo::kMuskets, kEquipment);
        }
        if (needsHorses(job)) {
            take(cargo::kHorses, kEquipment);
        }
        if (job == outside_job::kPioneer) {
            // Tools in twenties, at most 100.
            const int tools = std::clamp(colony.stock[cargo::kTools] / kToolsStep * kToolsStep, kToolsStep, kMostTools);
            u.tools = static_cast<std::uint8_t>(take(cargo::kTools, tools));
        }
        if (colony.population == 0) {
            removeColony(c);
        }
        refreshUnitFlags();
        return index;
    }
    int colonist = who.colonist;
    if (who.unit >= 0) {
        colonist = colony.population;
        const auto at = static_cast<std::size_t>(colonist);
        colony.skills[at] = static_cast<std::uint8_t>(colonySkill(unit(who.unit).profession));
        colony.jobs[at] = static_cast<std::uint8_t>(job::kStatesman);
        setJobTurns(colony, colonist, 0);
        ++colony.population;
        joinLiberty(colony, save_.map, rules_, save_.difficulty);
        removeUnit(who.unit);
        refreshUnitFlags();
    }
    if (square >= 0) {
        assignJobAt(colony, save_.map, save_.colonies, colonist, job, square);
    } else {
        assignJob(colony, save_.map, rules_, save_.colonies, colonist, job);
    }
    return -1;
}

void Game::teach(int c) {
    // Read from the original. Every colonist's turns at their job go up by
    // one (to 15 at most). A teacher finishes a lesson after 4, 6 or 8
    // turns (by the level of their skill), three lessons at most a turn,
    // and starts again. Each lesson goes to one of the colony's criminals,
    // servants and free colonists, picked at random: a criminal becomes a
    // servant, a servant a free colonist, a free colonist learns the
    // teacher's skill.
    auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    std::vector<int> students;
    std::vector<int> lessons;
    for (int i = 0; i < colony.population; ++i) {
        const auto ui = static_cast<std::size_t>(i);
        int turns = jobTurns(colony, i) + 1;
        const int skill = colony.skills[ui];
        if (skill == skill::kCriminal || skill == skill::kServant || skill == skill::kFreeColonist ||
            skill == skill::kFreeColonistUnit) {
            students.push_back(i);
        }
        if (colony.jobs[ui] == job::kTeacher && lessons.size() < 3) {
            const int row = skill == skill::kFreeColonist ? skill::kServant : skill;
            const int level = row >= 0 && static_cast<std::size_t>(row) < rules_.jobs.size()
                                  ? rules_.jobs[static_cast<std::size_t>(row)].skillLevel
                                  : 4;
            const int need = level == 1 ? 4 : level == 2 ? 6 : 8;
            if (level < 4 && turns >= need) {
                lessons.push_back(skill);
                turns = 0;
            }
        }
        setJobTurns(colony, i, turns);
    }
    const bool told = colony.nation == human() && colonyReportOn(colony_report::kTraining);
    for (const int lesson : lessons) {
        if (students.empty()) {
            if (told) {
                notices_.push_back(GameNotice{"TRAINFAIL", {colony.name}, -1, {}, c});
            }
            break;
        }
        const auto pick = static_cast<std::size_t>(nextRandom(static_cast<int>(students.size())));
        auto& skill = colony.skills[static_cast<std::size_t>(students[pick])];
        GameNotice notice{"", {colony.name}, -1, {}, c};
        if (skill == skill::kCriminal) {
            skill = static_cast<std::uint8_t>(skill::kServant);
            notice.section = "TRAINCRIMINAL";
        } else if (skill == skill::kServant) {
            skill = static_cast<std::uint8_t>(skill::kFreeColonist);
            notice.section = "TRAININDENTURED";
        } else {
            skill = static_cast<std::uint8_t>(lesson);
            notice.section = "TRAINPROFESSION";
            notice.strings.push_back(static_cast<std::size_t>(lesson) < rules_.jobs.size()
                                         ? rules_.jobs[static_cast<std::size_t>(lesson)].name
                                         : std::string());
        }
        if (told) {
            notices_.push_back(std::move(notice));
        }
        students.erase(students.begin() + static_cast<std::ptrdiff_t>(pick));
    }
}

void Game::learnByDoing(int c) {
    // Read from the original. A colonist who is not an expert (and no
    // convert) and works as a sugar, tobacco or cotton planter or a fur
    // trapper may learn that job, but only while the nation has no expert
    // of it: no colonist in its colonies and no colonist, soldier, pioneer,
    // missionary, dragoon or scout of it holds the skill. The chance a turn
    // is 1 in 100 for a free colonist, 1 in 200 for a servant and 1 in 300
    // for a criminal.
    auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    std::array<int, 32> experts{};
    auto count = [&experts](int skill) {
        if (skill >= 0 && static_cast<std::size_t>(skill) < experts.size()) {
            ++experts[static_cast<std::size_t>(skill)];
        }
    };
    for (const auto& u : save_.units) {
        // The unit types that are people (an unarmed colonist up to the
        // Continental army); ships, wagons, artillery and natives are not.
        static constexpr std::array<bool, 10> kPeople{true, true, true, true, true, true, false, true, false, true};
        if (u.nation == colony.nation && u.type >= 0 && u.type < static_cast<int>(kPeople.size()) &&
            kPeople[static_cast<std::size_t>(u.type)]) {
            count(u.profession);
        }
    }
    for (const auto& other : save_.colonies) {
        if (other.nation == colony.nation) {
            for (int i = 0; i < other.population; ++i) {
                count(other.skills[static_cast<std::size_t>(i)]);
            }
        }
    }
    const bool told = colony.nation == human() && colonyReportOn(colony_report::kTraining);
    for (int i = 0; i < colony.population; ++i) {
        const auto ui = static_cast<std::size_t>(i);
        const int job = colony.jobs[ui];
        auto& skill = colony.skills[ui];
        if (job < job::kSugarPlanter || job > job::kFurTrapper || skill == skill::kConvert || isExpert(skill) ||
            experts[static_cast<std::size_t>(job)] != 0) {
            continue;
        }
        const int range = skill == skill::kServant ? 200 : skill == skill::kCriminal ? 300 : 100;
        if (nextRandom(range) != 0) {
            continue;
        }
        ++experts[static_cast<std::size_t>(job)];
        skill = static_cast<std::uint8_t>(job);
        if (told) {
            notices_.push_back(GameNotice{"TRAINPROFESSION",
                                          {colony.name, static_cast<std::size_t>(job) < rules_.jobs.size()
                                                            ? rules_.jobs[static_cast<std::size_t>(job)].name
                                                            : std::string()},
                                          -1,
                                          {},
                                          c});
        }
    }
}

} // namespace viceroy
