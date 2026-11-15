// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/game/europe.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/render/colony_view.hpp"
#include "viceroy/ui/play.hpp"

#include <filesystem>
#include <string>

using namespace viceroy;

namespace {

constexpr int kMuskets = 15;
constexpr int kTools = 14;
constexpr int kFrigate = 17; // row of @UNIT
constexpr int kShip = 1;     // the frigate

// A Dutch colony at (3, 2) on the coast of a small world, with a frigate in
// its harbour carrying 100 muskets and 40 tools, and a wagon train.
SaveGame harbourSave() {
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
    SaveColony c;
    c.x = 3;
    c.y = 2;
    c.name = "Haven";
    c.nation = 3;
    c.stock[kTools] = 150;
    c.stock[kMuskets] = 20;
    s.colonies.push_back(c);
    SaveUnit frigate;
    frigate.x = 3;
    frigate.y = 2;
    frigate.type = kFrigate;
    frigate.nation = 3;
    frigate.orders = SaveUnit::kSentry;
    frigate.holds = 2;
    setHold(frigate, 0, kMuskets, 100);
    setHold(frigate, 1, kTools, 40);
    SaveUnit wagon = frigate;
    wagon.type = unit_type::kWagonTrain;
    wagon.holds = 0;
    setHold(wagon, 0, 0, 0);
    setHold(wagon, 1, 0, 0);
    s.units.push_back(wagon);   // 0
    s.units.push_back(frigate); // 1: the last on the square comes first in the harbour
    return s;
}

std::string firstLine(const PlayScreen& play) {
    return play.dialog() && !play.dialog()->lines.empty() ? play.dialog()->lines.front() : "";
}

} // namespace

VCR_TEST("colony harbour: loading tops up the good's holds, then takes a new one") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    Game game(harbourSave(), art.rules);
    // Ships and wagon trains are in the harbour, not on the Units Present panel.
    const auto& colony = game.state().colonies[0];
    VCR_CHECK(shipsInPort(game, colony).size() == 2);
    VCR_CHECK(unitsPresent(game, colony).empty());

    // Four holds, two free: 200 of room.
    VCR_CHECK(game.colonyCargoRoom(kShip, kTools) == 200);
    VCR_CHECK(game.loadInColony(kShip, 0, kTools, 100) == 100); // 60 tops up, 40 in a new hold
    const auto& f = game.unit(kShip);
    VCR_CHECK(f.holds == 3);
    VCR_CHECK(holdAmount(f, 1) == 100 && holdGood(f, 2) == kTools && holdAmount(f, 2) == 40);
    VCR_CHECK(game.state().colonies[0].stock[kTools] == 50);
    VCR_CHECK(f.orders == SaveUnit::kNoOrders); // loading clears its orders

    // Only what the warehouse has: 20 muskets.
    VCR_CHECK(game.loadInColony(kShip, 0, kMuskets, 100) == 20);
    VCR_CHECK(holdAmount(game.unit(kShip), 0) == 100 && holdGood(game.unit(kShip), 3) == kMuskets);
    // All holds in use: only what the good's own holds still take.
    VCR_CHECK(game.colonyCargoRoom(kShip, kTools) == 60);
    VCR_CHECK(game.colonyCargoRoom(kShip, 0) == 0);

    // Unloading empties the first hold of the good; asked for less, the
    // rest goes back aboard.
    VCR_CHECK(game.colonyCargoHold(kShip, kTools) == 100);
    VCR_CHECK(game.unloadInColony(kShip, 0, kTools, 30) == 30);
    VCR_CHECK(game.state().colonies[0].stock[kTools] == 80);
    VCR_CHECK(game.colonyCargoHold(kShip, kTools) == 100); // 40 topped up by 60 of the 70
    int tools = 0;
    for (int k = 0; k < game.unit(kShip).holds; ++k) {
        tools += holdGood(game.unit(kShip), k) == kTools ? holdAmount(game.unit(kShip), k) : 0;
    }
    VCR_CHECK(tools == 110);
}

VCR_TEST("original files: goods are dragged between the warehouse and the harbour") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    PlayScreen play(Game(harbourSave(), art.rules), dir / "unused.sav");
    play.setArt(&art);
    play.openColony(0);

    // A hold dragged onto the warehouse: "Unloading Muskets" while dragged.
    VCR_CHECK(play.clickColonyScreen(130, 175));
    VCR_CHECK(play.colonyScreen()->topMessage == "Unloading Muskets");
    // 20 + 100 is past what the warehouse keeps: it warns first.
    VCR_CHECK(play.releaseColonyScreen(100, 190, false));
    VCR_CHECK(firstLine(play).find("Your Excellency, the warehouse at") == 0);
    play.handle(Command::Down);
    play.handle(Command::Confirm); // "Unload the Muskets anyway."
    VCR_CHECK(play.game().state().colonies[0].stock[kMuskets] == 120);
    VCR_CHECK(play.game().unit(kShip).holds == 1);
    VCR_CHECK(play.colonyScreen()->topMessage == "100 Muskets moved to Haven");

    // A good dragged from the warehouse onto the harbour loads 100.
    VCR_CHECK(play.clickColonyScreen(19 * kTools + 5, 190));
    VCR_CHECK(play.colonyScreen()->selectedGood == kTools);
    VCR_CHECK(play.colonyScreen()->topMessage == "Loading Tools");
    VCR_CHECK(play.releaseColonyScreen(190, 140, false));
    VCR_CHECK(play.game().state().colonies[0].stock[kTools] == 50);
    VCR_CHECK(play.colonyScreen()->topMessage == "100 Tools moved to Frigate");
    VCR_CHECK(play.colonyScreen()->harbourChosen);
    play.tick(2500);
    VCR_CHECK(play.colonyScreen()->topMessage.empty());

    // With Shift it asks how much.
    VCR_CHECK(play.clickColonyScreen(19 * kTools + 5, 190));
    VCR_CHECK(play.releaseColonyScreen(190, 140, true));
    VCR_CHECK(firstLine(play).find("How much") == 0);
    play.handle(Command::Cancel);
    VCR_CHECK(play.game().state().colonies[0].stock[kTools] == 50);

    // The ship already picked, clicked again: its options.
    VCR_CHECK(play.clickColonyScreen(135, 150));
    VCR_CHECK(play.releaseColonyScreen(135, 150, false));
    VCR_CHECK(play.dialog().has_value());
    // Not "Move to front." (the first), nor "Clear orders." (it has none).
    VCR_CHECK(play.dialog()->options.size() == 4);
    VCR_CHECK(play.dialog()->options.front().find("Sentry") == 0);
    play.handle(Command::Down);
    play.handle(Command::Down);
    play.handle(Command::Confirm); // "Unload all cargo."
    // Each hold in turn, each past what the warehouse keeps.
    for (int hold = 0; hold < 2; ++hold) {
        VCR_CHECK(firstLine(play).find("Your Excellency, the warehouse at") == 0);
        play.handle(Command::Down);
        play.handle(Command::Confirm);
    }
    VCR_CHECK(play.game().unit(kShip).holds == 0);
    VCR_CHECK(play.game().state().colonies[0].stock[kTools] == 190);
}

VCR_TEST("original files: cargo dragged from one carrier to another moves across") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    PlayScreen play(Game(harbourSave(), art.rules), dir / "unused.sav");
    play.setArt(&art);
    play.openColony(0);
    // The frigate's muskets onto the wagon train (the second in the harbour).
    VCR_CHECK(play.clickColonyScreen(130, 175));
    VCR_CHECK(play.releaseColonyScreen(150, 150, false));
    VCR_CHECK(play.game().unit(0).holds == 1 && holdGood(play.game().unit(0), 0) == kMuskets &&
              holdAmount(play.game().unit(0), 0) == 100);
    VCR_CHECK(play.game().unit(kShip).holds == 1 && holdGood(play.game().unit(kShip), 0) == kTools);
    VCR_CHECK(play.game().state().colonies[0].stock[kMuskets] == 20);
    // With Shift it asks how much (HOWMUCH3).
    VCR_CHECK(play.clickColonyScreen(130, 175));
    VCR_CHECK(play.releaseColonyScreen(150, 150, true));
    VCR_CHECK(firstLine(play).find("How much") == 0);
}
