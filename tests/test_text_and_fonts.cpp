// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/font.hpp"
#include "viceroy/data/reader.hpp"
#include "viceroy/data/text_table.hpp"
#include "viceroy/game_files.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace viceroy;

namespace {

using V = std::vector<std::uint8_t>;

V bytesOf(std::string_view s) { return V(s.begin(), s.end()); }

V readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return V(std::istreambuf_iterator<char>(in), {});
}

// A font with height 2 and one glyph, 'A' (65), 3 pixels wide:
// row 0 = 1 2 3, row 1 = 0 3 0.
V tinyFont() {
    V d(128 + 2 * 127, 0);
    d[0] = 2;
    d[1] = 3;
    d[1 + 65] = 3;
    const auto offset = static_cast<std::uint16_t>(d.size());
    d[128 + 2 * 65] = static_cast<std::uint8_t>(offset);
    d[128 + 2 * 65 + 1] = static_cast<std::uint8_t>(offset >> 8);
    d.push_back(0b01101100);
    d.push_back(0b00110000);
    return d;
}

} // namespace

VCR_TEST("text: sections, directives and comments") {
    const auto file = parseTextFile(bytesOf(";\r\n; header\r\n\r\n@SEASONS\r\nSpring\r\nAutumn\r\n\r\n"
                                            "@MENU   ; comment\r\n@width=160\r\n@@VICEROY\r\nText\r\n"));
    VCR_CHECK(file.sections.size() == 2);
    const auto* seasons = file.find("SEASONS");
    VCR_CHECK(seasons != nullptr);
    VCR_CHECK((seasons->lines == std::vector<std::string>{"Spring", "Autumn", ""}));
    const auto* menu = file.find("MENU");
    VCR_CHECK(menu != nullptr);
    VCR_CHECK((menu->lines == std::vector<std::string>{"@width=160", "@@VICEROY", "Text"}));
    VCR_CHECK(file.find("NOPE") == nullptr);
}

VCR_TEST("text: table fields and numbers") {
    const auto f = splitFields("Tundra,       1, 0, 4, 2     ; a comment");
    VCR_CHECK((f == std::vector<std::string>{"Tundra", "1", "0", "4", "2"}));
    VCR_CHECK(splitFields("   ; only a comment").empty());
    VCR_CHECK(parseNumber(" 42 ") == 42L);
    VCR_CHECK(parseNumber("-3") == -3L);
    VCR_CHECK(!parseNumber("4x").has_value());
    VCR_CHECK(!parseNumber("").has_value());
}

VCR_TEST("text: code page 437 becomes UTF-8") {
    const V text = {'a', 0x82, 0x9C, 0xDB};
    VCR_CHECK(cp437ToUtf8(text) == "a\xC3\xA9\xC2\xA3\xE2\x96\x88"); // a é £ █
}

VCR_TEST("fonts: two bits per pixel, high bits first") {
    const auto font = decodeFont(tinyFont());
    VCR_CHECK(font.height == 2);
    VCR_CHECK(font.glyphs['A'].width == 3);
    VCR_CHECK((font.glyphs['A'].pixels == V{1, 2, 3, 0, 3, 0}));
    VCR_CHECK(font.glyphs['B'].width == 0);
}

VCR_TEST("fonts: damaged data is an error") {
    auto font = tinyFont();
    font.pop_back();
    VCR_CHECK([&] {
        try {
            decodeFont(font);
        } catch (const FormatError&) {
            return true;
        }
        return false;
    }());
    for (std::size_t i = 0; i < 4; ++i) {
        for (int value = 0; value < 256; ++value) {
            auto mutated = tinyFont();
            mutated[i < 2 ? i : 128 + 2 * 65 + (i - 2)] = static_cast<std::uint8_t>(value);
            try {
                decodeFont(mutated);
            } catch (const FormatError&) {
            }
        }
    }
}

VCR_TEST("original files: fonts and text tables") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    for (const char* name : {"FONTTINY.FF", "FONTSMAL.FF", "FONTKING.FF", "FONTINTR.FF", "FONT-NP.FF"}) {
        const auto path = findFileNoCase(dir, name);
        VCR_CHECK(path.has_value());
        const auto font = readFont(readAll(*path));
        VCR_CHECK(font.height > 0);
        VCR_CHECK(font.glyphs['A'].width > 0);
    }
    const auto names = parseTextFile(readAll(*findFileNoCase(dir, "NAMES.TXT")));
    for (const char* section : {"SEASONS", "UNFORESTED", "FORESTED", "UNIT", "BUILDING", "CARGO", "FATHERS"}) {
        VCR_CHECK(names.find(section) != nullptr);
    }
    const auto game = parseTextFile(readAll(*findFileNoCase(dir, "GAME.TXT")));
    VCR_CHECK(game.sections.size() == 499);
    VCR_CHECK(game.find("BEGINMENU") != nullptr);
}
