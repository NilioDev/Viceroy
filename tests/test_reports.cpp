// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/savegame.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/render/report_view.hpp"
#include "viceroy/ui/play.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>

using namespace viceroy;

namespace {

std::optional<SaveGame> firstSave(const std::filesystem::path& dir) {
    const auto file = findFileNoCase(dir, "COLONY00.SAV");
    if (!file) {
        return std::nullopt;
    }
    std::ifstream in(*file, std::ios::binary);
    return readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {}));
}

// Whether any pixel of the rows differs between the two screens.
bool rowsDiffer(const Screen& a, const Screen& b, int y0, int y1) {
    for (int y = y0; y < y1; ++y) {
        for (int x = 0; x < Screen::kWidth; ++x) {
            if (a.at(x, y) != b.at(x, y)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

VCR_TEST("original files: F2 and F4 to F10 open their reports until a key") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    auto save = firstSave(dir);
    VCR_CHECK(save.has_value());
    PlayScreen play(Game(*save, art.rules), {});
    play.setArt(&art);
    for (const auto& [command, report] : {std::pair{Command::ReligionReport, 2}, std::pair{Command::LaborReport, 4},
                                          std::pair{Command::EconomyReport, 5}, std::pair{Command::ColonyAdviser, 6},
                                          std::pair{Command::NavalReport, 7}, std::pair{Command::ForeignReport, 8},
                                          std::pair{Command::NativeReport, 9}, std::pair{Command::ScoreReport, 10}}) {
        play.handle(command);
        VCR_CHECK(play.report() == report);
        Screen screen;
        play.draw(screen, art);
        VCR_CHECK(report == 10 || screen.at(286, 184) == 0x77); // the OK box (none on the score)
        for (int i = 0; i < 40 && play.reportUp(); ++i) {
            play.handle(Command::Confirm);
        }
        VCR_CHECK(!play.reportUp());
    }
    // The REPORTS menu opens them too: Religious Adviser is its second row.
    VCR_CHECK(play.clickMenu(120, 3) && play.menu()->menu == 3);
    for (int i = 0; i < 10 && play.menu()->selected != 1; ++i) {
        play.handle(Command::MoveS);
    }
    play.handle(Command::Confirm);
    VCR_CHECK(play.report() == 2);
}

VCR_TEST("original files: no Foreign Affairs report after the declaration") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    auto save = firstSave(dir);
    VCR_CHECK(save.has_value());
    save->header[18] |= 0x01; // the war of independence
    PlayScreen play(Game(*save, art.rules), {});
    play.setArt(&art);
    play.handle(Command::ForeignReport);
    VCR_CHECK(!play.reportUp());
    VCR_CHECK(play.dialog().has_value());
}

VCR_TEST("original files: Jan de Witt adds each nation's figures to Foreign Affairs") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    auto save = firstSave(dir);
    VCR_CHECK(save.has_value());
    const Game without(*save, art.rules);
    const int human = without.human();
    auto& raw = save->nations[static_cast<std::size_t>(human)].raw;
    raw[7] |= 0x10; // Jan de Witt is father 4
    const Game with(*save, art.rules);
    Screen a;
    Screen b;
    drawForeignReport(a, art, without, human);
    drawForeignReport(b, art, with, human);
    // The first nation's figures stand on the two lines under its name.
    VCR_CHECK(!rowsDiffer(a, b, 0, 20));
    VCR_CHECK(rowsDiffer(a, b, 20, 34));
}

VCR_TEST("original files: the Naval Adviser has seven units to a page") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    auto save = firstSave(dir);
    VCR_CHECK(save.has_value());
    const int human = Game(*save, art.rules).human();
    // Only nine merchantmen of the player's.
    auto more = *save;
    const SaveUnit model = more.units.front();
    for (auto& u : more.units) {
        if (u.nation == human) {
            u.nation = (human + 1) % kEuropeanNations;
        }
    }
    VCR_CHECK(navalReportPages(Game(more, art.rules), human) == 1);
    for (int i = 0; i < 9; ++i) {
        SaveUnit ship = model;
        ship.type = 14;
        ship.nation = human;
        ship.holds = 0;
        more.units.push_back(ship);
    }
    const Game many(more, art.rules);
    VCR_CHECK(navalReportPages(many, human) == 2);
    Screen first;
    Screen second;
    drawNavalReport(first, art, many, human, 0);
    drawNavalReport(second, art, many, human, 1);
    // The second page has two rows: its third row is empty.
    VCR_CHECK(rowsDiffer(first, second, 83, 98));
    Screen empty;
    drawNavalReport(empty, art, many, human, 2);
    VCR_CHECK(!rowsDiffer(second, empty, 83, 98));
}

VCR_TEST("labor report: the grid's places, as the original lays them out") {
    // Three columns of 8, 9 and 9 from (2, 25), 105 by 18 each.
    VCR_CHECK(laborReportHit(3, 26) == 0);             // Expert Farmers
    VCR_CHECK(laborReportHit(3, 25 + 7 * 18) == 7);    // Expert Silver Miners
    VCR_CHECK(laborReportHit(108, 26) == 8);           // Expert Fishermen
    VCR_CHECK(laborReportHit(213, 26) == 17);          // Elder Statesmen
    VCR_CHECK(laborReportHit(213, 25 + 18) == 20);     // Hardy Pioneers: teachers and units' colonists left out
    VCR_CHECK(laborReportHit(213, 25 + 8 * 18) == 19); // Free Colonists, last
    VCR_CHECK(laborReportHit(300, 190) == -1);         // the OK box
    VCR_CHECK(laborReportHit(3, 5) == -1);
}

VCR_TEST("original files: a click on a profession zooms, a key goes back, a click elsewhere closes") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    auto save = firstSave(dir);
    VCR_CHECK(save.has_value());
    PlayScreen play(Game(*save, art.rules), {});
    play.setArt(&art);
    play.handle(Command::LaborReport);
    VCR_CHECK(play.report() == 4 && play.reportPage() == 0);
    play.clickReport(220, 25 + 8 * 18 + 4);
    VCR_CHECK(play.reportPage() == 20);
    Screen screen;
    play.draw(screen, art);
    VCR_CHECK(screen.at(286, 184) == 0x77);
    play.handle(Command::Confirm);
    VCR_CHECK(play.report() == 4 && play.reportPage() == 0);
    play.clickReport(5, 5);
    VCR_CHECK(!play.reportUp());
}

VCR_TEST("original files: the Economic and Colony Advisers turn their pages") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    auto save = firstSave(dir);
    VCR_CHECK(save.has_value());
    const Game game(*save, art.rules);
    const int human = game.human();
    int colonies = 0;
    for (const auto& c : game.state().colonies) {
        colonies += c.nation == human ? 1 : 0;
    }
    // European trade, then 17 colonies to a page of cargo.
    VCR_CHECK(economyReportPages(game, human) == 1 + (colonies + 16) / 17);
    // Garrisons, then the Sons of Liberty, nine colonies to a page each.
    VCR_CHECK(colonyAdviserPages(game, human) == 2 * std::max(1, (colonies + 8) / 9));
    Screen first;
    Screen last;
    drawColonyAdviser(first, art, game, human, 0);
    drawColonyAdviser(last, art, game, human, colonyAdviserPages(game, human) - 1);
    VCR_CHECK(rowsDiffer(first, last, 0, 20)); // the subtitles differ
}

VCR_TEST("original files: the Indian Adviser lists only the tribes met") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    auto save = firstSave(dir);
    VCR_CHECK(save.has_value());
    const int human = Game(*save, art.rules).human();
    auto& relations = save->nations[static_cast<std::size_t>(human)].raw;
    for (int t = 0; t < 8; ++t) {
        relations[static_cast<std::size_t>(56 + t)] &= static_cast<std::uint8_t>(~0x20);
        save->tribesAndMore[static_cast<std::size_t>(t * 78 + 3)] &= 0x7F;
    }
    Screen none;
    drawIndianReport(none, art, Game(*save, art.rules), human);
    relations[56] |= 0x20; // the Incas met
    Screen one;
    drawIndianReport(one, art, Game(*save, art.rules), human);
    // A row of 21 pixels from y 25.
    VCR_CHECK(rowsDiffer(none, one, 25, 46));
    VCR_CHECK(!rowsDiffer(none, one, 46, 180));
    // A tribe gone is listed even unmet.
    save->tribesAndMore[3] |= 0x80;
    relations[56] &= static_cast<std::uint8_t>(~0x20);
    Screen gone;
    drawIndianReport(gone, art, Game(*save, art.rules), human);
    VCR_CHECK(rowsDiffer(none, gone, 25, 46));
}

VCR_TEST("original files: the Colonization Score's parts") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    auto save = firstSave(dir);
    VCR_CHECK(save.has_value());
    const int human = Game(*save, art.rules).human();
    auto& nation = save->nations[static_cast<std::size_t>(human)];
    nation.gold = 0;
    nation.raw[24] = 0;
    for (int i = 7; i <= 10; ++i) {
        nation.raw[static_cast<std::size_t>(i)] = 0;
    }
    save->header[18] = 0;
    save->header[96] = 0;
    save->header[97] = 0;
    const int base = colonizationScore(Game(*save, art.rules), human);
    VCR_CHECK(base > 0);
    // A point for each thousand in gold.
    nation.gold = 12345;
    VCR_CHECK(colonizationScore(Game(*save, art.rules), human) == base + 12);
    nation.gold = 0;
    // Five for each founding father.
    nation.raw[7] = 0x03;
    VCR_CHECK(colonizationScore(Game(*save, art.rules), human) == base + 10);
    nation.raw[7] = 0;
    // Each village burned costs one point more than the difficulty.
    nation.raw[24] = 2;
    VCR_CHECK(colonizationScore(Game(*save, art.rules), human) == base - 2 * (1 + save->difficulty));
    nation.raw[24] = 0;
    // Independence first doubles the score, and the earlier the more.
    for (int n = 0; n < kEuropeanNations; ++n) {
        save->nations[static_cast<std::size_t>(n)].raw[0] &= static_cast<std::uint8_t>(~0x04);
    }
    save->header[18] = 0x08;
    save->header[55] = 17;
    save->header[56] = 75;
    VCR_CHECK(colonizationScore(Game(*save, art.rules), human) == 2 * (base + 10));
    // Once scoring is complete there is no score.
    save->header[18] = 0x10;
    VCR_CHECK(colonizationScore(Game(*save, art.rules), human) == 0);
}

VCR_TEST("a retirement's rating and step of the @SCORE list") {
    // Read from the original: 56 points at difficulty 2 weigh 6 × 56 / 100
    // = 3, the second step (2 × 2 / 3 = 1 < 3, 3 × 3 / 3 = 3 is not), and
    // a rating of half the weight.
    const auto e = exploitsFor(56, 2);
    VCR_CHECK(e.rating == 1 && e.level == 1);
    VCR_CHECK(exploitsFor(0, 4).level == -1);
    VCR_CHECK(exploitsFor(100000, 4).level == 23);
    // Viceroy weighs 10 in a hundred, Discoverer 4.
    VCR_CHECK(exploitsFor(1000, 4).rating == 50 && exploitsFor(1000, 0).rating == 20);
}

VCR_TEST("the Hall of Fame keeps five places, best rating first") {
    auto places = readHallOfFame({});
    VCR_CHECK(places.size() == 6 && places[0].nation == -1 && places[0].rating == -1);
    HallEntry a;
    a.name = "Michiel de Jager";
    a.nation = 3;
    a.year = 1727;
    a.difficulty = 2;
    a.score = 56;
    a.rating = 1;
    a.level = 1;
    VCR_CHECK(enterHallOfFame(places, a) == 0);
    HallEntry b = a;
    b.rating = 1; // a tie goes after
    VCR_CHECK(enterHallOfFame(places, b) == 1);
    HallEntry c = a;
    c.rating = 30;
    VCR_CHECK(enterHallOfFame(places, c) == 0);
    const Bytes file = writeHallOfFame(places);
    VCR_CHECK(file.size() == 210);
    // The original's record: the name, then words from byte 24.
    VCR_CHECK(file[42 + 24] == 3 && file[42 + 30] == 0xBF && file[42 + 31] == 0x06 && file[42 + 36] == 56);
    const auto back = readHallOfFame(file);
    VCR_CHECK(back[0].rating == 30 && back[1].name == "Michiel de Jager" && back[1].year == 1727 &&
              back[3].nation == -1);
}

VCR_TEST("original files: Retire shows the score, the honour and the Hall of Fame, then ends") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    auto save = firstSave(dir);
    VCR_CHECK(save.has_value());
    save->header[18] &= static_cast<std::uint8_t>(~0x10);
    PlayScreen play(Game(*save, art.rules), {});
    play.setArt(&art);
    play.openMenu(0, false);
    for (int i = 0; i < 20 && play.menu()->selected != 7; ++i) {
        play.handle(Command::MoveS);
    }
    play.handle(Command::Confirm);
    VCR_CHECK(play.dialog().has_value());
    for (int i = 0; i < 5 && play.dialog()->selected != 0; ++i) {
        play.handle(Command::MoveN);
    }
    play.handle(Command::Confirm); // "Yes"
    VCR_CHECK(play.ending() == 10 && play.report() == 10);
    play.handle(Command::Confirm);
    const int honour = play.ending();
    VCR_CHECK(honour == 11 || honour == 12);
    Screen screen;
    play.draw(screen, art);
    if (honour == 11) {
        play.handle(Command::Confirm);
    }
    VCR_CHECK(play.ending() == 12);
    VCR_CHECK(!play.quitRequested());
    play.handle(Command::Confirm);
    VCR_CHECK(play.quitRequested());
}
