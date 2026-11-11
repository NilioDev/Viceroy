// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/render/europe_view.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

using namespace viceroy;

VCR_TEST("europe: a unit's stage follows from its position") {
    SaveUnit u;
    u.nation = 3;
    setEuropeStage(u, 0);
    VCR_CHECK(u.x == 231 && u.y == 231);
    VCR_CHECK(europePlace(u) == EuropePlace::ToNewWorld);
    setEuropeStage(u, 2);
    VCR_CHECK(u.x == 239 && europePlace(u) == EuropePlace::InPort);
    setEuropeStage(u, 4);
    VCR_CHECK(u.x == 247 && europePlace(u) == EuropePlace::ToEurope);
    u.nation = 1;
    u.x = u.y = 233; // French, stage 1
    VCR_CHECK(europeStage(u) == 1);
    u.x = u.y = 40;
    VCR_CHECK(europePlace(u) == EuropePlace::None);
}

namespace {

Game loadSave(const Art& art, const char* name) {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    const std::filesystem::path path = std::filesystem::exists(name) ? std::filesystem::path(name) : *findFileNoCase(dir, name);
    std::ifstream in(path, std::ios::binary);
    return Game(readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {})), art.rules);
}

} // namespace

VCR_TEST("original files: the market and the home port") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    const ColonyArt colonyArt = loadColonyArt(dir);
    const char* saveName = std::getenv("VICEROY_SAVE") ? std::getenv("VICEROY_SAVE") : "COLONY00.SAV";
    Game game = loadSave(art, saveName);
    const auto& dutch = game.state().nations[3];
    if (std::string(saveName) == "COLONY00.SAV") {
        // As the original shows them: food 0/8, sugar 6/8, lumber 1/6, silver 13/14, muskets 9/10.
        VCR_CHECK(bidPrice(dutch, 0) == 0 && askPrice(game.rules(), dutch, 0) == 8);
        VCR_CHECK(bidPrice(dutch, 1) == 6 && askPrice(game.rules(), dutch, 1) == 8);
        VCR_CHECK(bidPrice(dutch, 5) == 1 && askPrice(game.rules(), dutch, 5) == 6);
        VCR_CHECK(bidPrice(dutch, 7) == 13 && askPrice(game.rules(), dutch, 7) == 14);
        VCR_CHECK(bidPrice(dutch, 15) == 9 && askPrice(game.rules(), dutch, 15) == 10);
        // A galleon bound for the New World with three colonists aboard.
        const auto going = unitsInEurope(game.state(), game.rules(), 3, EuropePlace::ToNewWorld, true);
        VCR_CHECK(going.size() == 1 && going[0] == 56);
        VCR_CHECK(carrierOf(game.state(), game.rules(), 85) == 56);
    }
    Screen screen;
    EuropeViewState view;
    const auto port = unitsInEurope(game.state(), game.rules(), 3, EuropePlace::InPort, true);
    view.ship = port.empty() ? -1 : port.front();
    drawEuropeScreen(screen, art, colonyArt, game, view);
    if (const char* dump = std::getenv("VICEROY_DUMP_EUROPE")) {
        std::ofstream out(dump, std::ios::binary);
        out.write(reinterpret_cast<const char*>(screen.pixels().data()),
                  static_cast<std::streamsize>(screen.pixels().size()));
        for (const auto& c : art.palette) {
            out.put(static_cast<char>(c.r)).put(static_cast<char>(c.g)).put(static_cast<char>(c.b));
        }
    }
}

#include "viceroy/ui/play.hpp"

VCR_TEST("original files: E opens the home port and Escape closes it") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    const ColonyArt colonyArt = loadColonyArt(dir);
    PlayScreen play(loadSave(art, "COLONY00.SAV"), {});
    play.setColonyArt(&colonyArt);
    VCR_CHECK(play.handle(Command::Europe));
    VCR_CHECK(play.europeScreen().has_value());
    VCR_CHECK(play.europeScreen()->ship == -1); // the galleon has left
    play.handle(Command::Right);
    VCR_CHECK(play.europeScreen()->good == 1);
    play.handle(Command::Left);
    play.handle(Command::Left);
    VCR_CHECK(play.europeScreen()->good == kGoodsKinds - 1);
    Screen screen;
    play.draw(screen, art);
    VCR_CHECK(screen.at(10, 181 + 5) != 0); // the market is drawn
    VCR_CHECK(play.handle(Command::Cancel));
    VCR_CHECK(!play.europeScreen().has_value());
}

#include "viceroy/render/dialog.hpp"

VCR_TEST("original files: the high seas question") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    Dialog d = makeDialog(art, "SAILHOME");
    VCR_CHECK(d.lines.size() == 2);
    VCR_CHECK(d.options.size() == 2 && d.selected == 0);
    VCR_CHECK(dialogHeight(d) == 56);
    if (const char* dump = std::getenv("VICEROY_DUMP_DIALOG")) {
        Screen screen;
        d.x = 64;
        d.y = 108;
        d.portrait = kNavalAdvisor;
        d.portraitX = 60;
        d.portraitY = 37;
        drawDialog(screen, art, d);
        std::ofstream out(dump, std::ios::binary);
        out.write(reinterpret_cast<const char*>(screen.pixels().data()),
                  static_cast<std::streamsize>(screen.pixels().size()));
        for (const auto& c : art.palette) {
            out.put(static_cast<char>(c.r)).put(static_cast<char>(c.g)).put(static_cast<char>(c.b));
        }
    }
}

VCR_TEST("original files: a ship sails to Europe, trades and comes back") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    const ColonyArt colonyArt = loadColonyArt(dir);
    Game game = loadSave(art, "COLONY00.SAV");
    // The Dutch caravel next to the high seas east of St. Louis.
    auto& caravel = game.state().units[0];
    VCR_CHECK(caravel.nation == 3 && game.isShip(0));
    caravel.x = 49;
    caravel.y = 32;
    caravel.movesUsed = 0;
    PlayScreen play(std::move(game), {});
    play.setArt(&art);
    play.setColonyArt(&colonyArt);
    auto& g = play.game();
    g.setActiveUnit(0);
    play.handle(Command::MoveE);
    VCR_CHECK(play.dialog().has_value());
    VCR_CHECK(play.dialog()->options.size() == 2);
    play.handle(Command::Confirm); // "Yes, steady as she goes."
    VCR_CHECK(europePlace(g.unit(0)) == EuropePlace::ToEurope);
    VCR_CHECK(g.state().nations[3].raw[50] == 50 && g.state().nations[3].raw[51] == 32);

    g.endTurn();
    VCR_CHECK(europePlace(g.unit(0)) == EuropePlace::ToEurope);
    g.endTurn();
    VCR_CHECK(europePlace(g.unit(0)) == EuropePlace::InPort);

    // In port: sell the furs aboard, buy tools, take the colonists along.
    play.handle(Command::Europe);
    VCR_CHECK(play.europeScreen()->ship == 0);
    const int goldBefore = g.state().nations[3].gold;
    const int furs = holdAmount(g.unit(0), 0);
    VCR_CHECK(holdGood(g.unit(0), 0) == 4 && furs > 0);
    for (int i = 0; i < 4; ++i) {
        play.handle(Command::Right); // to furs
    }
    play.handle(Command::Sentry); // sell
    VCR_CHECK(g.unit(0).holds == 0);
    VCR_CHECK(g.state().nations[3].gold > goldBefore);
    for (int i = 0; i < 10; ++i) {
        play.handle(Command::Right); // to tools
    }
    VCR_CHECK(play.europeScreen()->good == 14);
    play.handle(Command::Confirm); // buy 100
    VCR_CHECK(g.unit(0).holds == 1 && holdGood(g.unit(0), 0) == 14 && holdAmount(g.unit(0), 0) == 100);

    Screen screen;
    play.draw(screen, art);

    play.handle(Command::Up); // to the ship
    play.handle(Command::Confirm);
    VCR_CHECK(play.dialog().has_value());
    play.handle(Command::Confirm); // sail
    VCR_CHECK(europePlace(g.unit(0)) == EuropePlace::ToNewWorld);
    play.handle(Command::Cancel);
    g.endTurn();
    g.endTurn();
    VCR_CHECK(g.unit(0).x == 50 && g.unit(0).y == 32);
    VCR_CHECK(holdAmount(g.unit(0), 0) == 100);
}

VCR_TEST("original files: colonists on the docks board a ship in port") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    Game game = loadSave(art, "COLONY00.SAV");
    // The galleon and its three colonists, back in port; the colonists on the docks.
    for (const int u : {56, 85, 86, 87}) {
        setEuropeStage(game.state().units[static_cast<std::size_t>(u)], kPortStage);
    }
    for (const int u : {85, 86, 87}) {
        game.leaveShipInPort(u);
    }
    VCR_CHECK(unitsOnDocks(game.state(), game.rules(), 3).size() == 3);
    VCR_CHECK(game.passengersInPort(56).empty());
    VCR_CHECK(game.boardInPort(85, 56) && game.boardInPort(86, 56));
    VCR_CHECK(game.passengersInPort(56).size() == 2);
    VCR_CHECK(unitsOnDocks(game.state(), game.rules(), 3).size() == 1);
    VCR_CHECK(game.freeHolds(56) == 4);
    VCR_CHECK(game.sailToNewWorld(56));
    VCR_CHECK(europePlace(game.unit(85)) == EuropePlace::ToNewWorld);
    VCR_CHECK(europePlace(game.unit(87)) == EuropePlace::InPort);
    game.endTurn();
    game.endTurn();
    VCR_CHECK(game.onMap(56) && game.onMap(85) && game.onMap(86) && game.isAboard(85));
    VCR_CHECK(!game.onMap(87));
}
