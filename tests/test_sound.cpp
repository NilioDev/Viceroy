// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/game_files.hpp"
#include "viceroy/sound/background_music.hpp"
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

VCR_TEST("original files: Northfire starts and joins the tune after it") {
    const auto driver = loadDriver("PSOUND.COL");
    if (!driver) {
        VCR_SKIP("no PSOUND.COL in gamedata/");
    }
    const Piece* north = driver->piece(0x25);
    VCR_CHECK(north != nullptr && north->segue.has_value());
    MusicPlayer player(*driver);
    player.command(0x25);
    player.tick();
    VCR_CHECK(player.busy());
}

VCR_TEST("sound options follow the save game's bits") {
    const SoundOptions all = SoundOptions::fromSave(0x9E);
    VCR_CHECK(all.background && all.events && all.effects);
    const SoundOptions none = SoundOptions::fromSave(0x90);
    VCR_CHECK(!none.background && !none.events && !none.effects);
    SoundOptions some;
    some.events = false;
    VCR_CHECK(some.toSave(0x91) == 0x9B);
}

VCR_TEST("background music picks a new tune whenever one ends") {
    BackgroundMusic music(7);
    std::vector<int> out = music.update(false, false);
    VCR_CHECK(out.size() == 1 && out[0] >= 0x20 && out[0] <= 0x3F);
    const int first = out[0];
    VCR_CHECK(music.update(true, false).empty()); // still playing
    out = music.update(false, false);
    VCR_CHECK(out.size() == 1 && out[0] != first);
}

VCR_TEST("background music: Pick Music fades out and plays the pick") {
    BackgroundMusic music(3);
    music.update(false, false);
    music.pick(0x3A);
    std::vector<int> out = music.update(true, false);
    VCR_CHECK(out.size() == 1 && out[0] == 1);
    out = music.update(false, false);
    VCR_CHECK(out.size() == 1 && out[0] == 0x3A && music.current() == 0x3A);
}

VCR_TEST("background music: moods pick from their group of tunes") {
    BackgroundMusic music(11);
    for (int i = 0; i < 20; ++i) {
        music.setMood(3); // the independence tunes, 13-18
        const std::vector<int> out = music.update(false, false);
        VCR_CHECK(out.size() == 1 && out[0] >= 0x28 && out[0] <= 0x2D);
    }
}

VCR_TEST("background music: options silence what they turn off") {
    BackgroundMusic music(5);
    music.options.background = false;
    VCR_CHECK(music.update(false, false).empty());
    music.options.events = false;
    VCR_CHECK(!music.allowed(0x25) && music.allowed(1));
    music.options.effects = false;
    VCR_CHECK(!music.allowed(0x45));
}

VCR_TEST("original files: the OPL3 driver's effects and recordings") {
    const auto driver = loadDriver("PSOUND.COL");
    if (!driver) {
        VCR_SKIP("no PSOUND.COL in gamedata/");
    }
    // 35 recordings, one after another, filling COLDIG.BIN; the five drum
    // hits play faster than the rest.
    const auto& samples = driver->samples();
    VCR_CHECK(samples.size() == 35);
    VCR_CHECK(samples[0].offset == 0 && samples[0].length == 4960 && samples[0].rate == 19050);
    VCR_CHECK(samples[5].rate == 11025 && samples[34].offset + samples[34].length == 993755);
    // Effect 40h: recording 1Fh, else one FM stream; 46h only FM; 4Dh only
    // a recording.
    const Piece* first = driver->piece(0x40);
    VCR_CHECK(first != nullptr && first->sample == 0x1F && first->effectStreams.size() == 1);
    const Piece* fm = driver->piece(0x46);
    VCR_CHECK(fm != nullptr && fm->sample == -1 && fm->effectStreams.size() == 1);
    const Piece* digital = driver->piece(0x4D);
    VCR_CHECK(digital != nullptr && digital->sample == 0x0A && digital->effectStreams.empty());
    // The pieces from 8020h: four voices that wait for the last to end, or
    // three that start at once.
    const Piece* special = driver->piece(0x8020);
    VCR_CHECK(special != nullptr && special->starts.size() == 4 && special->onlyIfIdle);
    const Piece* church = driver->piece(0x8024);
    VCR_CHECK(church != nullptr && church->starts.size() == 3 && !church->onlyIfIdle);
    VCR_CHECK(driver->piece(0x5E) == nullptr && driver->piece(0x8027) == nullptr);

    // Without samples the FM version plays on an effect voice.
    MusicPlayer fmPlayer(*driver);
    VCR_CHECK(fmPlayer.command(0x40));
    fmPlayer.tick();
    VCR_CHECK(fmPlayer.busy());
    // With them the recording is queued instead, and the voices stay silent;
    // a full queue falls back to FM.
    MusicPlayer player(*driver);
    player.setDigital(true);
    std::vector<int> queued;
    bool full = false;
    player.onSample = [&](int s) {
        if (full) {
            return false;
        }
        queued.push_back(s);
        return true;
    };
    VCR_CHECK(player.command(0x40) && queued == std::vector<int>({0x1F}));
    player.tick();
    VCR_CHECK(!player.busy());
    full = true;
    VCR_CHECK(player.command(0x40));
    player.tick();
    VCR_CHECK(player.busy());
}

VCR_TEST("original files: a recording plays through the music stream") {
    const auto driver = loadDriver("PSOUND.COL");
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    const auto coldig = findFileNoCase(dir, "COLDIG.BIN");
    if (!driver || !coldig) {
        VCR_SKIP("no PSOUND.COL or COLDIG.BIN in gamedata/");
    }
    std::ifstream in(*coldig, std::ios::binary);
    MusicStream stream(*driver, 44100);
    stream.setRecordings({std::istreambuf_iterator<char>(in), {}});
    VCR_CHECK(stream.player().command(0x4D));
    std::vector<std::int16_t> pcm(44100 * 2); // one second
    stream.render(pcm);
    int peak = 0;
    for (const std::int16_t s : pcm) {
        peak = std::max(peak, s < 0 ? -static_cast<int>(s) : static_cast<int>(s));
    }
    VCR_CHECK(peak > 1000);
}
