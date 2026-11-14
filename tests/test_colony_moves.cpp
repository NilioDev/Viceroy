// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/game/colony.hpp"
#include "viceroy/game/game.hpp"

using namespace viceroy;

namespace {

constexpr int kDutch = 3;
constexpr int kEnglish = 0;

Ruleset moveRules() {
    Ruleset r;
    r.terrain.resize(27);
    r.units.resize(14);
    r.units[unit_type::kColonist] = {"Colonists", 101, 1, 0, 1, 0, 1, 1, 0, 0, 0, 0};
    r.units[unit_type::kSoldier] = {"Soldiers", 103, 1, 2, 2, 0, 1, 2, 0, 0, 0, 0};
    r.units[unit_type::kPioneer] = {"Pioneers", 102, 1, 0, 1, 0, 1, 2, 0, 0, 0, 0};
    r.units[unit_type::kDragoon] = {"Dragoons", 105, 4, 3, 3, 0, 1, 3, 0, 0, 0, 0};
    r.units[unit_type::kScout] = {"Scouts", 104, 4, 1, 1, 0, 1, 2, 0, 0, 0, 0};
    // The teaching levels of @JOB.
    const int levels[28] = {1, 2, 2, 2, 1, 1, 1, 1, 1, 2, 2, 2, 2, 1, 2, 2, 3, 3, 4, 4, 1, 2, 1, 2, 3, 4, 4, 4};
    for (int i = 0; i < 28; ++i) {
        JobType j;
        j.name = "Job" + std::to_string(i);
        j.skillLevel = levels[i];
        r.jobs.push_back(j);
    }
    return r;
}

// A 9 x 9 land of plains with the Dutch colony in the middle and its
// colonists in the town hall.
SaveGame moveSave(int people) {
    SaveGame s;
    s.humanNation = kDutch;
    s.map.width = 9;
    s.map.height = 9;
    s.map.squares.resize(81);
    for (auto& sq : s.map.squares) {
        sq.terrain = 2;
        sq.region = 2;
    }
    for (int n = 0; n < 4; ++n) {
        s.players[static_cast<std::size_t>(n)].control = n == kDutch ? 0 : 2;
    }
    SaveColony c;
    c.x = 4;
    c.y = 4;
    c.nation = kDutch;
    c.name = "Fort Nassau";
    c.population = people;
    for (int i = 0; i < people; ++i) {
        c.jobs[static_cast<std::size_t>(i)] = job::kStatesman;
        c.skills[static_cast<std::size_t>(i)] = skill::kFreeColonist;
    }
    c.squares.fill(-1);
    setBuildingLevel(c, BuildingKind::TownHall, 1);
    s.colonies.push_back(c);
    s.map.at(4, 4).flags |= MapSquare::kHasColony;
    return s;
}

SaveUnit unitAt(int x, int y, int type, int nation) {
    SaveUnit u;
    u.x = static_cast<std::uint8_t>(x);
    u.y = static_cast<std::uint8_t>(y);
    u.type = static_cast<std::uint8_t>(type);
    u.nation = static_cast<std::uint8_t>(nation);
    u.profession = static_cast<std::uint8_t>(skill::kFreeColonistUnit);
    return u;
}

} // namespace

VCR_TEST("colony moves: a stockade keeps three, the last colonist abandons (read from the original)") {
    {
        auto s = moveSave(3);
        setBuildingLevel(s.colonies[0], BuildingKind::Fortification, 1);
        Game g(s, moveRules());
        VCR_CHECK(g.checkColonyMove(0, {0, -1}, outside_job::kColonist) == ColonyMove::KeepStockade);
        g.state().colonies[0].population = 4;
        VCR_CHECK(g.checkColonyMove(0, {0, -1}, outside_job::kColonist) == ColonyMove::Allowed);
        // Moving inside is no business of the stockade's.
        VCR_CHECK(g.checkColonyMove(0, {0, -1}, job::kStatesman) == ColonyMove::Same);
    }
    {
        Game g(moveSave(1), moveRules());
        VCR_CHECK(g.checkColonyMove(0, {0, -1}, outside_job::kColonist) == ColonyMove::Abandon);
        const int u = g.moveColonist(0, {0, -1}, outside_job::kColonist);
        VCR_CHECK(g.state().colonies.empty());
        VCR_CHECK(u >= 0 && g.unit(u).x == 4 && g.unit(u).y == 4);
        VCR_CHECK(g.unit(u).profession == skill::kFreeColonistUnit);
    }
}

VCR_TEST("colony moves: room, docks and three to a building, but four distillers (read from the original)") {
    {
        auto s = moveSave(kMaxColonists);
        s.units.push_back(unitAt(4, 4, unit_type::kColonist, kDutch));
        Game g(s, moveRules());
        VCR_CHECK(g.checkColonyMove(0, {-1, 0}, job::kStatesman) == ColonyMove::Full);
    }
    {
        auto s = moveSave(5);
        auto& c = s.colonies[0];
        setBuildingLevel(c, BuildingKind::Carpenter, 1);
        setBuildingLevel(c, BuildingKind::Distiller, 1);
        for (int i = 0; i < 3; ++i) {
            c.jobs[static_cast<std::size_t>(i)] = job::kCarpenter;
        }
        Game g(s, moveRules());
        VCR_CHECK(g.checkColonyMove(0, {3, -1}, job::kFisherman) == ColonyMove::NoDocks);
        VCR_CHECK(g.checkColonyMove(0, {3, -1}, job::kCarpenter) == ColonyMove::MoreThanThree);
        for (int i = 0; i < 3; ++i) {
            g.state().colonies[0].jobs[static_cast<std::size_t>(i)] = job::kDistiller;
        }
        VCR_CHECK(g.checkColonyMove(0, {3, -1}, job::kDistiller) == ColonyMove::Allowed);
        g.moveColonist(0, {3, -1}, job::kDistiller);
        VCR_CHECK(g.state().colonies[0].jobs[3] == job::kDistiller);
    }
}

VCR_TEST("colony moves: who may teach where (read from the original)") {
    auto s = moveSave(4);
    auto& c = s.colonies[0];
    setBuildingLevel(c, BuildingKind::Schoolhouse, 1);
    c.skills[1] = static_cast<std::uint8_t>(job::kFarmer);    // level 1
    c.skills[2] = static_cast<std::uint8_t>(job::kDistiller); // level 2
    c.skills[3] = static_cast<std::uint8_t>(job::kStatesman); // level 3
    Game g(s, moveRules());
    VCR_CHECK(g.checkColonyMove(0, {0, -1}, job::kTeacher) == ColonyMove::NoTeacher);
    VCR_CHECK(g.checkColonyMove(0, {1, -1}, job::kTeacher) == ColonyMove::Allowed);
    VCR_CHECK(g.checkColonyMove(0, {2, -1}, job::kTeacher) == ColonyMove::NeedCollege);
    VCR_CHECK(g.checkColonyMove(0, {3, -1}, job::kTeacher) == ColonyMove::NeedUniversity);
    g.moveColonist(0, {1, -1}, job::kTeacher);
    VCR_CHECK(g.checkColonyMove(0, {2, -1}, job::kTeacher) == ColonyMove::School1);
    setBuildingLevel(g.state().colonies[0], BuildingKind::Schoolhouse, 2);
    VCR_CHECK(g.checkColonyMove(0, {2, -1}, job::kTeacher) == ColonyMove::Allowed);
    // The jobs menu offers teaching only to those who may.
    const auto& col = g.state().colonies[0];
    const auto offers = [&](int colonist) {
        for (const auto& choice : jobChoices(col, g.state().map, g.rules(), g.state().colonies, colonist)) {
            if (choice.job == job::kTeacher) {
                return true;
            }
        }
        return false;
    };
    VCR_CHECK(!offers(0));
    VCR_CHECK(offers(2));
    VCR_CHECK(!offers(3));
}

VCR_TEST("colony moves: a siege keeps all but soldiers in (read from the original)") {
    auto s = moveSave(4);
    s.units.push_back(unitAt(5, 4, unit_type::kSoldier, kEnglish));
    Game g(s, moveRules());
    VCR_CHECK(g.underSiege(0));
    VCR_CHECK(g.checkColonyMove(0, {0, -1}, outside_job::kColonist) == ColonyMove::Siege);
    VCR_CHECK(g.checkColonyMove(0, {0, -1}, outside_job::kSoldier) == ColonyMove::Allowed);
    // A soldier of our own on the square holds them off.
    g.state().units.push_back(unitAt(4, 4, unit_type::kSoldier, kDutch));
    VCR_CHECK(!g.underSiege(0));
}

VCR_TEST("colony moves: going out takes muskets, horses and tools; coming in gives them back") {
    auto s = moveSave(4);
    auto& c = s.colonies[0];
    c.stock[cargo::kMuskets] = 60;
    c.stock[cargo::kHorses] = 40;
    c.stock[cargo::kTools] = 75;
    Game g(s, moveRules());
    const auto choices = g.outsideChoices(0, {0, -1});
    VCR_CHECK(choices.size() == 5); // no church: no missionary
    VCR_CHECK(choices[1].job == outside_job::kPioneer && choices[1].affordable && choices[1].tools == 60);
    VCR_CHECK(choices[2].job == outside_job::kSoldier && choices[2].affordable);
    VCR_CHECK(choices[3].job == outside_job::kScout && !choices[3].affordable);
    VCR_CHECK(choices[4].job == outside_job::kDragoon && !choices[4].affordable);
    const int soldier = g.moveColonist(0, {0, -1}, outside_job::kSoldier);
    VCR_CHECK(g.unit(soldier).type == unit_type::kSoldier);
    VCR_CHECK(g.state().colonies[0].stock[cargo::kMuskets] == 10);
    VCR_CHECK(g.state().colonies[0].population == 3);
    // Back in as a carpenter: the muskets go back to the warehouse.
    setBuildingLevel(g.state().colonies[0], BuildingKind::Carpenter, 1);
    VCR_CHECK(g.moveColonist(0, {-1, soldier}, job::kCarpenter) == -1);
    VCR_CHECK(g.state().colonies[0].population == 4);
    VCR_CHECK(g.state().colonies[0].jobs[3] == job::kCarpenter);
    VCR_CHECK(g.state().colonies[0].stock[cargo::kMuskets] == 60);
}
