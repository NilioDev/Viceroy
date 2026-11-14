// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/savegame.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/render/trade_route_view.hpp"
#include "viceroy/ui/play.hpp"

#include <filesystem>
#include <string>

using namespace viceroy;

namespace {

constexpr int kSugar = 1;
constexpr int kFurs = 4;
constexpr int kTools = 14;
constexpr int kWagon = 0; // unit 0

// An 8×6 world, ocean in the first three columns: a Dutch colony on the
// coast at (3, 1) with furs in store and one inland at (6, 1), a wagon
// train in the first.
SaveGame routeSave() {
    SaveGame s;
    s.year = 1600;
    s.humanNation = 3;
    s.map.width = 8;
    s.map.height = 6;
    s.map.squares.resize(48);
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 8; ++x) {
            auto& sq = s.map.at(x, y);
            sq.terrain = x < 3 ? 25 : 2;
            sq.region = x < 3 ? 1 : 2;
        }
    }
    s.nations[3].prices[kFurs] = 4;
    s.nations[3].prices[kSugar] = 3;
    SaveColony coast;
    coast.x = 3;
    coast.y = 1;
    coast.name = "Haven";
    coast.nation = 3;
    coast.stock[kFurs] = 150;
    coast.stock[kSugar] = 30;
    s.colonies.push_back(coast);
    SaveColony inland = coast;
    inland.x = 6;
    inland.name = "Upland";
    inland.stock = {};
    s.colonies.push_back(inland);
    SaveUnit wagon;
    wagon.x = 3;
    wagon.y = 1;
    wagon.type = unit_type::kWagonTrain;
    wagon.nation = 3;
    s.units.push_back(wagon);
    return s;
}

Game::TradeRoute furRun() {
    Game::TradeRoute r;
    r.name = "Fur Run";
    r.sea = false;
    Game::TradeStop a;
    a.colony = 0;
    a.load = {kSugar, kFurs};
    Game::TradeStop b;
    b.colony = 1;
    b.unload = {kFurs, kSugar};
    r.stops = {a, b};
    return r;
}

std::string firstLine(const PlayScreen& play) {
    return play.dialog() && !play.dialog()->lines.empty() ? play.dialog()->lines.front() : "";
}

} // namespace

VCR_TEST("trade routes: kept in the save as the original keeps them") {
    Game game(routeSave(), Ruleset{});
    VCR_CHECK(game.tradeRouteCount() == 0);
    auto r = furRun();
    r.stops[1].colony = Game::kEuropeStop;
    VCR_CHECK(game.addTradeRoute(r) == 0);
    VCR_CHECK(game.tradeRouteCount() == 1);
    // Measured: the count is the header's word at 0x30, the routes the
    // last 888 bytes; a stop's goods are nibbles, the low one first.
    const auto& save = game.state();
    VCR_CHECK(save.header[0x30] == 1);
    const auto& tail = save.tail;
    const std::size_t at = tail.size() - 888;
    VCR_CHECK(std::string(reinterpret_cast<const char*>(&tail[at])) == "Fur Run");
    VCR_CHECK(tail[at + 32] == 0); // land
    VCR_CHECK(tail[at + 33] == 2); // stops
    VCR_CHECK(tail[at + 34] == 0 && tail[at + 35] == 0);
    VCR_CHECK(tail[at + 36] == 0x20);                          // two to load, none to unload
    VCR_CHECK(tail[at + 37] == 0x41);                          // sugar, then furs
    VCR_CHECK(tail[at + 44] == 0xE7 && tail[at + 45] == 0x03); // Europe: 999
    VCR_CHECK(tail[at + 46] == 0x02);
    VCR_CHECK(tail[at + 50] == 0x14); // furs, then sugar

    const auto back = game.tradeRoute(0);
    VCR_CHECK(back.name == "Fur Run" && !back.sea && back.stops.size() == 2);
    VCR_CHECK(back.stops[0].load.size() == 2 && back.stops[0].load[1] == kFurs);
    VCR_CHECK(back.stops[1].colony == Game::kEuropeStop && back.stops[1].unload[0] == kFurs);
}

VCR_TEST("original files: deleting a trade route moves the later routes up") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    auto save = routeSave();
    save.units.push_back(save.units[0]); // 1: a second wagon train
    Game game(save, art.rules);
    game.addTradeRoute(furRun());
    auto second = furRun();
    second.name = "Second";
    game.addTradeRoute(second);
    game.beginTradeRoute(0, 0, 1);
    game.beginTradeRoute(1, 1, 1);
    game.deleteTradeRoute(0);
    VCR_CHECK(game.tradeRouteCount() == 1);
    VCR_CHECK(game.tradeRoute(0).name == "Second");
    // Read from the original: those on the deleted route lose it and their
    // orders; the others follow their route's new number.
    VCR_CHECK(game.unit(0).orders == SaveUnit::kNoOrders);
    VCR_CHECK(game.unitTradeRoute(0) == 0 && game.unitTradeStop(0) == 0);
    VCR_CHECK(game.unit(1).orders == Game::kTradeRouteOrders);
    VCR_CHECK(game.unitTradeRoute(1) == 0 && game.unitTradeStop(1) == 1);
}

VCR_TEST("original files: a wagon train follows its route, loading and unloading") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    Game game(routeSave(), art.rules);
    game.addTradeRoute(furRun());
    game.beginTradeRoute(kWagon, 0, 0);
    game.moveTradeRouteUnits();
    // At Haven: the most valuable stock first (the price times the stock),
    // a hold at a time until the holds are full.
    const auto& w = game.unit(kWagon);
    VCR_CHECK(w.holds == 2);
    VCR_CHECK(holdGood(w, 0) == kFurs && holdAmount(w, 0) == 100);
    VCR_CHECK(holdGood(w, 1) == kFurs && holdAmount(w, 1) == 50);
    VCR_CHECK(game.state().colonies[0].stock[kFurs] == 0);
    VCR_CHECK(game.state().colonies[0].stock[kSugar] == 30);
    VCR_CHECK(game.unitTradeStop(kWagon) == 1);
    VCR_CHECK(w.x > 3); // and on its way
    for (int turn = 0; turn < 3 && game.unitTradeStop(kWagon) == 1; ++turn) {
        game.state().units[kWagon].movesUsed = 0;
        game.moveTradeRouteUnits();
    }
    // At Upland everything is unloaded, and it heads back with the moves
    // it has left.
    VCR_CHECK(game.unit(kWagon).holds == 0);
    VCR_CHECK(game.state().colonies[1].stock[kFurs] == 150);
    VCR_CHECK(game.unitTradeStop(kWagon) == 0);
    VCR_CHECK(game.unit(kWagon).x == 5);
    VCR_CHECK(game.unit(kWagon).orders == Game::kTradeRouteOrders);
}

VCR_TEST("original files: a route with one port says so") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    Game game(routeSave(), art.rules);
    auto r = furRun();
    r.stops[1].colony = 0;
    game.addTradeRoute(r);
    game.beginTradeRoute(kWagon, 0, 0);
    game.moveTradeRouteUnits();
    bool loop = false;
    for (const auto& n : game.takeNotices()) {
        loop = loop || (n.section == "ROUTELOOP" && n.strings.at(0) == "Fur Run");
    }
    VCR_CHECK(loop);
    VCR_CHECK(game.movesLeft(kWagon) == 0);
}

VCR_TEST("original files: creating a trade route from the TRADE menu") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    PlayScreen play(Game(routeSave(), art.rules), dir / "unused.sav");
    play.setArt(&art);

    play.editTradeRoute();
    VCR_CHECK(firstLine(play).find("You have not yet defined any trade") == 0);
    play.handle(Command::Confirm);

    // Seen in the original: the first destination, then (for a colony by
    // the water) the kind of route, the name, and the second destination.
    play.createTradeRoute();
    VCR_CHECK(firstLine(play).find("Select destination number 1") == 0);
    VCR_CHECK(play.dialog()->options.size() == 2);
    play.handle(Command::Confirm); // Haven
    VCR_CHECK(firstLine(play).find("Is this a") == 0);
    play.handle(Command::Confirm); // Sea route
    VCR_CHECK(play.dialog()->input);
    VCR_CHECK(play.dialog()->inputText.rfind("Haven ", 0) == 0);
    play.handle(Command::Confirm);
    VCR_CHECK(firstLine(play).find("Select destination number 2") == 0);
    VCR_CHECK(play.dialog()->options.size() == 1); // Upland lies inland
    play.handle(Command::Confirm);
    VCR_CHECK(play.routeScreen() == 0);
    const auto made = play.game().tradeRoute(0);
    VCR_CHECK(made.sea && made.stops.size() == 2 && made.stops[0].colony == 0 && made.stops[1].colony == 0);

    // On the screen: a third stop in Europe, furs to unload there, tools to
    // load at the first stop and taken off again.
    play.clickRouteScreen(40, 110); // the third row
    VCR_CHECK(firstLine(play).find("Select a port to sail to") == 0);
    VCR_CHECK(play.dialog()->selected == 1);
    play.handle(Command::Up);
    play.handle(Command::Confirm); // Amsterdam
    VCR_CHECK(play.game().tradeRoute(0).stops.size() == 3);
    VCR_CHECK(play.game().tradeRoute(0).stops[2].colony == Game::kEuropeStop);
    play.clickRouteScreen(150, 110);
    VCR_CHECK(firstLine(play).find("Select a cargo to") == 0);
    VCR_CHECK(play.dialog()->options.size() == 16);
    for (int k = 0; k < kFurs; ++k) {
        play.handle(Command::Down);
    }
    play.handle(Command::Confirm);
    VCR_CHECK(play.game().tradeRoute(0).stops[2].unload.size() == 1);
    play.clickRouteScreen(250, 70);
    VCR_CHECK(firstLine(play).find("Select a cargo to") == 0);
    for (int k = 0; k < kTools; ++k) {
        play.handle(Command::Down);
    }
    play.handle(Command::Confirm);
    VCR_CHECK(play.game().tradeRoute(0).stops[0].load.size() == 1);
    VCR_CHECK(play.game().tradeRoute(0).stops[0].load[0] == kTools);
    play.clickRouteScreen(210, 70); // the icon
    VCR_CHECK(play.game().tradeRoute(0).stops[0].load.empty());
    // (Delete Destination) ends the list for a stop already set.
    play.clickRouteScreen(40, 90);
    VCR_CHECK(play.dialog()->options.back() == "(Delete Destination)");
    play.handle(Command::Down);
    play.handle(Command::Confirm);
    VCR_CHECK(play.game().tradeRoute(0).stops.size() == 2);
    VCR_CHECK(play.game().tradeRoute(0).stops[1].colony == Game::kEuropeStop);
    play.clickRouteScreen(290, 180); // OK
    VCR_CHECK(play.routeScreen() == -1);

    // Delete Trade Route asks first.
    play.deleteTradeRoute();
    VCR_CHECK(play.dialog()->options.at(0) == "1. " + made.name);
    play.handle(Command::Confirm);
    VCR_CHECK(firstLine(play).find("Are you sure you want to delete") == 0);
    play.handle(Command::Cancel); // No
    VCR_CHECK(play.game().tradeRouteCount() == 1);
}

VCR_TEST("original files: Begin Trade Route lists the routes of the carrier's kind") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    Game game(routeSave(), art.rules);
    auto sea = furRun();
    sea.name = "Ferry";
    sea.sea = true;
    game.addTradeRoute(sea);
    game.setActiveUnit(kWagon);
    PlayScreen play(std::move(game), dir / "unused.sav");
    play.setArt(&art);
    play.handle(Command::Train); // T
    VCR_CHECK(firstLine(play).find("You have not yet defined any") == 0);
    VCR_CHECK(firstLine(play).find("Land") != std::string::npos);
    play.handle(Command::Confirm);

    play.game().addTradeRoute(furRun());
    play.handle(Command::Train);
    VCR_CHECK(firstLine(play).find("Select a trade route") == 0);
    VCR_CHECK(play.dialog()->options.size() == 1);
    VCR_CHECK(play.dialog()->options[0] == "2. Fur Run");
    play.handle(Command::Confirm);
    VCR_CHECK(firstLine(play).find("Select a port to sail to") == 0);
    VCR_CHECK(play.dialog()->options.size() == 2);
    play.handle(Command::Confirm); // Haven, where it stands
    VCR_CHECK(play.game().unit(kWagon).orders == Game::kTradeRouteOrders);
    VCR_CHECK(play.game().unitTradeRoute(kWagon) == 1);
    VCR_CHECK(play.game().unit(kWagon).holds == 2); // loaded and on its way
}

VCR_TEST("original files: the EDIT TRADE ROUTE screen's parts") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    Game game(routeSave(), art.rules);
    game.addTradeRoute(furRun());
    using Part = RouteScreenHit::Part;
    VCR_CHECK(routeScreenHit(art, game, 0, 20, 70).part == Part::Destination);
    VCR_CHECK(routeScreenHit(art, game, 0, 20, 70).row == 0);
    VCR_CHECK(routeScreenHit(art, game, 0, 20, 125).row == 3);
    // The load icons stand two pixels apart, each as wide as it is: sugar
    // (8 wide) at 208, furs at 218.
    VCR_CHECK(routeScreenHit(art, game, 0, 209, 70).good == 0);
    VCR_CHECK(routeScreenHit(art, game, 0, 217, 70).good == -1);
    VCR_CHECK(routeScreenHit(art, game, 0, 219, 70).good == 1);
    VCR_CHECK(routeScreenHit(art, game, 0, 290, 180).part == Part::Ok);
    VCR_CHECK(routeStopName(art, game, Game::kEuropeStop) == "Amsterdam");

    Screen screen;
    drawTradeRouteScreen(screen, art, game, 0);
    VCR_CHECK(screen.at(5, 61) == 0 && screen.at(115, 70) == 0 && screen.at(280, 170) == 0);
    VCR_CHECK(screen.at(5, 5) == 0x22);
}
