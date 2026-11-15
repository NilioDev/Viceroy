// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/game/colony.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/ui/play.hpp"

#include <filesystem>
#include <string>

using namespace viceroy;

namespace {

// An 8×6 world: ocean in the first three columns, an inland lake at (3, 4)
// next to it, plains elsewhere (with a river at (3, 1)); a Dutch caravel
// with a colonist aboard at (2, 2).
SaveGame shipSave() {
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
    s.map.at(3, 4).terrain = 25; // the lake
    s.map.at(3, 4).region = 3;
    s.map.at(3, 1).relief = 2; // a river
    auto unit = [&](int x, int y, int type, int nation, int orders) {
        SaveUnit u;
        u.x = x;
        u.y = y;
        u.type = type;
        u.nation = nation;
        u.orders = orders;
        s.units.push_back(u);
    };
    unit(2, 2, unit_type::kCaravel, 3, SaveUnit::kNoOrders); // 0
    unit(2, 2, unit_type::kColonist, 3, SaveUnit::kSentry);  // 1: aboard
    // The new land has its name already (no "Land Ho!").
    for (auto& player : s.players) {
        player.raw[48] = 0x80;
    }
    return s;
}

std::string firstLine(const PlayScreen& play) {
    return play.dialog() && !play.dialog()->lines.empty() ? play.dialog()->lines.front() : "";
}

} // namespace

VCR_TEST("original files: a ship steering onto land asks to make landfall") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    Game game(shipSave(), art.rules);
    game.setActiveUnit(0);
    PlayScreen play(std::move(game), dir / "unused.sav");
    play.setArt(&art);

    play.handle(Command::MoveE); // plains
    VCR_CHECK(play.dialog().has_value());
    VCR_CHECK(firstLine(play).find("Shall we make landfall") == 0);
    VCR_CHECK(play.dialog()->options.size() == 2);
    play.handle(Command::Cancel); // Escape stays with the ships
    VCR_CHECK(play.game().unit(1).x == 2);

    play.handle(Command::MoveNE); // a river: "Our ships cannot navigate up this river"
    VCR_CHECK(firstLine(play).find("Our ships cannot navigate") == 0);
    VCR_CHECK(play.dialog()->selected == 0); // "Stay With Ships" is the default
    play.handle(Command::Down);
    play.handle(Command::Confirm); // "Make Landfall"
    VCR_CHECK(!play.dialog().has_value());
    VCR_CHECK(play.game().unit(1).x == 3 && play.game().unit(1).y == 1);
    VCR_CHECK(play.game().unit(0).x == 2); // the ship stays
}

VCR_TEST("original files: ships keep off lakes and only warships attack ships") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    auto save = shipSave();
    save.units[0].y = 3; // next to the lake
    save.units[1].y = 3;
    SaveUnit french;
    french.x = 2;
    french.y = 4;
    french.type = unit_type::kCaravel;
    french.nation = 1;
    save.units.push_back(french);
    Game game(save, art.rules);
    VCR_CHECK(game.move(0, {1, 1}) == MoveResult::Blocked); // the lake
    game.setActiveUnit(0);
    PlayScreen play(std::move(game), dir / "unused.sav");
    play.setArt(&art);

    play.handle(Command::MoveSE);
    VCR_CHECK(firstLine(play).find("Ship units cannot enter inland") == 0);
    play.handle(Command::Confirm);
    play.handle(Command::MoveS); // the French caravel
    VCR_CHECK(firstLine(play).find("Only") == 0);
}

VCR_TEST("original files: land units cannot attack from on board a ship") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    auto save = shipSave();
    SaveUnit french;
    french.x = 3;
    french.y = 2;
    french.type = unit_type::kColonist;
    french.nation = 1;
    save.units.push_back(french);
    save.units[1].orders = SaveUnit::kNoOrders;
    Game game(save, art.rules);
    game.setActiveUnit(1);
    PlayScreen play(std::move(game), dir / "unused.sav");
    play.setArt(&art);
    play.handle(Command::MoveE);
    VCR_CHECK(firstLine(play).find("Land units cannot enter") == 0);
    VCR_CHECK(play.game().unit(1).x == 2);
}

VCR_TEST("original files: enemy warships hold up a passing ship or it slips past") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    int slowed = 0;
    int slipped = 0;
    for (int turn = 0; turn < 40; ++turn) {
        auto save = shipSave();
        save.turn = turn;
        save.units.pop_back(); // no colonist aboard
        SaveUnit frigate;
        frigate.x = 0;
        frigate.y = 4;
        frigate.type = 17;
        frigate.nation = 1;
        save.units.push_back(frigate);
        Game game(save, art.rules);
        VCR_CHECK(game.move(0, {-1, 1}) == MoveResult::Moved); // to (1, 3), next to the frigate
        const int used = game.unit(0).movesUsed;
        for (const auto& n : game.takeNotices()) {
            if (n.section == "SHIPSLOW") {
                ++slowed;
                VCR_CHECK(used == 3 + 6 || used == 3 + 3);
            } else if (n.section == "SHIPRUN") {
                ++slipped;
                VCR_CHECK(used == 3);
            }
        }
    }
    VCR_CHECK(slowed > 0 && slipped > 0);
    VCR_CHECK(slowed + slipped == 40);
}

VCR_TEST("original files: disbanding a unit, and a ship with people aboard") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    Game game(shipSave(), art.rules);
    game.setActiveUnit(0);
    PlayScreen play(std::move(game), dir / "unused.sav");
    play.setArt(&art);

    play.handle(Command::Disband); // the caravel carries a colonist
    VCR_CHECK(firstLine(play).find("We cannot disband a ship") == 0);
    play.handle(Command::Confirm);
    VCR_CHECK(play.game().unitCount() == 2);

    play.game().setActiveUnit(1);
    play.handle(Command::Disband);
    VCR_CHECK(firstLine(play).find("Really {disband} Colonists?") == 0);
    play.handle(Command::Confirm); // "Yes"
    VCR_CHECK(play.game().unitCount() == 1);
    VCR_CHECK(play.game().unit(0).type == unit_type::kCaravel);
}

VCR_TEST("original files: dumping cargo overboard") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    auto save = shipSave();
    save.units[0].holds = 2;
    setHold(save.units[0], 0, 15, 100); // muskets
    setHold(save.units[0], 1, 14, 30);  // tools
    Game game(save, art.rules);
    game.setActiveUnit(0);
    PlayScreen play(std::move(game), dir / "unused.sav");
    play.setArt(&art);

    play.handle(Command::Dump);
    VCR_CHECK(firstLine(play).find("What cargo shall we throw overboard") == 0);
    VCR_CHECK(play.dialog()->options.size() == 3);
    VCR_CHECK(play.dialog()->options[0] == "Nothing");
    VCR_CHECK(play.dialog()->options[1] == "100 Muskets");
    play.handle(Command::Down);
    play.handle(Command::Confirm);
    VCR_CHECK(play.game().unit(0).holds == 1);
    VCR_CHECK(holdGood(play.game().unit(0), 0) == 14 && holdAmount(play.game().unit(0), 0) == 30);
}

namespace {

// A colony at (3, 2) on the coast of shipSave's world, with a galleon in
// it; a caravel too, and a treasure of 3000 and a colonist outside.
Ruleset boardingRules() {
    Ruleset r;
    r.terrain.resize(26);
    for (auto& t : r.terrain) {
        t.movementCost = 1;
    }
    r.units.resize(19);
    r.units[unit_type::kColonist] = {"Colonists", 101, 1, 0, 1, 0, 1, 1, 0, 0, 0, 0};
    r.units[unit_type::kTreasure] = {"Treasure", 17, 1, 0, 0, 0, 6, 4, 0, 0, 0, 0};
    r.units[unit_type::kCaravel] = {"Caravel", 6, 4, 0, 2, 2, 99, 4, 4, 0, 4, 0};
    r.units[15] = {"Galleon", 8, 6, 0, 10, 6, 99, 10, 10, 4, 20, 0};
    r.cargo.resize(16);
    return r;
}

SaveGame boardingSave() {
    SaveGame s = shipSave();
    s.units.clear();
    SaveColony c;
    c.x = 3;
    c.y = 2;
    c.nation = 3;
    c.population = 1;
    c.name = "Fort Oranje";
    s.colonies.push_back(c);
    s.map.at(3, 2).flags |= MapSquare::kHasColony;
    s.map.at(3, 2).region = static_cast<std::uint8_t>(3 << 4 | 2);
    auto unit = [&](int type) {
        SaveUnit u;
        u.x = 3;
        u.y = 2;
        u.type = type;
        u.nation = 3;
        u.orders = SaveUnit::kNoOrders;
        s.units.push_back(u);
    };
    unit(15);                   // 0: galleon
    unit(unit_type::kCaravel);  // 1
    unit(unit_type::kTreasure); // 2
    s.units[2].profession = 30; // 3000
    unit(unit_type::kColonist); // 3
    s.nations[3].taxRate = 60;
    return s;
}

} // namespace

VCR_TEST("ships: units in a colony go aboard by their size and sail with the ship") {
    Game g(boardingSave(), boardingRules());
    // A treasure fills six holds: never a caravel, and only an empty galleon.
    VCR_CHECK(!g.boardInColony(2, 1));
    VCR_CHECK(g.boardInColony(3, 0));
    VCR_CHECK(g.freeHolds(0) == 5);
    VCR_CHECK(!g.boardInColony(2, 0));
    g.leaveShipInColony(3);
    VCR_CHECK(g.passengersInColony(0).empty() && g.unit(3).orders == SaveUnit::kNoOrders);
    VCR_CHECK(g.boardInColony(2, 0));
    VCR_CHECK(g.freeHolds(0) == 0 && !g.boardInColony(3, 0));
    VCR_CHECK(g.boardInColony(3, 1));
    // Out of the colony, those aboard come along; the colonist stays.
    VCR_CHECK(g.move(0, {-1, 0}) == MoveResult::Moved);
    VCR_CHECK(g.unit(2).x == 2 && g.unit(3).x == 3);
    VCR_CHECK(g.passengers(0) == std::vector<int>{2});
    VCR_CHECK(g.move(1, {-1, 1}) == MoveResult::Moved);
    VCR_CHECK(g.unit(3).x == 2 && g.unit(3).y == 3);
}

VCR_TEST("ships: a galleon's treasure is sold in Europe, the Crown taking the tax up to half") {
    Game g(boardingSave(), boardingRules());
    VCR_CHECK(g.boardInColony(2, 0));
    VCR_CHECK(g.move(0, {-1, 0}) == MoveResult::Moved);
    VCR_CHECK(g.sailToEurope(0));
    const int gold = g.state().nations[3].gold;
    std::vector<GameNotice> notices;
    for (int i = 0; i < 4 && g.unitCount() == 4; ++i) {
        g.endTurn();
        for (auto& n : g.takeNotices()) {
            notices.push_back(std::move(n));
        }
    }
    VCR_CHECK(g.unitCount() == 3); // the treasure is gone
    VCR_CHECK(g.state().nations[3].gold == gold + 1500);
    bool told = false;
    for (const auto& n : notices) {
        if (n.section == "LOOTCASH") {
            told = n.numbers == std::vector<int>{3000, 50, 1500} && !n.music.empty() &&
                   n.music[0] == MusicCue{MusicCue::Kind::Pick, 0x24};
        }
    }
    VCR_CHECK(told);
}

VCR_TEST("ships: a fort fires on enemy ships beside the colony") {
    auto rules = boardingRules();
    rules.units[16] = {"Privateer", 11, 8, 8, 8, 2, 99, 20, 0, 0, 8, 0};
    rules.units[11] = {"Artillery", 10, 1, 7, 5, 0, 1, 6, 4, 0, 0, 0};
    rules.buildings.resize(3);
    rules.buildings[1].name = "Fort";
    rules.buildings[2].name = "Fortress";
    int fired = 0;
    int damaged = 0;
    for (int round = 0; round < 40; ++round) {
        SaveGame s = boardingSave();
        s.units.resize(1); // the galleon stays in port
        s.year = 1600 + round;
        setBuildingLevel(s.colonies[0], BuildingKind::Fortification, 3); // a fortress
        SaveUnit privateer;
        privateer.x = 2; // west of the colony, at sea
        privateer.y = 2;
        privateer.type = 16;
        privateer.nation = 1;
        s.units.push_back(privateer);
        SaveUnit caravel = privateer;
        caravel.y = 1; // north-west, a French caravel: the nations are at peace
        caravel.type = unit_type::kCaravel;
        s.units.push_back(caravel);
        s.nations[3].raw[52 + 1] |= 0x40;
        s.nations[1].raw[52 + 3] |= 0x40;
        Game g(s, rules);
        g.fortFire(0);
        for (const auto& n : g.takeNotices()) {
            if (n.section == "FORTFIRE") {
                ++fired;
                VCR_CHECK(n.strings[0] == "Fortress" && n.strings[1] == "Fort Oranje" && n.strings[3] == "Privateer");
            }
        }
        damaged += (g.unit(1).raw[4] & SaveUnit::kDamaged) != 0 ? 1 : 0;
        VCR_CHECK((g.unit(2).raw[4] & SaveUnit::kDamaged) == 0); // never the caravel
    }
    VCR_CHECK(fired == 40);
    // 8 against 8: about half the time.
    VCR_CHECK(damaged > 8 && damaged < 32);
}
