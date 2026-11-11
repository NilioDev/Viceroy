// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/image.hpp"
#include "viceroy/data/madspack.hpp"
#include "viceroy/data/reader.hpp"
#include "viceroy/game_files.hpp"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace viceroy;

namespace {

using V = std::vector<std::uint8_t>;

template <typename F>
bool throwsFormatError(F&& f) {
    try {
        f();
    } catch (const FormatError&) {
        return true;
    }
    return false;
}

// Control bits 1,1,0,0,1,0,0,1 (lowest bit first) = 0x0093:
// literal 'A', literal 'B', short copy of 4 bytes from 2 back, end marker.
const V kAbStream = {'F', 'A', 'B', 12, 0x93, 0x00, 'A', 'B', 0xFE, 0x00, 0x00, 0x00};

V container(const std::vector<std::pair<int, V>>& parts, const std::vector<std::size_t>& sizes) {
    V out(0xB0, 0);
    const std::string sig = "MADSPACK 2.0\x1a";
    std::copy(sig.begin(), sig.end(), out.begin());
    out[14] = static_cast<std::uint8_t>(parts.size());
    for (std::size_t i = 0; i < parts.size(); ++i) {
        auto* e = &out[16 + i * 10];
        e[0] = static_cast<std::uint8_t>(parts[i].first);
        const auto size = sizes[i];
        const auto packed = parts[i].second.size();
        for (int b = 0; b < 4; ++b) {
            e[2 + b] = static_cast<std::uint8_t>(size >> (8 * b));
            e[6 + b] = static_cast<std::uint8_t>(packed >> (8 * b));
        }
    }
    for (const auto& part : parts) {
        out.insert(out.end(), part.second.begin(), part.second.end());
    }
    return out;
}

V readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return V(std::istreambuf_iterator<char>(in), {});
}

} // namespace

VCR_TEST("FAB: literals and an overlapping copy") {
    const auto out = fabDecompress(kAbStream, 6);
    VCR_CHECK((out == V{'A', 'B', 'A', 'B', 'A', 'B'}));
}

VCR_TEST("FAB: wrong expected size is an error") {
    VCR_CHECK(throwsFormatError([] { fabDecompress(kAbStream, 5); }));
    VCR_CHECK(throwsFormatError([] { fabDecompress(kAbStream, 7); }));
}

VCR_TEST("FAB: damaged streams are errors, not crashes") {
    for (std::size_t cut = 0; cut < kAbStream.size(); ++cut) {
        const V truncated(kAbStream.begin(), kAbStream.begin() + static_cast<std::ptrdiff_t>(cut));
        VCR_CHECK(throwsFormatError([&] { fabDecompress(truncated, 6); }));
    }
    V badShift = kAbStream;
    badShift[3] = 2;
    VCR_CHECK(throwsFormatError([&] { fabDecompress(badShift, 6); }));
    // A copy before anything was written.
    const V early = {'F', 'A', 'B', 12, 0x00, 0x00, 0xFF, 0x00};
    VCR_CHECK(throwsFormatError([&] { fabDecompress(early, 4); }));
}

VCR_TEST("MADSPACK: stored and packed entries") {
    const V stored = {1, 2, 3};
    const auto file = container({{0, stored}, {1, kAbStream}}, {3, 6});
    const auto entries = readMadspack(file);
    VCR_CHECK(entries.size() == 2);
    VCR_CHECK(entries[0] == stored);
    VCR_CHECK((entries[1] == V{'A', 'B', 'A', 'B', 'A', 'B'}));
}

VCR_TEST("MADSPACK: lying headers are errors") {
    auto file = container({{0, V{1, 2, 3}}}, {3});
    file.pop_back();
    VCR_CHECK(throwsFormatError([&] { readMadspack(file); }));
    VCR_CHECK(throwsFormatError([] { readMadspack(V(20, 0)); }));
    auto many = container({}, {});
    many[14] = 200;
    VCR_CHECK(throwsFormatError([&] { readMadspack(many); }));
}

VCR_TEST("sprites: raw lines, runs and transparency") {
    // 4x3: line 1 raw "1 2 <transparent> 3", line 2 empty, line 3 runs "2x7 2x8".
    const V data = {0xFE, 1, 2, 0xFD, 3, 0xFF, 0xFF, 0xFD, 2, 7, 2, 8, 0xFF, 0xFC};
    const auto img = decodeSprite(data, 4, 3);
    const V expected = {1, 2, kTransparent, 3, kTransparent, kTransparent, kTransparent, kTransparent, 7, 7, 8, 8};
    VCR_CHECK(img.pixels == expected);
    // Raw run escape: FE count colour.
    const V run = {0xFE, 0xFE, 3, 5, 0xFF, 0xFC};
    VCR_CHECK((decodeSprite(run, 3, 1).pixels == V{5, 5, 5}));
}

VCR_TEST("sprites: damaged data is an error") {
    VCR_CHECK(throwsFormatError([] { decodeSprite(V{0xFE, 1, 2}, 4, 1); }));
    VCR_CHECK(throwsFormatError([] { decodeSprite(V{0x42, 0xFF}, 4, 1); }));
    VCR_CHECK(throwsFormatError([] { decodeSprite(V{0xFC}, 100000, 1); }));
}

VCR_TEST("palette: 6-bit VGA values scale to 0..255") {
    V raw(768, 0);
    raw[3] = 63;
    raw[4] = 32;
    const auto pal = readVgaPalette(raw);
    VCR_CHECK((pal[1] == Rgb{255, 130, 0}));
    VCR_CHECK(throwsFormatError([] { readVgaPalette(V(100, 0)); }));
}

VCR_TEST("original files: every MADSPACK file unpacks") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    int pictures = 0;
    int sheets = 0;
    int containers = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const auto data = readAll(entry.path());
        if (!isMadspack(data)) {
            continue;
        }
        ++containers;
        readMadspack(data);
        auto ext = entry.path().extension().string();
        for (auto& c : ext) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        if (ext == ".PIK") {
            const auto pic = readPicture(data);
            VCR_CHECK(pic.image.width == 320 || pic.image.width == 960);
            ++pictures;
        } else if (ext == ".SS") {
            const auto sheet = readSpriteSheet(data);
            VCR_CHECK(!sheet.sprites.empty());
            ++sheets;
        }
    }
    VCR_CHECK(containers > 200);
    VCR_CHECK(pictures > 20);
    VCR_CHECK(sheets > 150);
    const auto pal = findFileNoCase(dir, "VICEROY.PAL");
    VCR_CHECK(pal.has_value());
    readVgaPalette(readAll(*pal));
}

VCR_TEST("FAB and sprites: every single-byte change fails cleanly or decodes") {
    for (std::size_t i = 0; i < kAbStream.size(); ++i) {
        for (int value = 0; value < 256; ++value) {
            V mutated = kAbStream;
            mutated[i] = static_cast<std::uint8_t>(value);
            try {
                fabDecompress(mutated, 6);
            } catch (const FormatError&) {
            }
        }
    }
    const V sprite = {0xFE, 1, 0xFE, 3, 2, 0xFF, 0xFD, 2, 7, 0xFF, 0xFC};
    for (std::size_t i = 0; i < sprite.size(); ++i) {
        for (int value = 0; value < 256; ++value) {
            V mutated = sprite;
            mutated[i] = static_cast<std::uint8_t>(value);
            try {
                decodeSprite(mutated, 4, 2);
            } catch (const FormatError&) {
            }
        }
    }
}
