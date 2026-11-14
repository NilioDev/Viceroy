// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/game/game.hpp"

using namespace viceroy;

namespace {

constexpr int kDutch = 3;

Ruleset foundRules() {
    Ruleset r;
    r.terrain.resize(27);
    r.units.resize(14);
    r.units[unit_type::kColonist] = {"Colonists", 101, 1, 0, 1, 0, 1, 1, 0, 0, 0, 0};
    return r;
}

// A 9 x 9 land of plains with a colonist in the middle; the east column is
// the open sea.
SaveGame foundSave(int terrain = 2) {
    SaveGame s;
    s.humanNation = kDutch;
    s.difficulty = 0;
    s.map.width = 9;
    s.map.height = 9;
    s.map.squares.resize(81);
    for (int y = 0; y < 9; ++y) {
        for (int x = 0; x < 9; ++x) {
            auto& sq = s.map.at(x, y);
            sq.terrain = static_cast<std::uint8_t>(x == 8 ? 25 : terrain);
            sq.region = x == 8 ? 1 : 2;
        }
    }
    for (int n = 0; n < 4; ++n) {
        s.players[static_cast<std::size_t>(n)].control = n == kDutch ? 0 : 2;
    }
    SaveUnit u;
    u.x = 4;
    u.y = 4;
    u.type = unit_type::kColonist;
    u.nation = kDutch;
    s.units.push_back(u);
    return s;
}

} // namespace

VCR_TEST("founding: what keeps a colony from being founded (read from the original)") {
    {
        Game g(foundSave(), foundRules());
        VCR_CHECK(g.canFoundColony(0) == FoundResult::Founded);
        // A colony next door: too near, and which one is named.
        SaveColony c;
        c.x = 5;
        c.y = 5;
        c.nation = 0;
        c.name = "Jamestown";
        g.state().colonies.push_back(c);
        VCR_CHECK(g.canFoundColony(0) == FoundResult::TooClose);
        VCR_CHECK(g.colonyNextTo(0) == 0);
        g.state().colonies[0].x = 6; // two squares away is fine
        VCR_CHECK(g.canFoundColony(0) == FoundResult::Founded);
        // A native settlement next door does not stop it.
        SaveSettlement v;
        v.x = 3;
        v.y = 3;
        v.nation = 5;
        g.state().settlements.push_back(v);
        VCR_CHECK(g.canFoundColony(0) == FoundResult::Founded);
        // 38 colonies of the nation's own are the most.
        for (int k = 0; k < 38; ++k) {
            SaveColony own;
            own.x = 0;
            own.y = 8;
            own.nation = kDutch;
            g.state().colonies.push_back(own);
        }
        VCR_CHECK(g.canFoundColony(0) == FoundResult::TooMany);
    }
    {
        Game g(foundSave(), foundRules());
        g.state().map.at(4, 4).relief = 5; // mountains
        VCR_CHECK(g.canFoundColony(0) == FoundResult::Mountains);
        g.state().map.at(4, 4).relief = 0;
        g.state().map.at(4, 4).terrain = 25;
        VCR_CHECK(g.canFoundColony(0) == FoundResult::AtSea);
    }
}

VCR_TEST("founding: the advisors' warnings (read from the original)") {
    {
        // On plains by the sea, four or more productive squares: nothing to
        // say but that there is no forest.
        SaveGame s = foundSave();
        s.units[0].x = 7;
        Game g(s, foundRules());
        const auto a = g.foundAdvice(0);
        VCR_CHECK(!a.noPort && !a.fewSquares && a.noForest);
    }
    {
        // Inland in the forest: no way to the ocean.
        Game g(foundSave(10), foundRules());
        const auto a = g.foundAdvice(0);
        VCR_CHECK(a.noPort && !a.fewSquares && !a.noForest);
    }
    {
        // Desert all round: few productive squares; from the third level on
        // the tutorial warnings are not given.
        Game g(foundSave(1), foundRules());
        VCR_CHECK(g.foundAdvice(0).fewSquares);
        g.state().difficulty = 2;
        const auto a = g.foundAdvice(0);
        VCR_CHECK(!a.fewSquares && !a.noForest && a.noPort);
    }
}

#include "viceroy/game_files.hpp"
#include "viceroy/render/game_view.hpp"
#include "viceroy/ui/play.hpp"

VCR_TEST("original files: Build Colony warns, then asks the name") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    Game game(foundSave(10), art.rules); // inland, in the forest
    game.setActiveUnit(0);
    PlayScreen play(std::move(game), dir / "unused.sav");
    play.setArt(&art);
    play.setColonyNames(&art.colonyNames);
    play.handle(Command::BuildColony);
    // "This square does not have access to the ocean": the first answer
    // gives up.
    VCR_CHECK(play.dialog().has_value() && play.dialog()->options.size() == 2);
    play.handle(Command::Confirm);
    VCR_CHECK(!play.dialog().has_value() && play.game().state().colonies.empty());
    // Again, and build anyway: the name is asked, the list's first offered.
    play.handle(Command::BuildColony);
    play.handle(Command::Down);
    play.handle(Command::Confirm);
    VCR_CHECK(play.typing());
    VCR_CHECK(play.dialog()->inputText == "New Amsterdam" && play.dialog()->inputLabel == "Name:");
    // A key replaces the offer; Backspace takes one back.
    play.type('B');
    play.type('o');
    play.type('x');
    play.eraseTyped();
    VCR_CHECK(play.dialog()->inputText == "Bo");
    play.handle(Command::Confirm);
    VCR_CHECK(play.game().state().colonies.size() == 1 && play.game().state().colonies[0].name == "Bo");
}

VCR_TEST("original files: Escape at the name founds nothing") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    SaveGame s = foundSave();
    s.units[0].x = 7;
    s.difficulty = 2;
    Game game(s, art.rules);
    game.setActiveUnit(0);
    PlayScreen play(std::move(game), dir / "unused.sav");
    play.setArt(&art);
    play.setColonyNames(&art.colonyNames);
    play.handle(Command::BuildColony);
    VCR_CHECK(play.typing());
    play.handle(Command::Cancel);
    VCR_CHECK(!play.dialog().has_value() && play.game().state().colonies.empty());
    // The name was used up all the same: the next offer is the second.
    play.handle(Command::BuildColony);
    VCR_CHECK(play.typing() && play.dialog()->inputText == "Fort Orange");
}
