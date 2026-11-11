// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/rules/ruleset.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace viceroy;

namespace {

using V = std::vector<std::uint8_t>;

// A made-up rule file with the same layout as NAMES.TXT (not its values).
const std::string kSample =
    "@UNFORESTED\r\nFlat, 1, 0, 3, 2,  1, 2, 3, 4, 5, 6, 7, 8, 9\r\n\r\n"
    "@FORESTED\r\nWoods, 2, 1, 4, 1,  0, 0, 0, 0, 2, 3, 0, 0, 0\r\n\r\n"
    "@OTHER\r\nWater, 1, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 4\r\n\r\n"
    "@BUILDING\r\nHut, 10, 2, 1, 1, 0\r\n\r\n"
    "; comment between sections\r\n"
    "@JOB\r\nDigger, Expert Diggers, 1, 500\r\nIdler, Idlers, 4, -1\r\n\r\n"
    "@CARGO\r\nBread, 1, 2, 1, 5, 6, 3, 2, -1, 0\r\nBells\r\n\r\n"
    "@UNIT\r\nWalker, 7, 1, 0, 1, 0, 1, 1, 0, 0, 0, 01000000  ; note\r\n\r\n"
    "@CLASS\r\nFolk, 300\r\n\r\n"
    "@FATHERS\r\nSomebody, 2, 1, 5, 9\r\n\r\n"
    "@TRIBES\r\nHill People, Hill, Shells, 1, 50\r\n\r\n; spare names\r\nOthers, Other\r\n";

V bytesOf(const std::string& s) { return V(s.begin(), s.end()); }

V readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return V(std::istreambuf_iterator<char>(in), {});
}

bool rejects(const std::string& text) {
    try {
        readRuleset(parseTextFile(bytesOf(text)));
    } catch (const FormatError&) {
        return true;
    }
    return false;
}

std::string replaced(std::string s, const std::string& from, const std::string& to) {
    s.replace(s.find(from), from.size(), to);
    return s;
}

} // namespace

VCR_TEST("rules: tables are read column by column") {
    const auto rules = readRuleset(parseTextFile(bytesOf(kSample)));
    VCR_CHECK(rules.terrain.size() == 3);
    VCR_CHECK(rules.terrain[0].name == "Flat");
    VCR_CHECK(rules.terrain[0].improvementTurns == 3);
    VCR_CHECK(rules.terrain[0].yield[static_cast<int>(SquareWork::Fisherman)] == 9);
    VCR_CHECK(rules.terrain[1].group == TerrainType::Group::Forested);
    VCR_CHECK(rules.buildings.at(0).tools == 20);
    VCR_CHECK(rules.jobs.size() == 2);
    VCR_CHECK(rules.jobs[1].europeCost == -1);
    VCR_CHECK(rules.cargo.size() == 2);
    VCR_CHECK(rules.cargo[0].tradable && rules.cargo[0].attrition == -1);
    VCR_CHECK(!rules.cargo[1].tradable);
    VCR_CHECK(rules.units.at(0).aiRoles == 0x40);
    VCR_CHECK(rules.classes.at(0).transportCost == 300);
    VCR_CHECK((rules.fathers.at(0).weight == std::array<int, 3>{1, 5, 9}));
    VCR_CHECK(rules.tribes.size() == 1);
    VCR_CHECK(rules.tribes[0].treasure == "Shells");
}

VCR_TEST("rules: broken tables are errors") {
    VCR_CHECK(rejects(replaced(kSample, "@CLASS", "@KLASS")));
    VCR_CHECK(rejects(replaced(kSample, "Hut, 10, 2, 1, 1, 0", "Hut, 10, 2, 1, 1")));
    VCR_CHECK(rejects(replaced(kSample, "Hut, 10,", "Hut, ten,")));
    VCR_CHECK(rejects(replaced(kSample, "Hut, 10,", "Hut, 99999999999,")));
    VCR_CHECK(rejects(replaced(kSample, "01000000", "0100000")));
    VCR_CHECK(rejects(replaced(kSample, "01000000", "01200000")));
    VCR_CHECK(rejects(replaced(kSample, "Folk, 300\r\n", "")));
}

VCR_TEST("original files: NAMES.TXT gives the full rule set") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const auto rules = readRuleset(parseTextFile(readAll(*findFileNoCase(dir, "NAMES.TXT"))));
    VCR_CHECK(rules.terrain.size() == 21);
    VCR_CHECK(rules.buildings.size() == 42);
    VCR_CHECK(rules.jobs.size() == 28);
    VCR_CHECK(rules.cargo.size() == 20);
    VCR_CHECK(rules.units.size() == 23);
    VCR_CHECK(rules.classes.size() == 8);
    VCR_CHECK(rules.fathers.size() == 25);
    VCR_CHECK(rules.tribes.size() == 8);
}
