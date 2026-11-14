// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

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
