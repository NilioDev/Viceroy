// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/game/colony.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/ui/play.hpp"

#include <filesystem>
#include <string>

using namespace viceroy;

namespace {

// A 12×8 world: ocean in the first six columns, plains east of them; an
// English caravel with a pioneer and a soldier aboard at (2, 3). Turn 0 of
// a Discoverer game with Tutorial Hints on.
SaveGame tutorialSave() {
    SaveGame s;
    s.year = 1492;
    s.humanNation = 0;
    s.map.width = 12;
    s.map.height = 8;
    s.map.squares.resize(96);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 12; ++x) {
            auto& sq = s.map.at(x, y);
            sq.terrain = x < 6 ? 25 : 2;
            sq.region = x < 6 ? 1 : 2;
        }
    }
    s.header[18] = 0x80;
    s.header[22] = 0x0E;
    auto unit = [&](int x, int y, int type, int orders) {
        SaveUnit u;
        u.x = x;
        u.y = y;
        u.type = type;
        u.nation = 0;
        u.orders = orders;
        u.profession = 0x1C;
        s.units.push_back(u);
    };
    unit(2, 3, unit_type::kCaravel, SaveUnit::kNoOrders); // 0
    unit(2, 3, unit_type::kPioneer, SaveUnit::kSentry);   // 1
    unit(2, 3, unit_type::kSoldier, SaveUnit::kSentry);   // 2
    for (auto& p : s.players) {
        p.control = 1;
    }
    s.players[0].control = 0;
    s.players[0].country = "New England";
    return s;
}

} // namespace

VCR_TEST("original files: the first hints of a Discoverer game, once each") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    Game g(tutorialSave(), art.rules);
    // The ship first: how to sail (turn 0, Discoverer only), then what
    // ships are for.
    auto hint = g.unitHint(0);
    VCR_CHECK(hint && hint->section == "TUTORIAL1" && hint->value == 0);
    VCR_CHECK(hint && hint->strings == std::vector<std::string>{"Caravel"});
    VCR_CHECK(g.state().header[22] == 0x1E);
    VCR_CHECK(!g.unitHint(0)); // people aboard
    g.state().units.resize(1);
    hint = g.unitHint(0);
    VCR_CHECK(hint && hint->section == "TUTORIAL11" && hint->value == 0);
    VCR_CHECK(hint && hint->strings.size() == 2 && hint->strings[0] == "Caravel");
    VCR_CHECK(hint && hint->ports == std::vector<std::pair<int, int>>{{1, 0}});
    VCR_CHECK(!g.unitHint(0));
    // Off with the option.
    Game off(tutorialSave(), art.rules);
    off.state().header[18] = 0;
    VCR_CHECK(!off.unitHint(0));
    // Explorer: no sailing lesson.
    auto explorerSave = tutorialSave();
    explorerSave.difficulty = 1;
    explorerSave.units.resize(1);
    Game explorer(explorerSave, art.rules);
    hint = explorer.unitHint(0);
    VCR_CHECK(hint && hint->section == "TUTORIAL11");
}

VCR_TEST("original files: next to land the player names it, then learns to go ashore") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    Game g(tutorialSave(), art.rules);
    g.unitHint(0);
    VCR_CHECK(g.move(0, {1, 0}) == MoveResult::Moved); // (3, 3): no land around
    VCR_CHECK(g.takeNotices().empty() && !g.landSighted());
    VCR_CHECK(g.move(0, {1, 0}) == MoveResult::Moved); // (4, 3)
    VCR_CHECK(g.takeNotices().empty());
    VCR_CHECK(g.move(0, {1, 0}) == MoveResult::Moved); // (5, 3): land at (6, 3)
    // The woodcut of the discovery, then the naming.
    auto notices = g.takeNotices();
    VCR_CHECK(notices.size() == 2 && notices[0].section == "WOODCUT" && notices[0].value == 1);
    VCR_CHECK(notices.size() == 2 && notices[1].section == "LANDHO");
    VCR_CHECK(g.landSighted() && (g.state().players[0].raw[48] & 0x80) != 0);
    g.setCountryName("Nieuw Holland");
    VCR_CHECK(g.state().players[0].country == "Nieuw Holland");
    VCR_CHECK(g.move(0, {0, 1}) == MoveResult::Moved);
    VCR_CHECK(g.takeNotices().empty()); // once a game

    // The play screen asks for the name, filled in with the old one, and
    // the naval adviser follows with how to go ashore.
    Game fresh(tutorialSave(), art.rules);
    fresh.unitHint(0);
    fresh.state().units[0].x = 4;
    fresh.state().units[1].x = 4;
    fresh.state().units[2].x = 4;
    fresh.setActiveUnit(0);
    PlayScreen play(std::move(fresh), dir / "unused.sav");
    play.setArt(&art);
    play.setAnimations(false);
    play.handle(Command::MoveE);
    VCR_CHECK(play.dialog() && play.dialog()->woodcut == 1);
    play.handle(Command::Confirm);
    VCR_CHECK(play.dialog() && play.dialog()->lines.front().find("Land Ho!") == 0);
    VCR_CHECK(play.dialog() && play.dialog()->inputText == "New England");
    play.handle(Command::Confirm);
    VCR_CHECK(play.dialog() && play.dialog()->lines.front().find("We've found uncharted land") == 0);
}

VCR_TEST("original files: pioneers and soldiers ashore get their lessons") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    auto save = tutorialSave();
    save.turn = 3;
    save.units.resize(1);
    save.units.push_back(save.units[0]);
    save.units[1].type = unit_type::kPioneer;
    save.units[1].x = 7;
    save.units.push_back(save.units[1]);
    save.units[2].type = unit_type::kSoldier;
    Game g(save, art.rules);
    auto hint = g.unitHint(1);
    VCR_CHECK(hint && hint->section == "TUTORIAL13" && hint->value == 3);
    hint = g.unitHint(2);
    VCR_CHECK(hint && hint->section == "TUTORIAL14" && hint->value == 1);
    // Late in the game: none of these.
    save.turn = 20;
    Game late(save, art.rules);
    VCR_CHECK(!late.unitHint(2));
}
