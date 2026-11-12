// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/game_files.hpp"
#include "viceroy/render/game_view.hpp"
#include "viceroy/ui/title.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace viceroy;

namespace {

// For comparing with the original: VICEROY_DUMP_TITLE=prefix.
void dump(const Screen& screen, const Art& art, const std::string& name) {
    const char* base = std::getenv("VICEROY_DUMP_TITLE");
    if (base == nullptr) {
        return;
    }
    std::ofstream out(std::string(base) + "_" + name, std::ios::binary);
    out.write(reinterpret_cast<const char*>(screen.pixels().data()),
              static_cast<std::streamsize>(screen.pixels().size()));
    for (int i = 0; i < 256; ++i) {
        const auto c = screen.colour(art.palette, static_cast<std::uint8_t>(i));
        out.put(static_cast<char>(c.r)).put(static_cast<char>(c.g)).put(static_cast<char>(c.b));
    }
}

} // namespace

VCR_TEST("original files: the main menu offers the original's choices") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    const TitleArt title = loadTitleArt(dir);
    MainMenu menu(art, "0.0.1", "7-Feb-95");
    VCR_CHECK(menu.dialog().options.size() == 5);
    VCR_CHECK(menu.dialog().y == 91);
    VCR_CHECK(menu.dialog().small);
    VCR_CHECK(menu.dialog().lines.size() == 1);
    VCR_CHECK(menu.dialog().lines.front().find("{Viceroy}") == 0);
    Screen screen;
    menu.draw(screen, art, title);
    dump(screen, art, "menu");

    VCR_CHECK(!menu.handle(Command::Down));
    VCR_CHECK(!menu.handle(Command::Down));
    VCR_CHECK(!menu.handle(Command::Down));
    const auto load = menu.handle(Command::Confirm);
    VCR_CHECK(load && *load == TitleChoice::Load);
    // A click on the first choice.
    const auto& d = menu.dialog();
    const auto first = menu.click(d.x + 20, d.y + 6 + 6 + 4 + 2);
    VCR_CHECK(first && *first == TitleChoice::NewWorld);
    VCR_CHECK(!menu.click(5, 5));
}

VCR_TEST("original files: the opening plays and can be skipped") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    const TitleArt title = loadTitleArt(dir);
    Intro intro(title);
    VCR_CHECK(intro.frame() == 0);
    VCR_CHECK(!intro.finished());
    Screen screen;
    intro.draw(screen, title);
    // VICEROY_INTRO_FRAMES=40,211,... dumps those frames.
    if (const char* frames = std::getenv("VICEROY_INTRO_FRAMES")) {
        std::string list = frames;
        std::size_t at = 0;
        while (at < list.size()) {
            const int f = std::atoi(list.c_str() + at);
            Intro shown(title);
            shown.tick(static_cast<std::uint64_t>(f) * Intro::kFrameMs);
            shown.draw(screen, title);
            dump(screen, art, "intro" + std::to_string(f));
            const auto comma = list.find(',', at);
            at = comma == std::string::npos ? list.size() : comma + 1;
        }
    }
    intro.tick(100 * Intro::kFrameMs);
    VCR_CHECK(intro.frame() == 100);
    intro.skip();
    VCR_CHECK(intro.finished());
    MainMenu menu(art, "0.0.1", "7-Feb-95");
    menu.draw(screen, art, title, &intro);
    dump(screen, art, "menu_after_intro");
}
