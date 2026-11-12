// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/game/new_game.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/resource.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/render/game_view.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace viceroy;

namespace {

Bytes mapFile(int w, int h, std::uint8_t terrain) {
    Bytes b{static_cast<std::uint8_t>(w), 0, static_cast<std::uint8_t>(h), 0, 4, 0};
    b.resize(6 + 3 * static_cast<std::size_t>(w * h), 0);
    for (int i = 0; i < w * h; ++i) {
        b[6 + static_cast<std::size_t>(i)] = terrain;
    }
    return b;
}

} // namespace

VCR_TEST("new game: a map file is read and checked") {
    auto file = mapFile(10, 8, 25);
    const auto map = readMapFile(file);
    VCR_CHECK(map.width == 10 && map.height == 8);
    VCR_CHECK(map.at(3, 3).terrain == 25);
    file.resize(20);
    bool threw = false;
    try {
        (void)readMapFile(file);
    } catch (const FormatError&) {
        threw = true;
    }
    VCR_CHECK(threw);
}

VCR_TEST("new game: the starting map is prepared as the original does") {
    auto file = mapFile(20, 20, 16 + 4); // forest of grassland, second numbering
    file[6 + 5 * 20 + 5] = static_cast<std::uint8_t>((5 << 5) | 21); // forest on mountains
    auto map = readMapFile(file);
    prepareStartingMap(map);
    VCR_CHECK(map.at(8, 8).terrain == 12);
    VCR_CHECK(map.at(5, 5).terrain == 5 && map.at(5, 5).mountains());
    VCR_CHECK(map.at(7, 0).terrain == 24 && map.at(7, 19).terrain == 24);
    VCR_CHECK(map.at(0, 7).terrain == 26 && map.at(1, 7).terrain == 26 && map.at(19, 7).terrain == 26);
    VCR_CHECK(map.at(2, 7).terrain == 12);
    VCR_CHECK((map.at(8, 8).region & 0xF0) == 0xF0);
}

VCR_TEST("new game: open sea far from land has no fish") {
    auto map = readMapFile(mapFile(64, 64, 25));
    map.at(32, 32).terrain = 4;
    map.resourceSeed = 78;
    markSeaWithoutFish(map);
    for (int y = 1; y < 63; ++y) {
        for (int x = 1; x < 63; ++x) {
            if (!onResourcePattern(x, y, 78)) {
                VCR_CHECK((map.at(x, y).flags & 4) == 0);
                continue;
            }
            const int dx = x > 32 ? x - 32 : 32 - x;
            const int dy = y > 32 ? y - 32 : 32 - y;
            const bool nearLand = dx <= 2 && dy <= 2 && dx + dy <= 3;
            VCR_CHECK(((map.at(x, y).flags & 4) != 0) == !nearLand);
        }
    }
}

VCR_TEST("new game: settlements are placed near their sites, apart, with braves") {
    auto file = mapFile(58, 72, 3); // grassland everywhere
    auto map = readMapFile(file);
    prepareStartingMap(map);
    std::vector<Tribe> tribes(8);
    tribes[0].level = 3;
    NewGameRandom random(12345);
    const auto settlements = placeAmericaSettlements(map, tribes, random);
    VCR_CHECK(settlements.size() == 56);
    int capitals = 0;
    for (std::size_t i = 0; i < settlements.size(); ++i) {
        const auto& s = settlements[i];
        VCR_CHECK(map.at(s.x, s.y).isLand());
        capitals += s.raw[3] == 4 ? 1 : 0;
        for (std::size_t j = 0; j < i; ++j) {
            const auto& o = settlements[j];
            VCR_CHECK(std::abs(o.x - s.x) > 1 || std::abs(o.y - s.y) > 1);
        }
    }
    VCR_CHECK(capitals == 8);
    VCR_CHECK(settlements.front().nation == 4 && settlements.front().population == 9);
    VCR_CHECK(settlements.back().nation == 11 && settlements.back().population == 3);
    const auto braves = settlementBraves(settlements);
    VCR_CHECK(braves.size() == 56 && braves[5].x == settlements[5].x && braves[5].raw[6] == 5);
}

VCR_TEST("new game: no settlement on water") {
    auto map = readMapFile(mapFile(58, 72, 25));
    NewGameRandom random(1);
    VCR_CHECK(placeAmericaSettlements(map, {}, random).empty());
}

VCR_TEST("original files: a new game on America starts as the original's") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "AMER2.MP"), std::ios::binary);
    const Bytes file(std::istreambuf_iterator<char>(in), {});
    NewGameSetup setup;
    setup.humanNation = 3;
    setup.seed = 99;
    for (int n = 0; n < 4; ++n) {
        setup.leaders[static_cast<std::size_t>(n)] = textLine(art.names, "LEADERNAME", n, "");
        setup.countries[static_cast<std::size_t>(n)] = textLine(art.names, "COLONYNAME", n, "");
    }
    const auto save = newAmericaGame(file, art.rules, setup);
    VCR_CHECK(save.map.width == 58 && save.map.height == 72);
    VCR_CHECK(save.units.size() == 12 + save.settlements.size());
    VCR_CHECK(save.settlements.size() >= 54);
    VCR_CHECK(save.players[3].name == "Michiel De Ruyter" && save.players[0].country == "New England");
    VCR_CHECK(save.nations[3].gold == 1000 && save.nations[0].gold == 0);
    VCR_CHECK(save.units[9].type == 14 && save.activeUnit == 9);
    for (const auto& s : save.settlements) {
        VCR_CHECK(save.map.at(s.x, s.y).isLand() && !save.map.at(s.x, s.y).mountains());
    }
    // It saves and loads back, and a game can be played from it.
    const auto again = readSaveGame(writeSaveGame(save));
    VCR_CHECK(again.units.size() == save.units.size() && again.map.resourceSeed == save.map.resourceSeed);
    VCR_CHECK(again.humanNation == 3 && again.year == 1492);
    Game game(again, art.rules);
    VCR_CHECK(game.isAboard(10) && game.isAboard(11));
    VCR_CHECK(!game.isAboard(9));
}

#include "viceroy/ui/new_game_flow.hpp"

VCR_TEST("original files: the screens of a new game, from the map to the departure") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    const NewGameArt pictures = loadNewGameArt(dir);
    NewGameFlow flow(art);
    using Step = NewGameFlow::Step;
    Screen screen;
    flow.draw(screen, art, pictures);
    flow.handle(Command::Confirm); // the original map
    VCR_CHECK(flow.step() == Step::Difficulty);
    flow.handle(Command::Right);
    flow.handle(Command::Right);
    VCR_CHECK(flow.difficulty() == 2);
    flow.draw(screen, art, pictures);
    VCR_CHECK(screen.at(23, 103) == 14); // the conquistador's yellow frame
    flow.handle(Command::Confirm);
    VCR_CHECK(flow.step() == Step::Nation);
    flow.handle(Command::Down); // England to Spain: the flags lie in a square
    VCR_CHECK(flow.nation() == 2);
    flow.handle(Command::Right);
    VCR_CHECK(flow.nation() == 3);
    flow.handle(Command::Confirm);
    VCR_CHECK(flow.step() == Step::Name && flow.typing());
    VCR_CHECK(flow.name() == "Michiel De Ruyter");
    for (int i = 0; i < 30; ++i) {
        flow.backspace();
    }
    for (const char c : std::string("Piet Hein")) {
        flow.type(c);
    }
    flow.draw(screen, art, pictures);
    flow.handle(Command::Confirm);
    VCR_CHECK(flow.step() == Step::NationPage1);
    flow.draw(screen, art, pictures);
    flow.handle(Command::Confirm);
    flow.handle(Command::Confirm);
    VCR_CHECK(flow.step() == Step::Audience);
    flow.draw(screen, art, pictures);
    flow.handle(Command::Confirm);
    VCR_CHECK(flow.step() == Step::Departure);
    flow.tick(20000);
    flow.draw(screen, art, pictures);
    flow.tick(100000);
    VCR_CHECK(flow.step() == Step::Done);
    const auto setup = flow.setup();
    VCR_CHECK(setup.humanNation == 3 && setup.difficulty == 2);
    VCR_CHECK(setup.leaders[3] == "Piet Hein" && setup.leaders[0] == "Walter Raleigh");
}

VCR_TEST("original files: Escape leaves the new game screens") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    NewGameFlow flow(art);
    flow.handle(Command::Confirm);
    flow.handle(Command::Cancel);
    VCR_CHECK(flow.step() == NewGameFlow::Step::Cancelled);
}
