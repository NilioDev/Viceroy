// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/game/europe.hpp"
#include "viceroy/game/new_game.hpp"
#include "viceroy/game/sounds.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/render/game_view.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace viceroy;

namespace {

Ruleset smallRules() {
    Ruleset r;
    r.terrain.resize(26);
    r.units.resize(19);
    r.cargo.resize(16);
    return r;
}

// A 12×8 world: ocean in the first six columns, plains east of them.
SaveGame woodcutSave() {
    SaveGame s;
    s.year = 1500;
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
    for (auto& p : s.players) {
        p.control = 1;
        p.raw[48] = 0x80; // the land is named
    }
    s.players[0].control = 0;
    return s;
}

std::vector<std::string> sections(const std::vector<GameNotice>& notices) {
    std::vector<std::string> out;
    for (const auto& n : notices) {
        out.push_back(n.section);
    }
    return out;
}

} // namespace

VCR_TEST("woodcuts: each is shown once a game, kept in header bytes 154 and 155") {
    Game g(woodcutSave(), smallRules());
    VCR_CHECK(!g.woodcutShown(1) && !g.woodcutShown(13));
    auto w = g.woodcut(9);
    VCR_CHECK(w && w->section == "WOODCUT" && w->value == 9);
    VCR_CHECK(w && w->music == (std::vector<MusicCue>{{MusicCue::Kind::ChangeMood, 2}}));
    VCR_CHECK(g.state().header[155] == 0x01 && g.state().header[154] == 0);
    VCR_CHECK(!g.woodcut(9));
    w = g.woodcut(4); // the Aztecs: their tune
    VCR_CHECK(w && w->music == (std::vector<MusicCue>{{MusicCue::Kind::Pick, 0x35}}));
    VCR_CHECK(g.state().header[154] == 0x08);
    VCR_CHECK(!g.woodcut(0) && !g.woodcut(14));
}

VCR_TEST("woodcuts: a notice that follows one hands it its music") {
    Game g(woodcutSave(), smallRules());
    GameNotice news{"LOSTCITY1", {}};
    news.music = {{MusicCue::Kind::Pick, 0x37}};
    g.pushWithWoodcut(news, 8);
    g.pushWithWoodcut(news, 8);
    const auto notices = g.takeNotices();
    VCR_CHECK(sections(notices) == (std::vector<std::string>{"WOODCUT", "LOSTCITY1", "LOSTCITY1"}));
    VCR_CHECK(notices.size() == 3 && notices[0].music == news.music && notices[1].music.empty());
    VCR_CHECK(notices.size() == 3 && notices[2].music == news.music);
}

VCR_TEST("original files: woodcuts next to the Pacific, and for a ship with cargo in Europe") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    auto s = woodcutSave();
    for (int y = 1; y < 7; ++y) {
        s.map.at(1, y).flags |= MapSquare::kPacific;
    }
    SaveUnit ship;
    ship.x = 4;
    ship.y = 3;
    ship.type = unit_type::kCaravel;
    ship.nation = 0;
    s.units.push_back(ship);
    Game g(s, art.rules);
    VCR_CHECK(g.move(0, {-1, 0}) == MoveResult::Moved); // (3, 3)
    VCR_CHECK(g.takeNotices().empty());
    VCR_CHECK(g.move(0, {-1, 0}) == MoveResult::Moved); // (2, 3): the Pacific next to it
    auto notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices[0].value == 6);
    VCR_CHECK(notices.size() == 1 && notices[0].music == (std::vector<MusicCue>{{MusicCue::Kind::Pick, 0x39}}));

    // One stage from the port with cargo: the woodcut, and the Europe
    // screen to open with the ship.
    setEuropeStage(g.state().units[0], 3);
    g.state().units[0].holds = 1;
    g.endTurn();
    VCR_CHECK(g.takeEuropeArrival() == 0);
    VCR_CHECK(g.woodcutShown(9));
    VCR_CHECK(g.takeEuropeArrival() == -1);
}

VCR_TEST("original files: the Pacific of a new game is the water west of the land") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    std::ifstream mapIn(*findFileNoCase(dir, "AMER2.MP"), std::ios::binary);
    auto map = readMapFile(Bytes(std::istreambuf_iterator<char>(mapIn), {}));
    prepareStartingMap(map);
    // The original's own game on the same map marks the same squares.
    std::ifstream saveIn(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    const auto save = readSaveGame(Bytes(std::istreambuf_iterator<char>(saveIn), {}));
    int pacific = 0;
    int differ = 0;
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            const bool ours = (map.at(x, y).flags & MapSquare::kPacific) != 0;
            pacific += ours ? 1 : 0;
            differ += ours != ((save.map.at(x, y).flags & MapSquare::kPacific) != 0) ? 1 : 0;
        }
    }
    VCR_CHECK(pacific > 800 && differ == 0);
}

VCR_TEST("original files: the first colony greets with a sound and its woodcut") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    auto s = woodcutSave();
    SaveUnit colonist;
    colonist.x = 8;
    colonist.y = 3;
    colonist.type = unit_type::kColonist;
    colonist.profession = 0x1C;
    s.units.push_back(colonist);
    colonist.y = 6;
    s.units.push_back(colonist);
    colonist.nation = 1;
    colonist.x = 10;
    s.units.push_back(colonist);
    Game g(s, art.rules);
    VCR_CHECK(g.foundColony(2, "Rivals") == FoundResult::Founded); // not the player's
    VCR_CHECK(g.takeNotices().empty() && g.takeMusicCues().empty());
    VCR_CHECK(g.foundColony(0, "Jamestown") == FoundResult::Founded);
    auto notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices[0].value == 2);
    VCR_CHECK(notices.size() == 1 && notices[0].music == (std::vector<MusicCue>{{MusicCue::Kind::MoodEvent, 2}}));
    VCR_CHECK(g.takeMusicCues() == (std::vector<MusicCue>{{MusicCue::Kind::Sound, sounds::kColonyFounded}}));
    VCR_CHECK(g.foundColony(0, "Plymouth") == FoundResult::Founded);
    VCR_CHECK(g.takeNotices().empty());
    VCR_CHECK(g.takeMusicCues() == (std::vector<MusicCue>{{MusicCue::Kind::Sound, sounds::kColonyFounded}}));

    // The picture: frame, woodcut and plate where the original puts them.
    Screen screen;
    drawWoodcut(screen, art, 1);
    const auto& frame = art.woodcutFrame.sprites.front().image;
    VCR_CHECK(frame.width == 274 && frame.height == 170);
    VCR_CHECK(screen.at(0, 0) == 0 && screen.at(319, 199) == 0);
    int plate = 0;
    for (int x = 0; x < 320; ++x) {
        plate += screen.at(x, 165) == 0x5E ? 1 : 0; // the title's letters
    }
    VCR_CHECK(plate > 20);
}
