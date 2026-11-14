// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/game/colony.hpp"
#include "viceroy/game/game.hpp"

using namespace viceroy;

namespace {

constexpr int kFreeColonist = 0x1C;
constexpr int kHardyPioneer = 0x14;

// The improvement values of NAMES.TXT for the rows used here.
Ruleset pioneerRules() {
    Ruleset r;
    r.terrain.resize(21);
    for (auto& t : r.terrain) {
        t.movementCost = 1;
        t.improvementTurns = 4;
    }
    r.terrain[2].improvementTurns = 3;  // plains
    r.terrain[6].improvementTurns = 5;  // marsh
    r.terrain[7].improvementTurns = 7;  // swamp
    r.terrain[10].improvementTurns = 4; // mixed forest
    r.terrain[13].improvementTurns = 6; // tropical forest
    r.terrain[19].improvementTurns = 7; // mountains
    r.units.resize(14);
    r.units[unit_type::kColonist] = {"Colonists", 101, 1, 0, 1, 0, 1, 1, 0, 0, 0, 0};
    r.units[unit_type::kPioneer] = {"Pioneers", 102, 1, 0, 1, 0, 1, 2, 0, 0, 0, 0};
    return r;
}

// One pioneer on a square of the given terrain.
Game pioneerOn(int terrain, int profession, int relief = 0, int tools = 100) {
    SaveGame s;
    s.year = 1492;
    s.humanNation = 3;
    s.map.width = 3;
    s.map.height = 3;
    s.map.squares.resize(9);
    for (auto& sq : s.map.squares) {
        sq.terrain = 2;
    }
    s.map.at(1, 1).terrain = static_cast<std::uint8_t>(terrain);
    s.map.at(1, 1).relief = static_cast<std::uint8_t>(relief);
    SaveUnit u;
    u.x = 1;
    u.y = 1;
    u.type = unit_type::kPioneer;
    u.nation = 3;
    u.profession = profession;
    u.tools = tools;
    s.units.push_back(u);
    s.activeUnit = 0;
    return Game(std::move(s), pioneerRules());
}

// Turns ended until the job is done: 0 when it is done at once.
int turnsToFinish(Game& g, int orders) {
    if (g.startWork(0, orders) == WorkResult::Finished) {
        return 0;
    }
    for (int turn = 1; turn <= 20; ++turn) {
        g.endTurn();
        if (g.unit(0).orders == SaveUnit::kNoOrders) {
            return turn;
        }
    }
    return -1;
}

} // namespace

VCR_TEST("pioneers: a pioneer takes one turn more than the terrain's effort") {
    // Measured in the original: plowing plains 4 turns, clearing mixed
    // forest 5, plowing marsh 6; roads on plains 2, mixed forest 3, swamp 6,
    // mountains 6.
    struct Case {
        int terrain;
        int relief;
        int orders;
        int turns;
    };
    const Case cases[] = {
        {2, 0, SaveUnit::kPlow, 4}, {10, 0, SaveUnit::kPlow, 5}, {6, 0, SaveUnit::kPlow, 6},
        {2, 0, SaveUnit::kRoad, 2}, {10, 0, SaveUnit::kRoad, 3}, {7, 0, SaveUnit::kRoad, 6},
        {2, 5, SaveUnit::kRoad, 6},
    };
    for (const auto& c : cases) {
        Game g = pioneerOn(c.terrain, kFreeColonist, c.relief);
        VCR_CHECK(turnsToFinish(g, c.orders) == c.turns);
    }
}

VCR_TEST("pioneers: a hardy pioneer takes half the effort, at once when that is nothing") {
    // Measured: plowing plains 1, clearing mixed forest 2, plowing swamp 3,
    // clearing tropical forest 3, a road on swamp 2, on plains at once.
    struct Case {
        int terrain;
        int orders;
        int turns;
    };
    const Case cases[] = {
        {2, SaveUnit::kPlow, 1}, {10, SaveUnit::kPlow, 2}, {7, SaveUnit::kPlow, 3},
        {13, SaveUnit::kPlow, 3}, {7, SaveUnit::kRoad, 2}, {2, SaveUnit::kRoad, 0},
    };
    for (const auto& c : cases) {
        Game g = pioneerOn(c.terrain, kHardyPioneer);
        VCR_CHECK(turnsToFinish(g, c.orders) == c.turns);
    }
}

VCR_TEST("pioneers: the work changes the land and uses 20 tools") {
    Game g = pioneerOn(10, kHardyPioneer);
    VCR_CHECK(g.startWork(0, SaveUnit::kPlow) == WorkResult::Started);
    VCR_CHECK(g.unit(0).workTurns == 1);
    VCR_CHECK(g.movesLeft(0) == 0);
    g.endTurn();
    g.endTurn();
    // The forest is gone, the field is not plowed yet.
    VCR_CHECK(g.state().map.at(1, 1).terrain == 2);
    VCR_CHECK((g.state().map.at(1, 1).flags & MapSquare::kPlowed) == 0);
    VCR_CHECK(g.unit(0).tools == 80);
    VCR_CHECK(g.unit(0).workTurns == 0);
    // The turn the work ends is used up too.
    VCR_CHECK(g.movesLeft(0) == 0);
    VCR_CHECK(g.activeUnit() == -1);

    g.endTurn();
    VCR_CHECK(g.activeUnit() == 0);
    VCR_CHECK(g.startWork(0, SaveUnit::kPlow) == WorkResult::Started);
    g.endTurn();
    VCR_CHECK((g.state().map.at(1, 1).flags & MapSquare::kPlowed) != 0);
    VCR_CHECK(g.unit(0).tools == 60);
}

VCR_TEST("pioneers: what can't be done") {
    Game g = pioneerOn(2, kFreeColonist);
    g.state().map.at(1, 1).flags = MapSquare::kPlowed | MapSquare::kRoad;
    VCR_CHECK(g.startWork(0, SaveUnit::kPlow) == WorkResult::AlreadyPlowed);
    VCR_CHECK(g.startWork(0, SaveUnit::kRoad) == WorkResult::AlreadyRoad);
    VCR_CHECK(g.unit(0).orders == SaveUnit::kNoOrders);

    Game hills = pioneerOn(2, kFreeColonist, 1);
    VCR_CHECK(!hills.canPlow(1, 1));
    VCR_CHECK(hills.startWork(0, SaveUnit::kPlow) == WorkResult::NotHere);
    VCR_CHECK(hills.startWork(0, SaveUnit::kRoad) == WorkResult::Started);

    Game noTools = pioneerOn(2, kFreeColonist, 0, 0);
    VCR_CHECK(noTools.startWork(0, SaveUnit::kRoad) == WorkResult::NotAPioneer);
}

VCR_TEST("pioneers: out of tools, a pioneer is a colonist again") {
    Game g = pioneerOn(2, kHardyPioneer, 0, 20);
    VCR_CHECK(g.startWork(0, SaveUnit::kRoad) == WorkResult::Finished);
    VCR_CHECK(g.unit(0).type == unit_type::kColonist);
    VCR_CHECK(g.unit(0).tools == 0);
    const auto notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices.front().section == "USEDUPTOOLS");
    VCR_CHECK(g.takeNotices().empty());
}

VCR_TEST("pioneers: a forest cleared near a colony gives it lumber (read from the original)") {
    const auto clear = [](int profession, bool mill, int stock, int colonyX) {
        Game g = pioneerOn(10, profession);
        SaveColony c;
        c.x = colonyX;
        c.y = 1;
        c.nation = 3;
        c.name = "Fort Oranje";
        c.population = 1;
        c.stock[cargo::kLumber] = stock;
        if (mill) {
            setBuildingLevel(c, BuildingKind::Carpenter, 2);
        }
        g.state().colonies.push_back(c);
        g.state().map.at(colonyX, 1).flags |= MapSquare::kHasColony;
        turnsToFinish(g, SaveUnit::kPlow);
        return std::pair{g.state().colonies[0].stock[cargo::kLumber], g.takeNotices()};
    };
    auto [lumber, notices] = clear(kFreeColonist, false, 0, 0);
    VCR_CHECK(lumber == 20);
    bool told = false;
    for (const auto& n : notices) {
        told = told || (n.section == "CLEARCUT" && n.numbers.size() == 1 && n.numbers[0] == 20);
    }
    VCR_CHECK(told);
    VCR_CHECK(clear(kHardyPioneer, false, 0, 0).first == 40);
    // With a lumber mill: the forest's lumber yield (0 in these rules)
    // plus one, 20 each.
    VCR_CHECK(clear(kFreeColonist, true, 0, 0).first == 20);
    // No more than the warehouse holds.
    VCR_CHECK(clear(kFreeColonist, false, 90, 0).first == 100);
}
