// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/data/savegame.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/render/game_view.hpp"
#include "viceroy/render/menu_bar.hpp"

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
    // Load Game goes back to the load menu.
    play.handle(Command::Menu);
    for (int i = 0; i < 5; ++i) {
        play.handle(Command::MoveS);
    }
    VCR_CHECK(play.menu()->selected == 5);
    VCR_CHECK(!play.handle(Command::Confirm));
    // In the orders menu a gold letter picks the entry: W for Wait.
    play.openMenu(2, false);
    play.menuLetter('w');
    VCR_CHECK(!play.menu().has_value());
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
    VCR_CHECK(play.dialog()->selected == 9);
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
