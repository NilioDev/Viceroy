// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/game/colony.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/render/colony_view.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>

using namespace viceroy;

VCR_TEST("colony buildings: levels fill their bits from the bottom") {
    SaveColony c;
    setBuildingLevel(c, BuildingKind::Docks, 2);
    VCR_CHECK(((c.buildings >> 6) & 7u) == 3u);
    VCR_CHECK(buildingLevel(c, BuildingKind::Docks) == 2);
    setBuildingLevel(c, BuildingKind::Carpenter, 1);
    VCR_CHECK(buildingLevel(c, BuildingKind::Carpenter) == 1);
    VCR_CHECK(buildingLevel(c, BuildingKind::Docks) == 2);
    setBuildingLevel(c, BuildingKind::Docks, 0);
    VCR_CHECK(buildingLevel(c, BuildingKind::Docks) == 0);
}

VCR_TEST("colony: sons of liberty share") {
    SaveColony c;
    c.rebelDividend = 107;
    c.rebelDivisor = 292;
    VCR_CHECK(sonsOfLibertyPercent(c) == 36);
}

namespace {

void dumpScreen(const Screen& screen, const Art& art, const char* path) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(screen.pixels().data()),
              static_cast<std::streamsize>(screen.pixels().size()));
    for (const auto& c : art.palette) {
        out.put(static_cast<char>(c.r)).put(static_cast<char>(c.g)).put(static_cast<char>(c.b));
    }
}

} // namespace

VCR_TEST("original files: a colony's production and screen") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    const ColonyArt colonyArt = loadColonyArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    Game game(readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {})), art.rules);

    // Isabella, as the original shows it: two distillers; the town commons
    // (savannah) grow 5 food and 4 sugar.
    const auto& isabella = game.state().colonies[1];
    VCR_CHECK(isabella.name == "Isabella");
    const auto p = colonyProduction(isabella, game.state().map, game.rules());
    VCR_CHECK(p.centreFood == 5);
    VCR_CHECK(p.centreGood == cargo::kSugar && p.centreAmount == 4);
    VCR_CHECK(p.produced[cargo::kRum] == 4); // a criminal makes 1, a colonist 3
    VCR_CHECK(p.produced[cargo::kBells] == 1 && p.produced[cargo::kCrosses] == 1);

    // Jamestown: farmers, a sugar planter and lumberjacks around it.
    const auto& jamestown = game.state().colonies[0];
    const auto q = colonyProduction(jamestown, game.state().map, game.rules());
    VCR_CHECK(q.centreFood == 4 && q.centreGood == cargo::kFurs && q.centreAmount == 3);
    VCR_CHECK(q.colonists[0].amount == 7); // an expert farmer on plowed savannah
    VCR_CHECK(q.colonists[1].amount == 5); // a colonist there
    VCR_CHECK(q.colonists[2].amount == 4); // sugar
    VCR_CHECK(q.colonists[3].amount == 4); // lumber in a forest
    VCR_CHECK(q.colonists[4].amount == 6); // lumber with a road
    VCR_CHECK(q.colonists[7].amount == 6); // an expert fur trader
    VCR_CHECK(q.colonists[8].amount == 12); // an expert carpenter in a lumber mill
    VCR_CHECK(q.produced[cargo::kBells] == 4);

    Screen screen;
    const int which = std::getenv("VICEROY_COLONY") ? std::atoi(std::getenv("VICEROY_COLONY")) : 1;
    // VICEROY_JOBS=line opens the jobs menu on that line, for comparing.
    const int jobsLine = std::getenv("VICEROY_JOBS") ? std::atoi(std::getenv("VICEROY_JOBS")) : -1;
    drawColonyScreen(screen, art, colonyArt, game, {which, 0, jobsLine});
    VCR_CHECK(jobsLine >= 0 || screen.at(200, 50) == 0);
    if (const char* dump = std::getenv("VICEROY_DUMP_COLONY")) {
        dumpScreen(screen, art, dump);
    }
}

#include "viceroy/ui/play.hpp"

VCR_TEST("original files: the colony screen opens from the map and closes") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    const ColonyArt colonyArt = loadColonyArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    Game game(readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {})), art.rules);
    game.setActiveUnit(51); // the frigate next to Isabella
    PlayScreen play(std::move(game), dir / "unused.sav");
    play.setColonyArt(&colonyArt);

    // As in the original: V for the cursor, one square west, Enter.
    VCR_CHECK(play.handle(Command::ViewMode));
    play.handle(Command::MoveW);
    play.handle(Command::Confirm);
    VCR_CHECK(play.colonyScreen().has_value());
    VCR_CHECK(play.colonyScreen()->colony == 1);
    Screen screen;
    play.draw(screen, art);
    VCR_CHECK(screen.at(200, 50) == 0); // the colony screen, not the map

    VCR_CHECK(play.handle(Command::Cancel)); // closes the colony
    VCR_CHECK(!play.colonyScreen().has_value());
    VCR_CHECK(play.handle(Command::Cancel)); // leaves the view mode
    VCR_CHECK(!play.viewMode());
    VCR_CHECK(!play.handle(Command::Cancel)); // nothing left: back to the menu

    // A click on one's own colony opens it too.
    play.clickSquare(43, 29);
    VCR_CHECK(play.colonyScreen().has_value());
}

#include "viceroy/ui/debug_menu.hpp"

VCR_TEST("original files: the debug menu changes the game only when asked") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    Game game(readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {})), art.rules);
    PlayScreen play(std::move(game), dir / "unused.sav");
    DebugMenu debug;
    VCR_CHECK(!debug.open());
    const int gold = play.game().state().nations[3].gold;
    debug.toggle(play);
    VCR_CHECK(debug.open());
    debug.handle(Command::Down, play); // Gold + 1000
    VCR_CHECK(!debug.handle(Command::Confirm, play).empty());
    VCR_CHECK(!debug.open());
    VCR_CHECK(play.game().state().nations[3].gold == gold + 1000);

    debug.apply(0, play); // reveal the map
    VCR_CHECK(play.game().state().map.at(0, 0).seenBy(3));

    play.openColony(1);
    const int people = play.game().state().colonies[1].population;
    debug.apply(1, play); // a colonist joins
    VCR_CHECK(play.game().state().colonies[1].population == people + 1);
    debug.apply(0, play); // goods
    VCR_CHECK(play.game().state().colonies[1].stock[0] == 113);
}

VCR_TEST("original files: colonists change jobs through the jobs menu") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    Game game(readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {})), art.rules);
    auto& save = game.state();
    auto& isabella = save.colonies[1];
    // Isabella's colonists both distil rum; the squares around are free.
    const auto choices = jobChoices(isabella, save.map, game.rules(), save.colonies, 1);
    // As the original lists them: the land, then the buildings Isabella has
    // (no armory, no church), 16 lines before "(More)".
    VCR_CHECK(choices.size() == 16);
    VCR_CHECK(choices.back().job == job::kStatesman);
    VCR_CHECK(choices[0].amount > 0);  // farming the forest to the west
    VCR_CHECK(choices[8].amount > 0);  // fishing: Isabella has docks
    VCR_CHECK(assignJob(isabella, save.map, game.rules(), save.colonies, 1, 0));
    VCR_CHECK(isabella.jobs[1] == 0);
    VCR_CHECK(std::count(isabella.squares.begin(), isabella.squares.end(), 1) == 1);
    // Back indoors: the square is freed.
    VCR_CHECK(assignJob(isabella, save.map, game.rules(), save.colonies, 1, 9));
    VCR_CHECK(std::count(isabella.squares.begin(), isabella.squares.end(), 1) == 0);
    // No church in Isabella: no preaching.
    VCR_CHECK(!assignJob(isabella, save.map, game.rules(), save.colonies, 1, 16));
}

VCR_TEST("original files: a colony stores what it makes at the end of the turn") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    Game game(readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {})), art.rules);
    auto& isabella = game.state().colonies[1];
    VCR_CHECK(isabella.stock[cargo::kFood] == 13 && isabella.stock[cargo::kSugar] == 16 &&
              isabella.stock[cargo::kRum] == 36);
    runColonyTurn(isabella, game.state().map, game.rules());
    // 5 food grown, 4 eaten; 4 sugar grown and distilled into 4 rum.
    VCR_CHECK(isabella.stock[cargo::kFood] == 14);
    VCR_CHECK(isabella.stock[cargo::kSugar] == 16);
    VCR_CHECK(isabella.stock[cargo::kRum] == 40);

    // Plenty of food makes a new colonist.
    isabella.stock[cargo::kFood] = 199;
    const int units = game.unitCount();
    game.endTurn();
    VCR_CHECK(game.unitCount() == units + 1);
    VCR_CHECK(game.unit(units).x == isabella.x && game.unit(units).nation == 3);
    VCR_CHECK(!game.lastTurnNews().empty());
}

namespace {

int bred(int horses, int surplus, bool stables, int warehouse = 0) {
    SaveColony c;
    c.stock[cargo::kHorses] = horses;
    c.raw[149] = static_cast<std::uint8_t>(warehouse);
    if (stables) {
        setBuildingLevel(c, BuildingKind::Stables, 1);
    }
    return horsesBred(c, surplus);
}

} // namespace

VCR_TEST("horses: breeding from the food surplus (measured)") {
    // Two for every started herd of 50, or 25 with stables.
    VCR_CHECK(bred(1, 29, false) == 0);
    VCR_CHECK(bred(2, 29, false) == 2);
    VCR_CHECK(bred(50, 29, false) == 2);
    VCR_CHECK(bred(51, 29, false) == 4);
    VCR_CHECK(bred(25, 29, true) == 2);
    VCR_CHECK(bred(26, 29, true) == 4);
    VCR_CHECK(bred(75, 29, true, 2) == 6);
    VCR_CHECK(bred(76, 29, true, 2) == 8);
    VCR_CHECK(bred(150, 29, false, 2) == 6);
    VCR_CHECK(bred(151, 29, false, 2) == 8);
    // At most half the surplus, rounded up; nothing without a surplus.
    VCR_CHECK(bred(87, 5, true) == 3);
    VCR_CHECK(bred(120, 4, false, 2) == 2);
    VCR_CHECK(bred(50, 0, false) == 0);
    VCR_CHECK(bred(50, -1, false) == 0);
    // The warehouse is the limit.
    VCR_CHECK(bred(99, 29, false) == 1);
    VCR_CHECK(bred(97, 29, false) == 3);
    VCR_CHECK(bred(100, 29, false) == 0);
}

namespace {

int centre(int terrain, int relief, bool plowed, int difficulty) {
    Ruleset r;
    r.terrain.resize(21);
    r.terrain[0].yield[job::kFarmer] = 2;  // tundra
    r.terrain[1].yield[job::kFarmer] = 1;  // desert
    r.terrain[2].yield[job::kFarmer] = 4;  // plains
    r.terrain[4].yield[job::kFarmer] = 2;  // grassland
    r.terrain[5].yield[job::kFarmer] = 3;  // savannah
    r.terrain[8].yield[job::kFarmer] = 1;  // boreal
    r.terrain[9].yield[job::kFarmer] = 1;  // scrub
    r.terrain[13].yield[job::kFarmer] = 2; // tropical
    MapSquare sq;
    sq.terrain = static_cast<std::uint8_t>(terrain);
    sq.relief = static_cast<std::uint8_t>(relief);
    sq.flags = plowed ? MapSquare::kPlowed : 0;
    return centreFoodYield(r, sq, difficulty);
}

} // namespace

VCR_TEST("colony square: food by land and difficulty (measured)") {
    // Conquistador.
    VCR_CHECK(centre(0, 0, false, 2) == 3);
    VCR_CHECK(centre(1, 0, false, 2) == 1);
    VCR_CHECK(centre(2, 0, false, 2) == 3);
    VCR_CHECK(centre(2, 0, true, 2) == 4);
    VCR_CHECK(centre(2, 2, false, 2) == 3);
    VCR_CHECK(centre(4, 0, false, 2) == 3);
    VCR_CHECK(centre(4, 0, true, 2) == 4);
    VCR_CHECK(centre(8, 0, false, 2) == 2);
    VCR_CHECK(centre(9, 0, false, 2) == 1);
    VCR_CHECK(centre(13, 0, false, 2) == 2);
    // Discoverer 2 more, Explorer 1, the harder levels as Conquistador.
    VCR_CHECK(centre(2, 0, false, 0) == 5);
    VCR_CHECK(centre(2, 0, false, 1) == 4);
    VCR_CHECK(centre(2, 0, false, 4) == 3);
    VCR_CHECK(centre(5, 0, false, 0) == 5);
    VCR_CHECK(centre(8, 0, false, 0) == 4);
    VCR_CHECK(centre(8, 0, false, 4) == 2);
    VCR_CHECK(centre(13, 0, false, 0) == 4);
}

VCR_TEST("silver: only from a deposit or a marked mountain (measured)") {
    Ruleset r;
    r.terrain.resize(21);
    r.terrain[19].yield[7] = 1; // mountains
    MapSquare m;
    m.terrain = 2;
    m.relief = 5;
    const int colonist = 28;
    const int expert = 7;
    VCR_CHECK(squareYield(r, m, 7, colonist) == 0);
    VCR_CHECK(squareYield(r, m, 7, colonist, {}, true, Resource::Silver) == 3);
    VCR_CHECK(squareYield(r, m, 7, expert, {}, true, Resource::Silver) == 6);
    m.flags = 0x04;
    VCR_CHECK(squareYield(r, m, 7, colonist) == 1);
    VCR_CHECK(squareYield(r, m, 7, colonist, {}, true, Resource::DepletedSilver) == 1);
    VCR_CHECK(squareYield(r, m, 7, expert, {}, true, Resource::DepletedSilver) == 2);
}

VCR_TEST("workshops: house, shop and factory (measured)") {
    const int colonist = 28;
    const int furTrader = 12;
    const int blacksmith = 14;
    VCR_CHECK(buildingYield(1, colonist, furTrader) == 3);
    VCR_CHECK(buildingYield(2, colonist, furTrader) == 6);
    VCR_CHECK(buildingYield(2, furTrader, furTrader) == 12);
    VCR_CHECK(buildingYield(3, colonist, furTrader) == 9);
    VCR_CHECK(buildingYield(3, colonist, furTrader, {}, false) == 6);
    // Bonus and penalty go in before an expert doubles.
    VCR_CHECK(buildingYield(1, blacksmith, blacksmith, {2, 0}) == 10);
    VCR_CHECK(buildingYield(1, blacksmith, blacksmith, {0, 1}) == 4);
    VCR_CHECK(buildingYield(1, colonist, blacksmith, {0, 1}) == 2);
    VCR_CHECK(buildingYield(2, colonist, blacksmith, {1, 0}) == 7);
}

VCR_TEST("construction: finishes with the hammers and tools the ruleset lists") {
    Ruleset r;
    r.buildings.resize(3);
    r.buildings[2].hammers = 320;
    r.buildings[2].tools = 200; // NAMES.TXT says 20: tens of tools
    SaveColony c;
    c.construction = 2;
    c.hammers = 319;
    c.stock[cargo::kTools] = 200;
    c.raw[149] = 1;
    setBuildingLevel(c, BuildingKind::Fortification, 2);
    SaveMap map;
    // One hammer short: nothing yet (the colony's own hammers come from
    // nobody here).
    auto turn = runColonyTurn(c, map, r);
    VCR_CHECK(turn.built == -1);
    c.hammers = 320;
    turn = runColonyTurn(c, map, r);
    VCR_CHECK(turn.built == 2);
    VCR_CHECK(c.hammers == 0);
    VCR_CHECK(c.stock[cargo::kTools] == 0);
    VCR_CHECK(buildingLevel(c, BuildingKind::Fortification) == 3);
}

VCR_TEST("construction: colonies build wagons, artillery and ships (measured)") {
    Ruleset r;
    r.units.resize(19);
    r.units[11].cost = 6; // artillery: 192 hammers, 40 tools
    r.units[11].tools = 4;
    r.units[12].cost = 1; // wagon train: 40 hammers
    r.units[13].cost = 4; // caravel: 128 hammers, 40 tools
    r.units[13].tools = 4;
    r.units[17].cost = 16; // frigate: offered, but never built in a colony
    VCR_CHECK(constructionUnit(42) == 11);
    VCR_CHECK(constructionUnit(47) == 16);
    VCR_CHECK(constructionUnit(48) == 17);
    VCR_CHECK(constructionUnit(49) == -1);
    VCR_CHECK(constructionHammers(r, 48) == 512);
    VCR_CHECK(constructionHammers(r, 42) == 192);
    VCR_CHECK(constructionHammers(r, 43) == 40);
    VCR_CHECK(constructionHammers(r, 44) == 128);
    VCR_CHECK(constructionTools(r, 44) == 40);

    SaveMap map;
    SaveColony c;
    c.construction = 43;
    c.hammers = 39;
    VCR_CHECK(runColonyTurn(c, map, r).builtUnit == -1);
    c.hammers = 600; // what is left over is lost
    auto turn = runColonyTurn(c, map, r);
    VCR_CHECK(turn.builtUnit == 12);
    VCR_CHECK(c.hammers == 0);
    VCR_CHECK(c.construction == 43); // it goes on building wagons

    c.construction = 44;
    c.hammers = 128;
    c.stock[cargo::kTools] = 39;
    turn = runColonyTurn(c, map, r);
    VCR_CHECK(turn.builtUnit == -1);
    VCR_CHECK(turn.toolsNeeded == 40);
    c.stock[cargo::kTools] = 40;
    VCR_CHECK(runColonyTurn(c, map, r).builtUnit == 13);
    VCR_CHECK(c.stock[cargo::kTools] == 0);

    c.construction = 48;
    c.hammers = 999;
    VCR_CHECK(runColonyTurn(c, map, r).builtUnit == -1);
    VCR_CHECK(c.hammers == 999);
}

VCR_TEST("build list: the next level of each building, then units (measured)") {
    Ruleset r;
    r.buildings.resize(42);
    for (auto& b : r.buildings) {
        b.hammers = 52;
    }
    r.buildings[3].minColonySize = 1; // armory
    r.buildings[13].minColonySize = 8; // college: too small a colony
    r.units.resize(19);
    SaveMap map;
    map.width = 3;
    map.height = 3;
    map.squares.resize(9);
    SaveColony c;
    c.x = 1;
    c.y = 1;
    c.population = 4;
    c.construction = 0;
    c.raw[149] = 0; // no warehouse yet
    setBuildingLevel(c, BuildingKind::Fortification, 1);
    setBuildingLevel(c, BuildingKind::Schoolhouse, 1);

    auto has = [](const std::vector<BuildOption>& list, int construction) {
        return std::any_of(list.begin(), list.end(), [&](const BuildOption& o) { return o.construction == construction; });
    };
    auto options = buildOptions(c, map, r);
    // The stockade being built is already there: shown, but complete.
    VCR_CHECK(!options.empty() && options.front().construction == 0 && options.front().complete);
    VCR_CHECK(has(options, 1));   // fort, the next level
    VCR_CHECK(!has(options, 2));  // not the fortress yet
    VCR_CHECK(has(options, 3));   // armory
    VCR_CHECK(!has(options, 6));  // docks only by the water
    VCR_CHECK(!has(options, 13)); // college needs 8 colonists
    VCR_CHECK(has(options, 15));  // warehouse
    VCR_CHECK(!has(options, 16)); // not its expansion yet
    VCR_CHECK(!has(options, 10) && !has(options, 30)); // never the town hall's upper levels or the capitol
    VCR_CHECK(has(options, 43));  // a wagon train anywhere
    VCR_CHECK(!has(options, 42)); // artillery needs an armory
    VCR_CHECK(!has(options, 44)); // ships need a shipyard

    map.at(0, 0).terrain = 25; // the ocean next door
    setBuildingLevel(c, BuildingKind::Armory, 1);
    setBuildingLevel(c, BuildingKind::Docks, 3);
    c.raw[149] = 1;
    options = buildOptions(c, map, r);
    VCR_CHECK(has(options, 42) && has(options, 4));
    VCR_CHECK(has(options, 16) && !has(options, 15));
    for (int ship = 44; ship <= kFrigateConstruction; ++ship) {
        VCR_CHECK(has(options, ship));
    }
}

VCR_TEST("original files: choosing what a colony builds") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    const ColonyArt colonyArt = loadColonyArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    Game game(readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {})), art.rules);
    PlayScreen play(std::move(game), dir / "unused.sav");
    play.setColonyArt(&colonyArt);
    play.setArt(&art);
    play.openColony(1);
    const auto& colony = play.game().state().colonies[1];
    const auto options = buildOptions(colony, play.game().state().map, play.game().rules());
    VCR_CHECK(options.size() >= 2);

    // B opens the list on the Make panel, on what is being built now.
    play.handle(Command::BuildColony);
    VCR_CHECK(play.colonyScreen()->panel == ColonyPanel::Make);
    VCR_CHECK(play.colonyScreen()->buildList >= 0);
    Screen screen;
    play.draw(screen, art);

    // The line above the first item is "(No Production)".
    while (play.colonyScreen()->buildList != 0) {
        play.handle(Command::Down);
    }
    play.handle(Command::Down);
    play.handle(Command::Confirm);
    VCR_CHECK(play.colonyScreen()->buildList == -1);
    VCR_CHECK(colony.construction == options.front().construction || options.front().complete);

    play.handle(Command::BuildColony);
    play.handle(Command::Up);
    if (play.colonyScreen()->buildList != 0) {
        while (play.colonyScreen()->buildList != 0) {
            play.handle(Command::Up);
        }
    }
    play.handle(Command::Confirm);
    VCR_CHECK(colony.construction == -1);

    // Clicks: the house button goes back to production, the musket to the
    // units present, the hammer to Make, CHANGE opens the list and a click
    // on a line picks it.
    VCR_CHECK(play.clickColonyScreen(kPanelButtons[0].x + 2, kPanelButtons[0].y + 2));
    VCR_CHECK(play.colonyScreen()->panel == ColonyPanel::Production);
    VCR_CHECK(play.clickColonyScreen(kPanelButtons[1].x + 2, kPanelButtons[1].y + 2));
    VCR_CHECK(play.colonyScreen()->panel == ColonyPanel::Units);
    VCR_CHECK(play.clickColonyScreen(kPanelButtons[2].x + 2, kPanelButtons[2].y + 2));
    VCR_CHECK(play.colonyScreen()->panel == ColonyPanel::Make);
    VCR_CHECK(play.clickColonyScreen(kChangeButton.x + 2, kChangeButton.y + 2));
    VCR_CHECK(play.colonyScreen()->buildList == 0);
    const auto box = buildListBox(static_cast<int>(options.size()));
    int pick = -1;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (!options[i].complete) {
            pick = static_cast<int>(i);
            break;
        }
    }
    VCR_CHECK(pick >= 0);
    VCR_CHECK(play.clickColonyScreen(box.x + 20, box.firstLine + 8 * (pick + 1) + 2));
    VCR_CHECK(colony.construction == options[static_cast<std::size_t>(pick)].construction);
    VCR_CHECK(play.colonyScreen()->buildList == -1);
}

VCR_TEST("buy: what finishing a construction costs (measured)") {
    Ruleset r;
    r.buildings.resize(42);
    r.buildings[1].hammers = 120; // fort
    r.buildings[1].tools = 100;
    r.units.resize(19);
    r.units[12].cost = 1; // wagon train: 40 hammers
    SaveNation nation;
    nation.prices[cargo::kTools] = 2; // Europe asks 2 for tools
    SaveColony c;
    c.construction = 43;
    c.hammers = 39;
    VCR_CHECK(constructionPrice(c, r, nation) == 13);
    c.hammers = 0; // twice as much before the first hammer
    VCR_CHECK(constructionPrice(c, r, nation) == 1040);
    c.construction = 1;
    c.stock[cargo::kTools] = 9;
    c.hammers = 119;
    VCR_CHECK(constructionPrice(c, r, nation) == 13 + 6 * 91);
    c.hammers = 0;
    VCR_CHECK(constructionPrice(c, r, nation) == 4212);
    c.hammers = 119;
    nation.prices[cargo::kTools] = 20;
    VCR_CHECK(constructionPrice(c, r, nation) == 13 + 24 * 91);
    nation.taxRate = 30; // no part in it
    VCR_CHECK(constructionPrice(c, r, nation) == 13 + 24 * 91);
    c.construction = -1;
    VCR_CHECK(constructionPrice(c, r, nation) == 0);
}

VCR_TEST("original files: BUY finishes the construction for gold") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    const ColonyArt colonyArt = loadColonyArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    auto save = readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {}));
    save.colonies[1].construction = 43; // a wagon train
    save.colonies[1].hammers = 30;
    save.nations[static_cast<std::size_t>(save.humanNation)].gold = 1000;
    PlayScreen play(Game(std::move(save), art.rules), dir / "unused.sav");
    play.setColonyArt(&colonyArt);
    play.setArt(&art);
    play.openColony(1);
    play.handle(Command::Purchase);
    VCR_CHECK(play.dialog().has_value());
    VCR_CHECK(play.dialog()->options.size() == 2);
    VCR_CHECK(play.dialog()->selected == 0); // "Never mind." first
    play.handle(Command::Down);
    play.handle(Command::Confirm);
    const auto& colony = play.game().state().colonies[1];
    VCR_CHECK(colony.hammers == 40);
    VCR_CHECK(play.game().state().nations[static_cast<std::size_t>(play.game().human())].gold == 1000 - 130);
}
