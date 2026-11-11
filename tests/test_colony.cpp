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
    drawColonyScreen(screen, art, colonyArt, game, {which, 0});
    VCR_CHECK(screen.at(200, 50) == 0);
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
    VCR_CHECK(choices.size() == 18);
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
