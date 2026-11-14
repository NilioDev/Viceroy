// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/game/colony.hpp"
#include "viceroy/game/game.hpp"

#include <algorithm>
#include <functional>

using namespace viceroy;

namespace {

// A colony of `population` with nobody at work: it makes no food and eats
// two for each colonist.
Game reportGame(int population, int year, int season, const std::function<void(SaveColony&)>& setup,
                bool cotton = false) {
    SaveGame s;
    s.year = year;
    s.season = season;
    s.humanNation = 3;
    s.map.width = 3;
    s.map.height = 3;
    s.map.squares.resize(9);
    SaveColony c;
    c.x = 1;
    c.y = 1;
    c.nation = 3;
    c.population = population;
    c.rebelDivisor = 200;
    c.name = "New Amsterdam";
    c.stock[cargo::kFood] = 500;
    setup(c);
    s.colonies.push_back(c);
    s.nations[3].crossesNeeded = 1000;
    Ruleset rules;
    rules.cargo.resize(16);
    rules.cargo[3].name = "Cotton";
    rules.cargo[6].name = "Ore";
    if (cotton) {
        // The colony's own square grows 3 cotton (and a little food).
        rules.terrain.resize(21);
        rules.terrain[0].yield[3] = 3;
    }
    return Game(std::move(s), rules);
}

std::vector<GameNotice> afterTurn(Game& g) {
    g.endTurn();
    return g.takeNotices();
}

const GameNotice* find(const std::vector<GameNotice>& notices, const std::string& section) {
    const auto it = std::find_if(notices.begin(), notices.end(), [&](const GameNotice& n) { return n.section == section; });
    return it == notices.end() ? nullptr : &*it;
}

} // namespace

VCR_TEST("colony reports: a new cargo when the store passes a hundred (measured)") {
    const auto cargo = [](int warehouse, int cotton) {
        Game g = reportGame(
            1, 1494, 0,
            [&](SaveColony& c) {
                c.raw[149] = static_cast<std::uint8_t>(warehouse);
                c.stock[3] = cotton;
            },
            true);
        return afterTurn(g);
    };
    // 98 + 3 passes 100 below the limit; reaching the limit names it, and
    // offers a larger warehouse while one can be built. The cotton thrown
    // away then is not reported again.
    auto n = cargo(1, 98);
    VCR_CHECK(find(n, "CARGOREADY0") != nullptr);
    VCR_CHECK(find(n, "CARGOREADY0")->strings == std::vector<std::string>{"New Amsterdam", "Cotton"});
    n = cargo(1, 198);
    VCR_CHECK(find(n, "CARGOREADY1") != nullptr);
    VCR_CHECK(find(n, "CARGOREADY1")->numbers == std::vector<int>{200});
    VCR_CHECK(find(n, "SPOIL1") == nullptr);
    n = cargo(2, 298);
    VCR_CHECK(find(n, "CARGOREADY2") != nullptr);
    n = cargo(1, 120);
    VCR_CHECK(n.empty() || (find(n, "CARGOREADY0") == nullptr && find(n, "SPOIL1") == nullptr));
}

VCR_TEST("colony reports: goods thrown away (measured)") {
    {
        Game g = reportGame(1, 1494, 0, [](SaveColony& c) { c.stock[6] = 150; });
        const auto n = afterTurn(g);
        // 150 down to 100 is no new cargo, only waste.
        VCR_CHECK(find(n, "CARGOREADY1") == nullptr);
        const auto* spoil = find(n, "SPOIL1");
        VCR_CHECK(spoil != nullptr);
        VCR_CHECK(spoil->strings == std::vector<std::string>{"New Amsterdam", "Ore"});
        VCR_CHECK(spoil->numbers == std::vector<int>{50});
    }
    {
        Game g = reportGame(1, 1494, 0, [](SaveColony& c) {
            c.stock[3] = 150;
            c.stock[6] = 150;
        });
        const auto n = afterTurn(g);
        VCR_CHECK(find(n, "SPOIL2") != nullptr);
        VCR_CHECK(find(n, "SPOIL1") == nullptr);
    }
    {
        Game g = reportGame(1, 1494, 0, [](SaveColony& c) {
            c.raw[149] = 2;
            c.stock[6] = 350;
        });
        VCR_CHECK(find(afterTurn(g), "SPOIL3") != nullptr);
    }
}

VCR_TEST("colony reports: food runs low, then out (measured)") {
    // Four colonists eat 8 a turn with nothing made: 31 left warns (fewer
    // than four turns' food), 33 does not.
    {
        Game g = reportGame(4, 1494, 0, [](SaveColony& c) { c.stock[cargo::kFood] = 39; });
        const auto n = afterTurn(g);
        const auto* low = find(n, "FOODLOW");
        VCR_CHECK(low != nullptr && low->numbers == std::vector<int>{31});
    }
    {
        Game g = reportGame(4, 1494, 0, [](SaveColony& c) { c.stock[cargo::kFood] = 41; });
        VCR_CHECK(find(afterTurn(g), "FOODLOW") == nullptr);
    }
    // Out of food: before 1600 and after an autumn turn plainly, and with
    // winter coming after a spring turn from 1600. Nobody starves in the
    // turn the food runs out.
    {
        Game g = reportGame(4, 1494, 0, [](SaveColony& c) { c.stock[cargo::kFood] = 3; });
        const auto n = afterTurn(g);
        VCR_CHECK(find(n, "FOOD1") != nullptr);
        // Read from the original: a sad tune at once with Event Music alone.
        VCR_CHECK(find(n, "FOOD1")->music == std::vector<MusicCue>{{MusicCue::Kind::MoodEvent, 1}});
        VCR_CHECK(g.state().colonies[0].population == 4);
        VCR_CHECK(g.state().colonies[0].stock[cargo::kFood] == 0);
    }
    {
        Game g = reportGame(4, 1650, 0, [](SaveColony& c) { c.stock[cargo::kFood] = 3; });
        VCR_CHECK(find(afterTurn(g), "FOOD2") != nullptr);
    }
    {
        Game g = reportGame(4, 1650, 1, [](SaveColony& c) { c.stock[cargo::kFood] = 3; });
        VCR_CHECK(find(afterTurn(g), "FOOD1") != nullptr);
    }
}

VCR_TEST("colony reports: a workshop out of its raw good (measured)") {
    // A fur trader with no furs reports it; with one fur he makes a coat
    // and nothing is said.
    const auto furTrader = [](int furs) {
        Game g = reportGame(1, 1494, 0, [&](SaveColony& c) {
            c.jobs[0] = 12; // fur trader
            c.squares.fill(-1);
            c.stock[cargo::kFurs] = furs;
        });
        return afterTurn(g);
    };
    const auto none = furTrader(0);
    VCR_CHECK(find(none, "FURS") != nullptr && find(none, "FURS")->strings == std::vector<std::string>{"New Amsterdam"});
    VCR_CHECK(find(furTrader(1), "FURS") == nullptr);
}

VCR_TEST("colony reports: the cargo tutorial hint, once a game and only with hints on (measured)") {
    const auto setup = [](SaveColony& c) {
        c.raw[149] = 1;
        c.stock[3] = 98;
    };
    {
        Game g = reportGame(1, 1494, 0, setup, true);
        auto n = afterTurn(g);
        VCR_CHECK(find(n, "TUTORIAL6") == nullptr);
    }
    Game g = reportGame(1, 1494, 0, setup, true);
    g.state().header[18] |= 0x80;
    VCR_CHECK(g.tutorialHints());
    auto n = afterTurn(g);
    const auto* hint = find(n, "TUTORIAL6");
    VCR_CHECK(hint != nullptr);
    VCR_CHECK(hint->strings == std::vector<std::string>{"Cotton", "New Amsterdam"});
    VCR_CHECK(hint->numbers == std::vector<int>{100});
    VCR_CHECK(g.tutorialShown(Game::kTutorialCargo));
    VCR_CHECK(g.state().header[23] == 0x02);
    // The cotton is sold off and grows past a hundred again: no second hint.
    g.state().colonies[0].stock[3] = 98;
    n = afterTurn(g);
    VCR_CHECK(find(n, "CARGOREADY0") != nullptr);
    VCR_CHECK(find(n, "TUTORIAL6") == nullptr);
}

namespace {

int countSection(const std::vector<GameNotice>& notices, const std::string& section) {
    return static_cast<int>(
        std::count_if(notices.begin(), notices.end(), [&](const GameNotice& n) { return n.section == section; }));
}

} // namespace

VCR_TEST("colony reports: a colonist starves when the store was empty already (seen in the original)") {
    Game g = reportGame(3, 1494, 0, [](SaveColony& c) {
        c.stock[cargo::kFood] = 0;
        c.squares = {0, 1, 2, -1, -1, -1, -1, -1};
    });
    g.state().difficulty = 2;
    auto n = afterTurn(g);
    VCR_CHECK(find(n, "STARVE1") != nullptr);
    VCR_CHECK(find(n, "FOOD1") == nullptr);
    const auto& c = g.state().colonies[0];
    VCR_CHECK(c.population == 2);
    // One of the three squares is free now, the other two are worked by
    // colonists 0 and 1.
    VCR_CHECK(std::count(c.squares.begin(), c.squares.end(), -1) == 6);
    VCR_CHECK(std::count(c.squares.begin(), c.squares.end(), 0) == 1);
    VCR_CHECK(std::count(c.squares.begin(), c.squares.end(), 1) == 1);
}

VCR_TEST("colony reports: food running out this turn only warns (seen in the original)") {
    Game g = reportGame(3, 1494, 0, [](SaveColony& c) { c.stock[cargo::kFood] = 3; });
    g.state().difficulty = 4;
    auto n = afterTurn(g);
    VCR_CHECK(find(n, "FOOD1") != nullptr);
    VCR_CHECK(g.state().colonies[0].population == 3);
    // The next turn begins with an empty store.
    n = afterTurn(g);
    VCR_CHECK(find(n, "STARVE1") != nullptr);
    VCR_CHECK(find(n, "STARVE1")->music == std::vector<MusicCue>{{MusicCue::Kind::ChangeMood, 1}});
    VCR_CHECK(g.state().colonies[0].population == 2);
}

VCR_TEST("colony reports: nobody starves on Discoverer before 1520, some do after (measured)") {
    Game g = reportGame(8, 1494, 0, [](SaveColony& c) { c.stock[cargo::kFood] = 0; });
    for (int turn = 0; turn < 20; ++turn) {
        const auto n = afterTurn(g);
        VCR_CHECK(countSection(n, "STARVE1") == 0);
    }
    VCR_CHECK(g.state().colonies[0].population == 8);

    Game late = reportGame(30, 1601, 0, [](SaveColony& c) { c.stock[cargo::kFood] = 0; });
    int starved = 0;
    int spared = 0;
    for (int turn = 0; turn < 24; ++turn) {
        const auto n = afterTurn(late);
        starved += countSection(n, "STARVE1") + countSection(n, "STARVE2");
        spared += countSection(n, "FOOD1") + countSection(n, "FOOD2");
    }
    VCR_CHECK(starved > 0 && spared > 0);
    VCR_CHECK(late.state().colonies[0].population == 30 - starved);
}

VCR_TEST("colony reports: the last colonist starving ends the colony (seen in the original)") {
    Game g = reportGame(1, 1494, 0, [](SaveColony& c) { c.stock[cargo::kFood] = 0; });
    g.state().difficulty = 2;
    g.state().map.at(1, 1).flags |= MapSquare::kHasColony;
    const auto n = afterTurn(g);
    VCR_CHECK(find(n, "VANISH") != nullptr);
    VCR_CHECK(find(n, "VANISH")->strings == std::vector<std::string>{"New Amsterdam"});
    VCR_CHECK(find(n, "VANISH")->music == std::vector<MusicCue>{{MusicCue::Kind::ChangeMood, 1}});
    VCR_CHECK(g.state().colonies.empty());
    VCR_CHECK((g.state().map.at(1, 1).flags & MapSquare::kHasColony) == 0);
}

VCR_TEST("colony reports: another nation's colony starves only when 3 or more short (seen in the original)") {
    Game g = reportGame(1, 1494, 0, [](SaveColony& c) {
        c.stock[cargo::kFood] = 0;
        c.nation = 1;
    });
    g.state().difficulty = 2;
    afterTurn(g);
    VCR_CHECK(g.state().colonies.size() == 1);
    Game two = reportGame(2, 1494, 0, [](SaveColony& c) {
        c.stock[cargo::kFood] = 0;
        c.nation = 1;
    });
    two.state().difficulty = 2;
    const auto n = afterTurn(two);
    VCR_CHECK(two.state().colonies[0].population == 1);
    VCR_CHECK(n.empty() || find(n, "STARVE1") == nullptr);
}
