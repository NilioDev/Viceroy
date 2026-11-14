// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/data/savegame.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/render/game_view.hpp"
#include "viceroy/render/menu_bar.hpp"
#include "viceroy/render/pedia_view.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace viceroy;

namespace {

void dump(const Screen& screen, const Art& art, const std::string& name) {
    const char* base = std::getenv("VICEROY_DUMP_MENU");
    if (base == nullptr) {
        return;
    }
    std::ofstream out(std::string(base) + "_" + name, std::ios::binary);
    out.write(reinterpret_cast<const char*>(screen.pixels().data()),
              static_cast<std::streamsize>(screen.pixels().size()));
    for (const auto& c : art.palette) {
        out.put(static_cast<char>(c.r)).put(static_cast<char>(c.g)).put(static_cast<char>(c.b));
    }
}

} // namespace

VCR_TEST("original files: the menus are read from MENU.TXT") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    VCR_CHECK(art.menus.size() == 6);
    VCR_CHECK(art.menus[0].title == "~GAME" && art.menus[0].entries.size() == 9);
    VCR_CHECK(art.menus[2].entries.size() == 20 && art.menus[2].entries[1].hotkey == 'W');
    // Where the original puts the titles.
    VCR_CHECK(art.menus[0].x == 13 && art.menus[1].x == 45 && art.menus[2].x == 77);
    VCR_CHECK(art.menus[3].x == 115 && art.menus[4].x == 157 && art.menus[5].x == 255);

    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    const Bytes bytes(std::istreambuf_iterator<char>(in), {});
    Game game(readSaveGame(bytes), art.rules);
    game.setActiveUnit(51);
    ViewState view;
    centreOn(view, game, game.unit(51).x, game.unit(51).y);

    // The game menu, as the original shows it.
    Screen screen;
    OpenMenu open;
    open.menu = 0;
    open.states.assign(art.menus[0].entries.size(), EntryState::Enabled);
    drawGameScreen(screen, art, game, view);
    drawMenuBar(screen, art, art.menus, &open);
    VCR_CHECK(menuBox(art, art.menus[0], open).x == 12 && menuBox(art, art.menus[0], open).w == 88);
    VCR_CHECK(menuBox(art, art.menus[0], open).h == 109);
    dump(screen, art, "game");

    // The orders for the frigate, with "Wait for next unit" highlighted.
    open.menu = 2;
    using S = EntryState;
    open.states = {S::Enabled,  S::Enabled,  S::Enabled, S::Hidden,   S::Enabled, S::Hidden,  S::Hidden,
                   S::Hidden,   S::Disabled, S::Disabled, S::Disabled, S::Disabled, S::Hidden, S::Enabled,
                   S::Hidden,   S::Enabled,  S::Disabled, S::Enabled, S::Disabled, S::Enabled};
    open.selected = 1;
    drawGameScreen(screen, art, game, view);
    drawMenuBar(screen, art, art.menus, &open);
    VCR_CHECK(menuBox(art, art.menus[2], open).h == 149);
    VCR_CHECK(menuEntryAt(art, art.menus[2], open, 100, 22) == 1);
    VCR_CHECK(menuEntryAt(art, art.menus[2], open, 100, 45) == -1); // the line between groups
    VCR_CHECK(menuTitleAt(art, art.menus, 120, 3) == 3);
    dump(screen, art, "orders");

    open.menu = 5;
    open.selected = -1;
    open.states = {S::Enabled, S::Enabled, S::Enabled, S::Enabled, S::Enabled, S::Enabled, S::Hidden, S::Hidden};
    drawGameScreen(screen, art, game, view);
    drawMenuBar(screen, art, art.menus, &open);
    VCR_CHECK(menuBox(art, art.menus[5], open).x == 249);
    dump(screen, art, "pedia");
}

#include "viceroy/sound/background_music.hpp"
#include "viceroy/ui/play.hpp"

VCR_TEST("original files: the menus open, move and act") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    const Bytes bytes(std::istreambuf_iterator<char>(in), {});
    Game game(readSaveGame(bytes), art.rules);
    game.setActiveUnit(51);
    PlayScreen play(std::move(game), {});
    play.setArt(&art);

    // F10 opens the game menu with its first entry highlighted.
    play.handle(Command::Menu);
    VCR_CHECK(play.menu().has_value() && play.menu()->menu == 0 && play.menu()->selected == 0);
    play.handle(Command::MoveE);
    VCR_CHECK(play.menu()->menu == 1);
    play.handle(Command::MoveE);
    // The frigate's orders, as the original shows them.
    using S = EntryState;
    const std::vector<S> frigate = {S::Enabled,  S::Enabled,  S::Enabled,  S::Hidden,   S::Enabled,
                                    S::Hidden,   S::Hidden,   S::Hidden,   S::Disabled, S::Disabled,
                                    S::Disabled, S::Disabled, S::Hidden,   S::Enabled,  S::Hidden,
                                    S::Enabled,  S::Disabled, S::Enabled,  S::Disabled, S::Enabled};
    VCR_CHECK(play.menu()->menu == 2 && play.menu()->states == frigate);
    // The cursor skips grey entries: Sentry, then Go to Port.
    play.handle(Command::MoveS);
    play.handle(Command::MoveS);
    play.handle(Command::MoveS);
    VCR_CHECK(play.menu()->selected == 4);
    play.handle(Command::MoveS);
    VCR_CHECK(play.menu()->selected == 13);
    play.handle(Command::Cancel);
    VCR_CHECK(!play.menu().has_value());

    // A click on a title opens its menu, a click on an entry acts.
    VCR_CHECK(play.clickMenu(120, 3));
    VCR_CHECK(play.menu()->menu == 3);
    VCR_CHECK(play.clickMenu(50, 4)); // another title
    VCR_CHECK(play.menu()->menu == 1);
    VCR_CHECK(play.clickMenu(60, 21)); // View Pieces
    VCR_CHECK(!play.menu().has_value() && play.viewMode());
    VCR_CHECK(!play.clickMenu(100, 100)); // the map
    play.handle(Command::Menu);
    VCR_CHECK(play.clickMenu(200, 150)); // outside closes it
    VCR_CHECK(!play.menu().has_value());
    // Load Game opens the box of the ten slots.
    play.handle(Command::Menu);
    for (int i = 0; i < 5; ++i) {
        play.handle(Command::MoveS);
    }
    VCR_CHECK(play.menu()->selected == 5);
    VCR_CHECK(play.handle(Command::Confirm));
    VCR_CHECK(play.dialog() && play.dialog()->options.size() == 10);
    play.handle(Command::Cancel);
    VCR_CHECK(!play.dialog() && play.loadRequested().empty());
    // In the orders menu a gold letter picks the entry: W for Wait.
    play.openMenu(2, false);
    play.menuLetter('w');
    VCR_CHECK(!play.menu().has_value());
}

VCR_TEST("original files: Save Game and Load Game offer the original's slots") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    const auto work = std::filesystem::temp_directory_path() / "viceroy_test_slots";
    std::filesystem::remove_all(work);
    std::filesystem::create_directories(work);
    std::filesystem::copy_file(*findFileNoCase(dir, "COLONY04.SAV"), work / "COLONY09.SAV");
    std::ifstream in(work / "COLONY09.SAV", std::ios::binary);
    const Bytes bytes(std::istreambuf_iterator<char>(in), {});
    PlayScreen play(Game(readSaveGame(bytes), art.rules), work / "COLONY09.SAV");
    play.setArt(&art);
    const auto pick = [&](int entry) {
        play.handle(Command::Menu);
        for (int i = 0; i < entry; ++i) {
            play.handle(Command::MoveS);
        }
        play.handle(Command::Confirm);
    };

    // Save Game: the first eight slots, empty ones as the original names
    // them; Escape saves nothing.
    pick(4);
    VCR_CHECK(play.dialog() && play.dialog()->options.size() == 8);
    VCR_CHECK(play.dialog()->options[0] == "(EMPTY)");
    VCR_CHECK(play.dialog()->small && play.dialog()->x == 62 && play.dialog()->y == 59); // centred, as measured
    play.handle(Command::Cancel);
    VCR_CHECK(!play.dialog() && !std::filesystem::exists(work / "COLONY00.SAV"));
    pick(4);
    play.handle(Command::MoveS);
    play.handle(Command::MoveS);
    play.handle(Command::Confirm);
    VCR_CHECK(std::filesystem::exists(work / "COLONY02.SAV"));
    VCR_CHECK(play.savePath() == work / "COLONY02.SAV");
    VCR_CHECK(play.dialog().has_value());
    std::string said;
    for (const auto& line : play.dialog()->lines) {
        said += line + " ";
    }
    VCR_CHECK(said == "Conquistador Michiel de jager of the Dutch (Spring 1727) saved as COLONY02.SAV. "); // seen
    play.handle(Command::Confirm);

    // Load Game: all ten; an empty slot does nothing, a game is handed to
    // the app.
    pick(5);
    VCR_CHECK(play.dialog() && play.dialog()->options.size() == 10);
    VCR_CHECK(play.dialog()->options[2] == "Conquistador Michiel de jager of the Dutch, Spring 1727"); // seen
    VCR_CHECK(play.dialog()->options[9] == play.dialog()->options[2]);
    VCR_CHECK(play.dialog()->y == 51); // measured
    play.handle(Command::Confirm);
    VCR_CHECK(!play.dialog() && play.loadRequested().empty());
    pick(5);
    play.handle(Command::MoveS);
    play.handle(Command::MoveS);
    play.handle(Command::Confirm);
    VCR_CHECK(play.loadRequested() == work / "COLONY02.SAV");
    std::filesystem::remove_all(work);
}

VCR_TEST("original files: Sound Options turn music on and off") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    const Bytes bytes(std::istreambuf_iterator<char>(in), {});
    PlayScreen play(Game(readSaveGame(bytes), art.rules), {});
    play.setArt(&art);
    sound::BackgroundMusic music;
    play.setMusic(&music);
    const auto openOptions = [&] {
        play.openMenu(0, true);
        play.handle(Command::MoveS);
        play.handle(Command::MoveS);
        play.handle(Command::Confirm);
    };
    openOptions();
    VCR_CHECK(play.dialog().has_value() && play.dialog()->checkbox);
    VCR_CHECK(play.dialog()->options.size() == 3);
    VCR_CHECK(play.dialog()->checked == std::vector<bool>({true, true, true}));
    // The box where the original draws it, the ring with a dot in front
    // of each option.
    VCR_CHECK(play.dialog()->x == 62 && play.dialog()->y == 71 && dialogHeight(*play.dialog()) == 58);
    Screen screen;
    drawDialog(screen, art, *play.dialog());
    VCR_CHECK(screen.pixels()[92 * Screen::kWidth + 72] == 0x95 && screen.pixels()[93 * Screen::kWidth + 73] == 0x2F);
    dump(screen, art, "sound");
    // Space turns the option under the bar over; Escape drops the change.
    play.handle(Command::Skip);
    VCR_CHECK(!play.dialog()->checked[0]);
    play.handle(Command::Cancel);
    VCR_CHECK(!play.dialog().has_value() && music.options.background);
    VCR_CHECK(play.game().soundOptionBits() == 0x9E);
    // A letter picks its option; Return keeps the change.
    openOptions();
    play.dialogLetter('s');
    VCR_CHECK(play.dialog()->selected == 2 && !play.dialog()->checked[2]);
    play.handle(Command::Confirm);
    VCR_CHECK(!music.options.effects && music.options.events && play.game().soundOptionBits() == 0x96);
}

VCR_TEST("original files: Colony Report Options set the report bits (read from the original)") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    const Bytes bytes(std::istreambuf_iterator<char>(in), {});
    PlayScreen play(Game(readSaveGame(bytes), art.rules), {});
    play.setArt(&art);
    const int before = play.game().colonyReportBits();
    play.openMenu(0, true);
    play.handle(Command::MoveS);
    play.handle(Command::Confirm);
    VCR_CHECK(play.dialog().has_value() && play.dialog()->checkbox);
    VCR_CHECK(play.dialog()->options.size() == 10);
    for (std::size_t i = 0; i < 10; ++i) {
        VCR_CHECK(play.dialog()->checked[i] == ((before & colony_report::kOrder[i]) == 0));
    }
    // Turn over the reports of trained colonists and of rebel majorities.
    VCR_CHECK(play.dialog()->selected == 0);
    play.handle(Command::MoveS);
    play.handle(Command::MoveS);
    play.handle(Command::Skip);
    for (int i = 0; i < 7; ++i) {
        play.handle(Command::MoveS);
    }
    VCR_CHECK(play.dialog()->selected == 9);
    play.handle(Command::Skip);
    play.handle(Command::Confirm);
    VCR_CHECK(!play.dialog().has_value());
    VCR_CHECK(play.game().colonyReportBits() == (before ^ colony_report::kTraining ^ colony_report::kRebels));
}

VCR_TEST("original files: Game Options, autosave and the turn ending by itself (read from the original)") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    const auto work = std::filesystem::temp_directory_path() / "viceroy_test_options";
    std::filesystem::remove_all(work);
    std::filesystem::create_directories(work);
    std::filesystem::copy_file(*findFileNoCase(dir, "COLONY00.SAV"), work / "COLONY03.SAV");
    std::ifstream in(work / "COLONY03.SAV", std::ios::binary);
    const Bytes bytes(std::istreambuf_iterator<char>(in), {});
    PlayScreen play(Game(readSaveGame(bytes), art.rules), work / "COLONY03.SAV");
    play.setArt(&art);

    // The save's 0xC6: both Show Moves, Autosave, Combat Analysis and Water
    // Color Cycling ticked (seen in the original); this one has the
    // tutorial's hints on too.
    const int before = play.game().gameOptionBits();
    VCR_CHECK((before >> 8) == 0xC6 && (before & game_option::kTutorialHints) != 0);
    play.openMenu(0, true);
    play.handle(Command::Confirm);
    VCR_CHECK(play.dialog().has_value() && play.dialog()->checkbox);
    VCR_CHECK(play.dialog()->x == 62 && play.dialog()->y == 41); // measured
    const std::vector<bool> seen{true, true, false, false, true, true, true, true};
    VCR_CHECK(play.dialog()->checked == seen);
    // The gold letters: E turns End of Turn on, Y Water Color Cycling off,
    // I Show Indian Moves off and on again.
    play.dialogLetter('e');
    play.dialogLetter('y');
    play.dialogLetter('i');
    VCR_CHECK(!play.dialog()->checked[0]);
    play.dialogLetter('i');
    play.handle(Command::Confirm);
    VCR_CHECK(!play.dialog().has_value());
    VCR_CHECK(play.game().gameOptionBits() == (before | game_option::kEndOfTurn | game_option::kNoWaterCycling));

    // With End of Turn on, the game waits at the end of the turn.
    for (int i = 0; i < 200 && !play.waitingForEndOfTurn(); ++i) {
        play.handle(Command::Skip);
    }
    VCR_CHECK(play.waitingForEndOfTurn());
    const int year = play.game().state().year;
    const int season = play.game().state().season;
    play.tick(2000);
    VCR_CHECK(play.waitingForEndOfTurn() && play.game().state().year == year);
    // Without it the turn ends by itself, and the new turn is autosaved in
    // COLONY09.SAV while the game stays in its own slot.
    play.handle(Command::Menu);
    play.handle(Command::Cancel);
    auto& game = play.game();
    game.setGameOptionBits(game.gameOptionBits() & ~game_option::kEndOfTurn);
    for (int i = 0; i < 20 && play.game().state().year == year && play.game().state().season == season; ++i) {
        play.tick(100);
    }
    VCR_CHECK(play.game().state().year != year || play.game().state().season != season);
    VCR_CHECK(std::filesystem::exists(work / "COLONY09.SAV"));
    VCR_CHECK(play.savePath() == work / "COLONY03.SAV");
    std::filesystem::remove_all(work);
}

VCR_TEST("original files: Exit to DOS asks first, the bar on No (seen in the original)") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    const Bytes bytes(std::istreambuf_iterator<char>(in), {});
    PlayScreen play(Game(readSaveGame(bytes), art.rules), {});
    play.setArt(&art);
    const auto exitToDos = [&] {
        play.openMenu(0, true);
        for (int i = 0; i < 8; ++i) {
            play.handle(Command::MoveS);
        }
        play.handle(Command::Confirm);
    };
    exitToDos();
    VCR_CHECK(play.dialog().has_value() && play.dialog()->options.size() == 2);
    // Measured: a section without @width is 80 wide, the box centred.
    VCR_CHECK(play.dialog()->selected == 1 && play.dialog()->width == 80);
    VCR_CHECK(play.dialog()->x == 117 && play.dialog()->y == 77);
    play.handle(Command::Confirm);
    VCR_CHECK(!play.quitRequested());
    exitToDos();
    play.handle(Command::MoveN);
    play.handle(Command::Confirm);
    VCR_CHECK(play.quitRequested());
}

VCR_TEST("original files: the VIEW menu finds colonies, shows the hidden terrain and Europe (seen in the original)") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    const Bytes bytes(std::istreambuf_iterator<char>(in), {});
    PlayScreen play(Game(readSaveGame(bytes), art.rules), {});
    play.setArt(&art);
    const auto view = [&](int entry) {
        play.openMenu(1, true);
        for (int i = 0; i < entry; ++i) {
            play.handle(Command::MoveS);
        }
        play.handle(Command::Confirm);
    };
    const auto typeIn = [&](const std::string& text) {
        for (const char c : text) {
            play.type(c);
        }
        play.handle(Command::Confirm);
    };
    // Find Colony: the whole name in any case; the view cursor lands on it.
    const auto& colony = play.game().state().colonies.at(0);
    view(3);
    VCR_CHECK(play.dialog().has_value() && play.dialog()->input && play.dialog()->inputText.empty());
    VCR_CHECK(play.dialog()->x == 54 && play.dialog()->y == 81); // measured
    std::string shouted = colony.name;
    for (auto& c : shouted) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    typeIn(shouted);
    VCR_CHECK(!play.dialog().has_value());
    VCR_CHECK(play.viewMode() && play.cursorX() == colony.x && play.cursorY() == colony.y);
    play.handle(Command::MoveMode);
    // Part of a name is not enough.
    view(3);
    typeIn(colony.name.substr(0, 3));
    VCR_CHECK(play.dialog().has_value() && !play.dialog()->lines.empty());
    VCR_CHECK(play.dialog()->lines[0] == "\"" + colony.name.substr(0, 3) + "\" not found.");
    play.handle(Command::Confirm);
    VCR_CHECK(!play.viewMode());

    // Show Hidden Terrain stays until a key, which does nothing else.
    view(10);
    VCR_CHECK(play.view().hiddenTerrain);
    play.handle(Command::EndTurn);
    VCR_CHECK(!play.view().hiddenTerrain && play.game().activeUnit() >= 0);

    // European Status opens the home port.
    view(2);
    VCR_CHECK(play.europeScreen().has_value());
}

VCR_TEST("original files: the VIEW menu zooms out to the whole map and back (seen in the original)") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    const Bytes bytes(std::istreambuf_iterator<char>(in), {});
    PlayScreen play(Game(readSaveGame(bytes), art.rules), {});
    play.setArt(&art);
    const auto& map = play.game().state().map;
    VCR_CHECK(play.view().zoom == 0);
    play.handle(Command::ZoomIn); // already as close as it goes
    VCR_CHECK(play.view().zoom == 0);
    // Zoom Level 120 x 96: the whole map, centred in the window.
    play.openMenu(1, true);
    for (int i = 0; i < 6; ++i) {
        play.handle(Command::MoveS);
    }
    play.handle(Command::Confirm);
    VCR_CHECK(play.view().zoom == 3);
    VCR_CHECK(play.view().originX == 1 - (120 - (map.width - 2)) / 2);
    VCR_CHECK(play.view().originY == 1 - (96 - (map.height - 2)) / 2);
    play.handle(Command::ZoomOut);
    VCR_CHECK(play.view().zoom == 3);
    // Back in, the window is centred on the active unit again.
    play.handle(Command::ZoomIn);
    play.handle(Command::ZoomIn);
    VCR_CHECK(play.view().zoom == 1);
    const int active = play.game().activeUnit();
    VCR_CHECK(active >= 0);
    ViewState expected;
    expected.zoom = 1;
    centreOn(expected, play.game(), play.game().unit(active).x, play.game().unit(active).y);
    VCR_CHECK(play.view().originX == expected.originX && play.view().originY == expected.originY);
    VCR_CHECK(play.view().originX >= 1 && play.view().originX + 30 <= map.width - 1);
    // A click on the wood around the whole map does nothing.
    play.handle(Command::ZoomOut);
    play.handle(Command::ZoomOut);
    const auto before = play.view().originX;
    play.clickSquare(play.view().originX, play.view().originY);
    VCR_CHECK(play.view().originX == before && play.view().zoom == 3);

    Screen screen;
    drawGameScreen(screen, art, play.game(), play.view());
    // The land's name above the map, in white (measured: its top at row 24).
    bool white = false;
    for (int x = 60; x < 180; ++x) {
        white = white || screen.at(x, 24) == 0x0F; // white
    }
    VCR_CHECK(white);
}

VCR_TEST("original files: the Colonizopedia's index and pages (seen in the original)") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    // The index lists each part alphabetically, without the rows the
    // original leaves out.
    const std::size_t counts[pedia::kParts] = {16, 23, 21, 27, 38, 25};
    for (int part = 0; part < pedia::kParts; ++part) {
        const auto entries = pediaEntries(art, part);
        VCR_CHECK(entries.size() == counts[part]);
        for (std::size_t i = 1; i < entries.size(); ++i) {
            VCR_CHECK(entries[i - 1].name < entries[i].name);
        }
    }
    VCR_CHECK(pediaEntries(art, pedia::kCargo).front().name == "Cigars");
    VCR_CHECK(pediaEntries(art, pedia::kTerrain).front().name == "Arctic");
    VCR_CHECK(pediaEntries(art, pedia::kTerrain)[1].name == "Boreal Forest");
    VCR_CHECK(pediaEntries(art, pedia::kSkill).front().name == "Blacksmith");
    VCR_CHECK(pediaEntries(art, pedia::kFather).front().name == "Adam Smith");
    // Lines break as in the original: "in" does not fit after "them".
    const auto braves = pediaPageLines(art, pedia::kUnit, 21);
    VCR_CHECK(braves.size() > 2 && braves[1].ends_with("brought them"));
    VCR_CHECK(braves[2].starts_with("in the late"));
    // A line of the file without a trailing space counts 3 pixels more.
    const auto sugar = pediaPageLines(art, pedia::kCargo, 1);
    VCR_CHECK(sugar.size() > 2 && sugar[1].ends_with("profit or converted"));

    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    const Bytes bytes(std::istreambuf_iterator<char>(in), {});
    PlayScreen play(Game(readSaveGame(bytes), art.rules), {});
    play.setArt(&art);
    // Colonizopedia, Terrain Types: the index, Arctic picked.
    play.openMenu(5, true);
    for (int i = 0; i < 2; ++i) {
        play.handle(Command::MoveS);
    }
    play.handle(Command::Confirm);
    VCR_CHECK(play.pedia().has_value() && play.pedia()->part == pedia::kTerrain && play.pedia()->page < 0);
    play.handle(Command::MoveS);
    play.handle(Command::MoveS);
    play.handle(Command::MoveE); // one column only: nothing
    VCR_CHECK(play.pedia()->selected == 2);
    play.handle(Command::Confirm);
    VCR_CHECK(play.pedia()->page == 11); // Broadleaf Forest
    // Any key goes back to the index, Escape closes it.
    play.handle(Command::Cancel);
    VCR_CHECK(play.pedia().has_value() && play.pedia()->page < 0 && play.pedia()->selected == 2);
    play.handle(Command::Cancel);
    VCR_CHECK(!play.pedia().has_value());
    // F1: the page of the active unit's square, and a key back to the map.
    const int active = play.game().activeUnit();
    VCR_CHECK(active >= 0);
    play.handle(Command::TerrainHelp);
    VCR_CHECK(play.pedia().has_value());
    const auto& u = play.game().unit(active);
    VCR_CHECK(play.pedia()->page == pediaTerrainOf(play.game().state().map.at(u.x, u.y)));
    play.handle(Command::Confirm);
    VCR_CHECK(!play.pedia().has_value());
    // A click on (Exit) closes the index.
    play.openPedia(pedia::kFather);
    play.clickPedia(300, 7);
    VCR_CHECK(!play.pedia().has_value());
}

VCR_TEST("original files: Pick Music plays the tune picked") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    const Bytes bytes(std::istreambuf_iterator<char>(in), {});
    PlayScreen play(Game(readSaveGame(bytes), art.rules), {});
    play.setArt(&art);
    sound::BackgroundMusic music;
    play.setMusic(&music);
    music.pick(0x26); // Bonny Morn plays
    music.update(false, false);
    play.openMenu(0, true);
    for (int i = 0; i < 3; ++i) {
        play.handle(Command::MoveS);
    }
    play.handle(Command::Confirm);
    // Twelve tunes and three more lists; the bar starts on the tune playing.
    VCR_CHECK(play.dialog().has_value() && play.dialog()->options.size() == 15);
    VCR_CHECK(play.dialog()->selected == 9 && play.dialog()->marked == 9);
    Screen screen;
    drawDialog(screen, art, *play.dialog());
    dump(screen, art, "pick");
    for (int i = 0; i < 3; ++i) {
        play.handle(Command::MoveN);
    }
    play.handle(Command::Confirm); // Joe Clark
    VCR_CHECK(!play.dialog().has_value());
    VCR_CHECK(music.update(false, false) == std::vector<int>({1, 0x38})); // fade out, then the pick
    // Military Tunes, then its third: "Morelli's Lesson".
    play.openMenu(0, true);
    for (int i = 0; i < 3; ++i) {
        play.handle(Command::MoveS);
    }
    play.handle(Command::Confirm);
    VCR_CHECK(play.dialog()->selected == 6);
    for (int i = 0; i < 7; ++i) {
        play.handle(Command::MoveS);
    }
    play.handle(Command::Confirm);
    VCR_CHECK(play.dialog().has_value() && play.dialog()->options.size() == 4);
    play.handle(Command::MoveS);
    play.handle(Command::MoveS);
    play.handle(Command::Confirm);
    VCR_CHECK(music.update(false, false) == std::vector<int>({1, 0x30}));
    // Escape closes the list without a pick.
    play.openMenu(0, true);
    for (int i = 0; i < 3; ++i) {
        play.handle(Command::MoveS);
    }
    play.handle(Command::Confirm);
    play.handle(Command::Cancel);
    VCR_CHECK(!play.dialog().has_value() && music.update(true, false).empty());
}
