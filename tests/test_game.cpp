// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/text_table.hpp"
#include "viceroy/game/game.hpp"

#include <string>

using namespace viceroy;

namespace {

// A small made-up world: rules with one land type, forest, the "other"
// rows, a colonist, a scout and a ship; a 6×4 map with water on the left.
Ruleset sampleRules() {
    Ruleset r;
    r.terrain.resize(21);
    for (auto& t : r.terrain) {
        t.movementCost = 1;
    }
    for (std::size_t i = 8; i < 16; ++i) {
        r.terrain[i].movementCost = 2;
    }
    r.terrain[19].movementCost = 3; // mountains
    r.terrain[20].movementCost = 2; // hills
    r.units.resize(14);
    r.units[unit_type::kColonist] = {"Colonists", 101, 1, 0, 1, 0, 1, 1, 0, 0, 0, 0};
    r.units[unit_type::kScout] = {"Scouts", 104, 4, 1, 1, 0, 1, 2, 0, 0, 0, 0};
    r.units[unit_type::kCaravel] = {"Caravel", 6, 4, 0, 2, 2, 99, 4, 4, 0, 4, 0};
    return r;
}

SaveGame sampleSave() {
    SaveGame s;
    s.year = 1598;
    s.season = 0;
    s.humanNation = 3;
    s.map.width = 6;
    s.map.height = 4;
    s.map.squares.resize(24);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 6; ++x) {
            auto& sq = s.map.at(x, y);
            sq.terrain = x < 2 ? 25 : 2; // ocean, then plains
        }
    }
    s.map.at(4, 1).terrain = 10; // forest
    s.map.at(5, 1).relief = 5;   // mountains
    auto unit = [&](int x, int y, int type, int nation) {
        SaveUnit u;
        u.x = x;
        u.y = y;
        u.type = type;
        u.nation = nation;
        s.units.push_back(u);
    };
    unit(2, 2, unit_type::kColonist, 3); // 0
    unit(3, 2, unit_type::kScout, 3);    // 1
    unit(1, 2, unit_type::kCaravel, 3);  // 2
    unit(4, 3, unit_type::kColonist, 1); // 3: French
    unit(230, 230, unit_type::kColonist, 3); // 4: in Europe
    return s;
}

} // namespace

VCR_TEST("game: units move, pay for terrain and stop when out of moves") {
    Game g(sampleSave(), sampleRules());
    VCR_CHECK(g.movementAllowance(1) == 12);
    VCR_CHECK(g.move(1, {1, -1}) == MoveResult::Moved); // into the forest
    VCR_CHECK(g.unit(1).x == 4 && g.unit(1).y == 1);
    VCR_CHECK(g.movesLeft(1) == 6);
    // Mountains cost 9: with 6 left the unit gets there two times in three,
    // or its turn is over.
    const MoveResult r = g.move(1, {1, 0});
    VCR_CHECK((r == MoveResult::NoMovesLeft && g.unit(1).x == 4) || (r == MoveResult::Moved && g.unit(1).x == 5));
    VCR_CHECK(g.movesLeft(1) == 0);

    // A unit that has not moved yet always gets one step.
    VCR_CHECK(g.move(0, {0, -1}) == MoveResult::Moved);
    VCR_CHECK(g.movesLeft(0) == 0);
    VCR_CHECK(g.move(0, {0, -1}) == MoveResult::NoMovesLeft);
}

VCR_TEST("game: land and water keep units apart, ships carry them") {
    Game g(sampleSave(), sampleRules());
    VCR_CHECK(g.move(2, {1, 0}) == MoveResult::Blocked);   // ship onto land
    VCR_CHECK(g.move(0, {-1, -1}) == MoveResult::Blocked); // colonist into empty sea
    VCR_CHECK(g.move(0, {-1, 0}) == MoveResult::Boarded);  // onto the caravel
    VCR_CHECK(g.isAboard(0));
    VCR_CHECK(g.unit(0).orders == SaveUnit::kSentry);
    VCR_CHECK(g.move(2, {-1, 0}) == MoveResult::Moved); // the ship sails off ...
    VCR_CHECK(g.unit(0).x == 0 && g.unit(0).y == 2);    // ... with the colonist
    VCR_CHECK(g.move(1, {1, 1}) == MoveResult::Occupied); // the French colonist
    VCR_CHECK(g.passengers(2) == std::vector<int>{0});
    VCR_CHECK(g.move(2, {1, 0}) == MoveResult::Moved); // back to the coast
    g.endTurn();
    g.setOrders(0, SaveUnit::kNoOrders);
    VCR_CHECK(g.move(0, {1, -1}) == MoveResult::Moved); // ashore
    VCR_CHECK(!g.isAboard(0) && g.unit(0).x == 2 && g.unit(0).y == 1);
}

VCR_TEST("game: the next unit needing orders skips the moved, the sentried and Europe") {
    Game g(sampleSave(), sampleRules());
    VCR_CHECK(g.nextUnitNeedingOrders(-1) == 0);
    g.skipUnit(0);
    VCR_CHECK(g.nextUnitNeedingOrders(0) == 1);
    g.setOrders(1, SaveUnit::kSentry);
    VCR_CHECK(g.nextUnitNeedingOrders(1) == 2);
    g.skipUnit(2);
    VCR_CHECK(g.nextUnitNeedingOrders(2) == -1);
}

VCR_TEST("game: turns follow the calendar") {
    Game g(sampleSave(), sampleRules());
    g.skipUnit(0);
    g.endTurn();
    VCR_CHECK(g.state().year == 1599 && g.state().season == 0);
    VCR_CHECK(g.movesLeft(0) == 3);
    g.endTurn();
    VCR_CHECK(g.state().year == 1600 && g.state().season == 0);
    g.endTurn();
    VCR_CHECK(g.state().year == 1600 && g.state().season == 1);
    g.endTurn();
    VCR_CHECK(g.state().year == 1601 && g.state().season == 0);
}

VCR_TEST("game: colonists found colonies, not too close and not on mountains") {
    Game g(sampleSave(), sampleRules());
    VCR_CHECK(g.canFoundColony(2) == FoundResult::NotAColonist);
    VCR_CHECK(g.foundColony(0, "Haven") == FoundResult::Founded);
    VCR_CHECK(g.state().colonies.size() == 1);
    VCR_CHECK(g.state().colonies[0].name == "Haven" && g.state().colonies[0].population == 1);
    VCR_CHECK(g.state().map.at(2, 2).flags & MapSquare::kHasColony);
    VCR_CHECK(g.unitCount() == 4); // the colonist joined the colony
    VCR_CHECK(g.unit(0).type == unit_type::kScout);
    VCR_CHECK(g.canFoundColony(0) == FoundResult::TooClose); // the scout, next door
    g.state().units[0].x = 5;
    g.state().units[0].y = 1;
    VCR_CHECK(g.canFoundColony(0) == FoundResult::Mountains);
}

VCR_TEST("game: moving reveals the map") {
    Game g(sampleSave(), sampleRules());
    VCR_CHECK(!g.state().map.at(5, 0).seenBy(3));
    VCR_CHECK(g.move(1, {1, 0}) == MoveResult::Moved); // the scout sees two squares
    VCR_CHECK(g.state().map.at(5, 0).seenBy(3));
    VCR_CHECK(!g.state().map.at(5, 0).seenBy(1));
}

VCR_TEST("game: how far units see (measured)") {
    // A colonist sees one square around, two with Hernando de Soto; a
    // scout one more (measured: a dragoon 1 and 2, a scout 2 and 3).
    const auto seen = [](bool deSoto, int type) {
        SaveGame s = sampleSave();
        s.map.width = 14;
        s.map.height = 9;
        s.map.squares.assign(14 * 9, MapSquare{});
        for (auto& sq : s.map.squares) {
            sq.terrain = 2;
        }
        s.units.resize(1);
        s.units[0].x = 4;
        s.units[0].y = 4;
        s.units[0].type = type;
        if (deSoto) {
            s.nations[3].raw[7] |= 0x80; // father 7
        }
        Game g(s, sampleRules());
        VCR_CHECK(g.move(0, {1, 0}) == MoveResult::Moved);
        int far = 0;
        while (g.state().map.at(5 + far + 1, 4).seenBy(3)) {
            ++far;
        }
        return far;
    };
    VCR_CHECK(seen(false, unit_type::kColonist) == 1);
    VCR_CHECK(seen(true, unit_type::kColonist) == 2);
    VCR_CHECK(seen(false, unit_type::kScout) == 2);
    VCR_CHECK(seen(true, unit_type::kScout) == 3);
}

VCR_TEST("game: colony names come from the nation's list") {
    Game g(sampleSave(), sampleRules());
    const std::string text = "@DUTCH\r\nNieuw,1600\r\nOud\r\n@STOP\r\n";
    const auto names = parseTextFile(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
    g.setColonyNames(names);
    // The count of names given picks the line, used or not.
    VCR_CHECK(g.nextColonyName(3) == "Nieuw");
    VCR_CHECK(g.nextColonyName(3) == "Oud");
    VCR_CHECK(g.nextColonyName(3) == "Nieuw"); // past the end: the first again
    VCR_CHECK(g.state().players[3].raw[50] == 2);
}

#include "viceroy/game_files.hpp"
#include "viceroy/render/game_view.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

VCR_TEST("original files: the game screen is drawn from a savegame") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    const Bytes bytes(std::istreambuf_iterator<char>(in), {});
    Game game(readSaveGame(bytes), art.rules);
    game.setActiveUnit(51); // the frigate the original shows first
    ViewState view;
    centreOn(view, game, game.unit(51).x, game.unit(51).y);
    VCR_CHECK(view.originX == 37 && view.originY == 23);
    Screen screen;
    drawGameScreen(screen, art, game, view);
    VCR_CHECK(screen.at(240, 100) == 0); // the black line left of the panel
    VCR_CHECK(unitIcon(game, 51) == 15); // a frigate
    // For comparing with the original by eye: VICEROY_DUMP_SCREEN=file.
    if (const char* dump = std::getenv("VICEROY_DUMP_SCREEN")) {
        // VICEROY_DUMP_ORIGIN=x,y shows another part of the map.
        if (const char* origin = std::getenv("VICEROY_DUMP_ORIGIN")) {
            std::sscanf(origin, "%d,%d", &view.originX, &view.originY);
            drawGameScreen(screen, art, game, view);
        }
        std::ofstream out(dump, std::ios::binary);
        out.write(reinterpret_cast<const char*>(screen.pixels().data()),
                  static_cast<std::streamsize>(screen.pixels().size()));
        for (const auto& c : art.palette) {
            out.put(static_cast<char>(c.r)).put(static_cast<char>(c.g)).put(static_cast<char>(c.b));
        }
    }
}

#include "viceroy/ui/play.hpp"

VCR_TEST("original files: a turn can be played and saved") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    const auto work = std::filesystem::temp_directory_path() / "viceroy_test_play";
    std::filesystem::remove_all(work);
    std::filesystem::create_directories(work);
    std::filesystem::copy_file(*findFileNoCase(dir, "COLONY09.SAV"), work / "COLONY09.SAV");

    const auto slots = listSaves(work, art);
    VCR_CHECK(slots.size() == 1);
    LoadMenu menu(slots);
    const auto chosen = menu.handle(Command::Confirm);
    VCR_CHECK(chosen.has_value());

    std::ifstream in(chosen->path, std::ios::binary);
    const Bytes bytes(std::istreambuf_iterator<char>(in), {});
    PlayScreen play(Game(readSaveGame(bytes), art.rules), chosen->path);
    play.setColonyNames(&art.colonyNames);
    const int year = play.game().state().year;
    VCR_CHECK(play.game().activeUnit() >= 0);

    // Give every unit "no orders this turn" until the turn is over.
    for (int i = 0; i < 200 && !play.waitingForEndOfTurn(); ++i) {
        play.handle(Command::Skip);
    }
    VCR_CHECK(play.waitingForEndOfTurn());
    play.handle(Command::Confirm);
    VCR_CHECK(play.game().state().year == year + 1 || play.game().state().season == 1);
    VCR_CHECK(!play.waitingForEndOfTurn());

    // Walk the first unit somewhere and save; the save reads back the same.
    for (int d = 0; d < 8; ++d) {
        play.handle(static_cast<Command>(d));
    }
    VCR_CHECK(play.save());
    std::ifstream again(chosen->path, std::ios::binary);
    const Bytes saved(std::istreambuf_iterator<char>(again), {});
    VCR_CHECK(saved == writeSaveGame(play.game().state()));
    VCR_CHECK(readSaveGame(saved).turn == play.game().state().turn);

    Screen screen;
    play.draw(screen, art);
    std::filesystem::remove_all(work);
}

VCR_TEST("original files: units glide, and a click far away moves the map") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    const Bytes bytes(std::istreambuf_iterator<char>(in), {});
    Game game(readSaveGame(bytes), art.rules);
    game.setActiveUnit(51); // the frigate the original shows first
    PlayScreen play(std::move(game), {});
    play.setAnimations(true);
    const int frigate = play.game().activeUnit();
    VCR_CHECK(frigate == 51);
    const int x = play.game().unit(frigate).x;
    const int y = play.game().unit(frigate).y;

    // A click on a square far from the frigate brings the map round to it.
    play.clickSquare(x - 10, y + 6);
    VCR_CHECK(play.game().unit(frigate).x == x);
    VCR_CHECK(play.view().originX == x - 10 - kViewColumns / 2);
    // A click on the small map does the same, one square up and left of the
    // pointer's tip, as measured on the original.
    const int top = minimapTop(play.game(), play.view());
    const auto square = minimapSquare(play.game(), play.view(), kMinimapArea.x + 20, kMinimapArea.y + 10);
    VCR_CHECK(square.has_value() && square->first == 19 && square->second == top + 9);
    play.clickMinimap(square->first, square->second);
    VCR_CHECK(play.view().originX == 19 - kViewColumns / 2);

    // A move glides; commands wait until the frigate has arrived.
    play.handle(Command::MoveN);
    VCR_CHECK(play.animating());
    VCR_CHECK(play.view().slideUnit == frigate && play.view().slideY == kSquare);
    VCR_CHECK(play.game().unit(frigate).y == y - 1);
    play.handle(Command::MoveN);
    VCR_CHECK(play.game().unit(frigate).y == y - 1);
    play.tick(80);
    VCR_CHECK(play.view().slideY > 0 && play.view().slideY < kSquare);
    play.tick(200);
    VCR_CHECK(!play.animating() && play.view().slideUnit == -1);
    VCR_CHECK(play.game().activeUnit() == frigate);
    play.handle(Command::MoveN);
    VCR_CHECK(play.game().unit(frigate).y == y - 2);
}
