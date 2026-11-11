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
        for (int i = 0; i < 256; ++i) {
            const auto c = screen.colour(art.palette, static_cast<std::uint8_t>(i));
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

VCR_TEST("europe: recruit prices as measured on the original") {
    SaveNation n;
    // (recruits so far, crosses, crosses needed) -> price, read off the original's Recruit box.
    struct Case {
        int count, crosses, needed, price;
    };
    for (const Case c : {Case{32, 30, 284, 715}, Case{28, 31, 268, 636}, Case{12, 88, 106, 150}, Case{12, 0, 106, 380},
                         Case{0, 0, 106, 140}, Case{30, 0, 106, 740}, Case{12, 50, 106, 250}, Case{30, 80, 106, 298},
                         Case{0, 106, 106, 101}, Case{20, 106, 106, 113}, Case{200, 106, 106, 859}}) {
        n.recruitCount = c.count;
        n.crosses = c.crosses;
        n.crossesNeeded = c.needed;
        VCR_CHECK(recruitPrice(n) == c.price);
    }
    n.artilleryBought = 8;
    VCR_CHECK(artilleryPrice(n) == 1300);
}

VCR_TEST("original files: Recruit, Purchase and Train") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    Art art = loadArt(dir);
    const ColonyArt colonyArt = loadColonyArt(dir);
    PlayScreen play(loadSave(art, "COLONY03.SAV"), {});
    play.setColonyArt(&colonyArt);
    play.setArt(&art);
    const auto& dutch = play.game().state().nations[3];
    VCR_CHECK(dutch.recruits[0] == 6 && dutch.recruits[1] == 5 && dutch.recruits[2] == kFreeColonist);
    VCR_CHECK(recruitPrice(dutch) == 150);
    VCR_CHECK(artilleryPrice(dutch) == 1300);

    // The training list in the original's order.
    const auto rows = trainingList(play.game().rules());
    VCR_CHECK(rows.size() == 17);
    VCR_CHECK(rows[0] == 6 && rows[1] == 5 && rows[5] == 13 && rows[6] == 8 && rows[10] == 20 && rows[16] == 21);

    play.handle(Command::Europe);
    const auto dump = [&](const char* name) {
        const char* base = std::getenv("VICEROY_DUMP_DIALOG");
        if (base == nullptr) {
            return;
        }
        Screen screen;
        play.draw(screen, art);
        std::ofstream out(std::string(base) + "_" + name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(screen.pixels().data()),
                  static_cast<std::streamsize>(screen.pixels().size()));
        for (int i = 0; i < 256; ++i) {
            const auto c = screen.colour(art.palette, static_cast<std::uint8_t>(i));
            out.put(static_cast<char>(c.r)).put(static_cast<char>(c.g)).put(static_cast<char>(c.b));
        }
    };

    play.handle(Command::Recruit);
    VCR_CHECK(play.dialog().has_value());
    VCR_CHECK(play.dialog()->options.size() == 4);
    VCR_CHECK(play.dialog()->options[1] == "Expert Ore Miners");
    VCR_CHECK(play.dialog()->x == 62 && play.dialog()->y == 88 && dialogHeight(*play.dialog()) == 103);
    dump("recruit");
    const int units = play.game().unitCount();
    const int gold = dutch.gold;
    play.handle(Command::Down);
    play.handle(Command::Confirm);
    VCR_CHECK(play.game().unitCount() == units + 1);
    VCR_CHECK(dutch.gold == gold - 150);
    VCR_CHECK(dutch.recruitCount == 13 && dutch.crosses == 0);
    VCR_CHECK(recruitPrice(dutch) == 400); // as the original asks next
    VCR_CHECK(play.game().unit(units).profession == 6);
    VCR_CHECK(europePlace(play.game().unit(units)) == EuropePlace::InPort);
    VCR_CHECK(unitsOnDocks(play.game().state(), play.game().rules(), 3).front() == units);

    play.game().state().nations[3].gold = 1062;
    play.handle(Command::Purchase);
    VCR_CHECK(play.dialog()->y == 80 && dialogHeight(*play.dialog()) == 119);
    VCR_CHECK(play.dialog()->values[2] == "(Cost: 1000)");
    VCR_CHECK(play.dialog()->disabled[1] && !play.dialog()->disabled[2]); // 1062 gold buys no artillery
    dump("purchase");
    play.handle(Command::Down); // skips the artillery
    VCR_CHECK(play.dialog()->selected == 2);
    play.handle(Command::Cancel);
    VCR_CHECK(!play.dialog().has_value());

    play.handle(Command::Train);
    VCR_CHECK(play.dialog()->small);
    VCR_CHECK(play.dialog()->y == 10 && dialogHeight(*play.dialog()) == 180);
    VCR_CHECK(play.dialog()->values[1] == "(Cost: 600)");
    dump("train");
    for (int i = 0; i < 12; ++i) {
        play.handle(Command::Down);
    }
    VCR_CHECK(play.dialog()->selected == 3); // the cursor wraps over the eight it can afford
    play.handle(Command::Confirm);
    const int gunsmith = play.game().unitCount() - 1;
    VCR_CHECK(play.game().unit(gunsmith).profession == 15);
    VCR_CHECK(play.game().state().nations[3].gold == 1062 - 850);

    play.game().state().nations[3].gold = 5000;
    play.handle(Command::Purchase);
    play.handle(Command::Down);
    play.handle(Command::Confirm);
    VCR_CHECK(play.dialog().has_value()); // "Purchase Artillery for 1300$?"
    dump("reallybuy");
    play.handle(Command::Confirm);
    const int artillery = play.game().unitCount() - 1;
    VCR_CHECK(play.game().unit(artillery).type == kArtillery);
    VCR_CHECK(play.game().state().nations[3].gold == 3700);
    VCR_CHECK(artilleryPrice(play.game().state().nations[3]) == 1400);
}

VCR_TEST("original files: selling moves the market as in the original") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    Game game = loadSave(art, "COLONY00.SAV");
    auto& dutch = game.state().nations[3];
    VCR_CHECK(dutch.prices[1] == 7 && dutch.volumes[1] == 143);
    // The original: four loads of 100 sugar leave the price at 6/8, the fifth brings it to 5/7.
    for (int i = 0; i < 4; ++i) {
        VCR_CHECK(moveMarket(game.rules(), dutch, 1, 100) == 0);
    }
    VCR_CHECK(moveMarket(game.rules(), dutch, 1, 100) == -1);
    VCR_CHECK(dutch.prices[1] == 6);
    VCR_CHECK(bidPrice(dutch, 1) == 5 && askPrice(game.rules(), dutch, 1) == 7);
    // Buying pushes the other way.
    VCR_CHECK(moveMarket(game.rules(), dutch, 1, -500) == 1);
    VCR_CHECK(dutch.prices[1] == 7);
}
