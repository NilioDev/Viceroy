// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/render/europe_view.hpp"

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
    std::ifstream in(*findFileNoCase(dir, name), std::ios::binary);
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
