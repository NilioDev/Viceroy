// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/game_files.hpp"
#include "viceroy/sound/music_driver.hpp"
#include "viceroy/sound/music_stream.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using namespace viceroy;
using namespace viceroy::sound;

namespace {

std::optional<MusicDriver> loadDriver(const char* name) {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    const auto path = findFileNoCase(dir, name);
    if (!path) {
        return std::nullopt;
    }
    std::ifstream in(*path, std::ios::binary);
    const std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>(in), {});
    return MusicDriver::load(bytes);
}

} // namespace

VCR_TEST("music driver: other files are not drivers") {
    std::vector<std::uint8_t> junk(0x800, 0);
    VCR_CHECK(!MusicDriver::load(junk));
    junk[0] = 'M';
    junk[1] = 'Z';
    VCR_CHECK(!MusicDriver::load(junk));
}

VCR_TEST("original files: the OPL3 driver's tunes start their voices") {
    const auto driver = loadDriver("PSOUND.COL");
    if (!driver) {
        VCR_SKIP("no PSOUND.COL in gamedata/");
    }
    VCR_CHECK(driver->opl3());
    // "Bird Song" (command 20h) starts six music voices after fading the others.
    const Piece* bird = driver->piece(0x20);
    VCR_CHECK(bird != nullptr && bird->starts.size() == 6 && bird->stopFirst);
    VCR_CHECK(bird->starts[0].voice == 0 && bird->starts[5].voice == 5);
    // Some pieces hand one voice to the digitised drums.
    const Piece* march = driver->piece(0x28);
    VCR_CHECK(march != nullptr && march->drumVoice == 4);
    VCR_CHECK(driver->piece(0x10) == nullptr && driver->piece(0x60) == nullptr);
}

VCR_TEST("original files: the AdLib driver is not read yet") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!findFileNoCase(dir, "ASOUND.COL")) {
        VCR_SKIP("no ASOUND.COL in gamedata/");
    }
    VCR_CHECK(!loadDriver("ASOUND.COL"));
}

VCR_TEST("original files: Shady Grove plays as the original's driver plays it") {
    const auto driver = loadDriver("PSOUND.COL");
    if (!driver) {
        VCR_SKIP("no PSOUND.COL in gamedata/");
    }
    // The register writes of the first 30 seconds, checked write for write
    // against a capture of the original on an OPL3 card, folded into a hash.
    MusicPlayer player(*driver);
    std::uint32_t hash = 0x811C9DC5;
    int writes = 0;
    player.onWrite = [&](int set, int reg, int value) {
        for (const int b : {set, reg, value}) {
            hash = (hash ^ static_cast<std::uint32_t>(b)) * 0x01000193;
        }
        ++writes;
    };
    VCR_CHECK(player.command(0x23));
    VCR_CHECK(player.busy());
    for (int t = 0; t < 1824; ++t) {
        player.tick();
    }
    VCR_CHECK(writes == 1844);
    VCR_CHECK(hash == 0x9C639B64);
}

VCR_TEST("original files: a tune fades out when stopped") {
    const auto driver = loadDriver("PSOUND.COL");
    if (!driver) {
        VCR_SKIP("no PSOUND.COL in gamedata/");
    }
    MusicPlayer player(*driver);
    player.command(0x23);
    for (int t = 0; t < 300; ++t) {
        player.tick();
    }
    player.stop();
    int t = 0;
    while (player.busy() && t < 5000) {
        player.tick();
        ++t;
    }
    VCR_CHECK(!player.busy());
    VCR_CHECK(t > 10);
}

VCR_TEST("original files: the music stream makes sound") {
    const auto driver = loadDriver("PSOUND.COL");
    if (!driver) {
        VCR_SKIP("no PSOUND.COL in gamedata/");
    }
    MusicStream stream(*driver, 44100);
    stream.player().command(0x23);
    std::vector<std::int16_t> pcm(44100 * 2 * 3); // three seconds
    stream.render(pcm);
    int peak = 0;
    for (const std::int16_t s : pcm) {
        peak = std::max(peak, s < 0 ? -static_cast<int>(s) : static_cast<int>(s));
    }
    VCR_CHECK(peak > 1000);
}
