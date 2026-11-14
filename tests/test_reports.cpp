// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/savegame.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/render/report_view.hpp"
#include "viceroy/ui/play.hpp"

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

VCR_TEST("original files: F2, F7 and F8 open their reports until a key") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    auto save = firstSave(dir);
    VCR_CHECK(save.has_value());
    PlayScreen play(Game(*save, art.rules), {});
    play.setArt(&art);
    for (const auto& [command, report] : {std::pair{Command::ReligionReport, 2}, std::pair{Command::NavalReport, 7},
                                          std::pair{Command::ForeignReport, 8}}) {
        play.handle(command);
        VCR_CHECK(play.report() == report);
        Screen screen;
        play.draw(screen, art);
        VCR_CHECK(screen.at(286, 184) == 0x77); // the OK box
        for (int i = 0; i < 10 && play.reportUp(); ++i) {
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
