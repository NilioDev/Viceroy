// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/game/game.hpp"
#include "viceroy/sound/background_music.hpp"

using namespace viceroy;

namespace {

Ruleset crossRules() {
    Ruleset r;
    r.terrain.resize(21);
    for (auto& t : r.terrain) {
        t.movementCost = 1;
    }
    r.units.resize(14);
    r.units[unit_type::kColonist] = {"Colonists", 101, 1, 0, 1, 0, 1, 1, 0, 0, 0, 0};
    return r;
}

// The Dutch with no colony, a ship-less map and crosses towards `needed`.
Game crossGame(int crosses, int needed) {
    SaveGame s;
    s.year = 1492;
    s.humanNation = 3;
    s.map.width = 3;
    s.map.height = 3;
    s.map.squares.resize(9);
    for (auto& sq : s.map.squares) {
        sq.terrain = 2;
    }
    auto& dutch = s.nations[3];
    dutch.crosses = crosses;
    dutch.crossesNeeded = needed;
    dutch.recruits = {0x1C, 0x1C, 0x1C};
    return Game(std::move(s), crossRules());
}

} // namespace

VCR_TEST("crosses: two a turn without a colony") {
    Game g = crossGame(0, 10);
    g.endTurn();
    VCR_CHECK(g.state().nations[3].crosses == 2);
    g.endTurn();
    VCR_CHECK(g.state().nations[3].crosses == 4);
    VCR_CHECK(g.unitCount() == 0);
    VCR_CHECK(g.takeNotices().empty());
}

VCR_TEST("crosses: more crosses than needed bring a colonist from the docks") {
    // Nobody at all: 2 × 0 + 8 = 8 crosses are needed, and 9 is more.
    Game g = crossGame(7, 9);
    g.endTurn();
    const auto& dutch = g.state().nations[3];
    VCR_CHECK(dutch.crosses == 0);
    VCR_CHECK(dutch.crossesNeeded == 8);
    VCR_CHECK(g.unitCount() == 1);
    VCR_CHECK((dutch.raw[0] & 0x40) != 0); // someone has come
    const auto notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices[0].section == "UNREST" && notices[0].value == 0x1C);
    // Exactly the number needed is not enough.
    Game h = crossGame(6, 0);
    h.endTurn();
    VCR_CHECK(h.unitCount() == 0 && h.state().nations[3].crosses == 8);
}

VCR_TEST("crosses: the number needed follows the nation's people, as in the original") {
    // Read from the original: 2 × (colonists in colonies + units) + 8; a
    // computer nation (8 - level) eighths of it, the English two thirds.
    Game g = crossGame(0, 0);
    auto& s = g.state();
    s.colonies.resize(1);
    s.colonies[0].nation = 3;
    s.colonies[0].population = 5;
    s.units.resize(4);
    for (auto& u : s.units) {
        u.nation = 3;
        u.x = 1;
        u.y = 1;
    }
    VCR_CHECK(g.crossesNeeded(3) == 2 * (5 + 4) + 8);
    s.players[3].control = 1;
    s.difficulty = 2;
    VCR_CHECK(g.crossesNeeded(3) == 26 * 6 / 8);
    s.players[0].control = 1;
    for (auto& u : s.units) {
        u.nation = 0;
    }
    s.colonies[0].nation = 0;
    VCR_CHECK(g.crossesNeeded(0) == 26 * 6 / 8 * 2 / 3);
    // The saves of the original agree: 98 colonists and 40 units need 284.
}

VCR_TEST("crosses: people waiting on the docks hold immigration back") {
    // Once someone has come, each person in port takes 2 crosses a turn
    // instead of the 2 the nation gathers.
    Game g = crossGame(10, 0);
    g.endTurn(); // 12 > 8: a colonist comes and waits on the docks
    VCR_CHECK(g.unitCount() == 1 && g.state().nations[3].crosses == 0);
    g.state().nations[3].crosses = 5;
    g.endTurn();
    VCR_CHECK(g.state().nations[3].crosses == 3);
    g.endTurn();
    g.endTurn();
    VCR_CHECK(g.state().nations[3].crosses == 0); // not below nothing
    VCR_CHECK(g.unitCount() == 1);
}

VCR_TEST("crosses: computer nations get newcomers too") {
    Game g = crossGame(0, 0);
    auto& s = g.state();
    s.players[1].control = 1;
    s.nations[1].crosses = 20;
    s.nations[1].recruits = {0x1C, 0x1C, 0x1C};
    g.immigration(1);
    VCR_CHECK(g.unitCount() == 1 && g.unit(0).nation == 1);
    VCR_CHECK(s.nations[1].crosses == 0 && s.nations[1].crossesNeeded == 8);
    VCR_CHECK(g.takeNotices().empty()); // the player hears nothing of it
}

VCR_TEST("docks: who comes next follows the original's draw") {
    // Experts come from the shift register in byte 68 of the nation record;
    // professions 1-4, 18, 19, 23 and 25 up are never drawn.
    Game g = crossGame(0, 100);
    auto& dutch = g.state().nations[3];
    dutch.raw[68] = 0x13;
    dutch.raw[69] = 5;
    int experts = 0;
    for (int i = 0; i < 300; ++i) {
        dutch.gold = 100000;
        dutch.recruitCount = 0;
        g.state().units.clear(); // a nation may have 201 units at most
        const int before = dutch.raw[68];
        VCR_CHECK(g.recruit(0) >= 0);
        const int p = dutch.recruits[0];
        const bool unskilled = p == 0x19 || p == 0x1A || p == 0x1C;
        VCR_CHECK(unskilled || (p < 25 && p != 1 && p != 2 && p != 3 && p != 4 && p != 18 && p != 19 && p != 23));
        if (!unskilled) {
            ++experts;
            VCR_CHECK(dutch.raw[68] != before);
        }
    }
    VCR_CHECK(experts > 0);
    // With William Brewster neither criminals nor servants come.
    dutch.raw[7 + 20 / 8] |= 1U << (20 % 8);
    for (int i = 0; i < 200; ++i) {
        dutch.gold = 100000;
        dutch.recruitCount = 0;
        g.state().units.clear();
        g.recruit(1);
        VCR_CHECK(dutch.recruits[1] != 0x19 && dutch.recruits[1] != 0x1A);
    }
}

VCR_TEST("crosses: a newcomer brings a cheerful tune; with Brewster the player chooses") {
    Game g = crossGame(7, 9);
    g.endTurn();
    auto notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && !notices[0].music.empty());
    VCR_CHECK(!notices.empty() && notices[0].music[0].kind == MusicCue::Kind::SetMood &&
              notices[0].music[0].value == 2);

    Game brewster = crossGame(7, 9);
    brewster.state().nations[3].raw[7 + 20 / 8] |= 1U << (20 % 8);
    brewster.endTurn();
    notices = brewster.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices[0].section == "RECRUITCHOOSE");
    VCR_CHECK(brewster.unitCount() == 0); // nobody until the player chooses
    VCR_CHECK(brewster.state().nations[3].crosses == 0);
    VCR_CHECK(brewster.recruitFree(1) >= 0 && brewster.unitCount() == 1);
}

VCR_TEST("natives: trading or living with a tribe now and then brings its tune") {
    Game g = crossGame(0, 10);
    int cues = 0;
    for (int i = 0; i < 400; ++i) {
        g.villageVisitMood(3, 1); // the Aztecs
        for (const auto& cue : g.takeMusicCues()) {
            VCR_CHECK(cue.kind == MusicCue::Kind::SetMood && cue.value == 6);
            ++cues;
        }
        g.villageVisitMood(0, 1); // not the player: nothing
        VCR_CHECK(g.takeMusicCues().empty());
    }
    VCR_CHECK(cues > 60 && cues < 140);
}

VCR_TEST("natives: without Background Music, entering a village turns the music to the tribe's") {
    for (const bool background : {true, false}) {
        Game g = crossGame(0, 10);
        auto options = sound::SoundOptions::fromSave(g.state().header[22]);
        options.background = background;
        g.state().header[22] = options.toSave(g.state().header[22]);
        g.villageEntryMood(3, 0); // the Inca
        const auto cues = g.takeMusicCues();
        VCR_CHECK(background ? cues.empty() : cues == std::vector<MusicCue>{{MusicCue::Kind::ChangeMood, 7}});
        g.villageEntryMood(1, 0);
        VCR_CHECK(g.takeMusicCues().empty());
    }
}
