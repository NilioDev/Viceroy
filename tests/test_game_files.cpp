// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/game_files.hpp"

#include <filesystem>
#include <fstream>

using namespace viceroy;

VCR_TEST("file names are matched without regard to case") {
    const auto dir = std::filesystem::temp_directory_path() / "viceroy_test_game_files";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "viceroy.pal") << "x";
    VCR_CHECK(findFileNoCase(dir, "VICEROY.PAL").has_value());
    VCR_CHECK(isGameFolder(dir));
    VCR_CHECK(!findFileNoCase(dir, "MISSING.SS").has_value());
    std::filesystem::remove_all(dir);
    VCR_CHECK(!isGameFolder(dir));
}

VCR_TEST("the original game folder is recognised when present") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    VCR_CHECK(findFileNoCase(dir, "VICEROY.PAL").has_value());
}
