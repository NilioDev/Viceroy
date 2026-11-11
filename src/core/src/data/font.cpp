// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/data/font.hpp"

#include "viceroy/data/madspack.hpp"
#include "viceroy/data/reader.hpp"

namespace viceroy {

namespace {

constexpr std::size_t kWidthTable = 1;   // width of character c is at 1 + c
constexpr std::size_t kOffsetTable = 128; // offset of character c is at 128 + 2c
// The width table and the offset table meet at byte 128, so the last
// character with a glyph is 126.
constexpr std::size_t kLastChar = 126;
constexpr int kMaxHeight = 64;
constexpr int kMaxWidth = 64;

} // namespace

Font decodeFont(std::span<const std::uint8_t> data) {
    Reader in(data);
    Font font;
    font.height = in.u8();
    font.maxWidth = in.u8();
    if (font.height < 1 || font.height > kMaxHeight || font.maxWidth > kMaxWidth) {
        throw FormatError("font with impossible size");
    }
    for (std::size_t c = 1; c <= kLastChar; ++c) {
        in.seek(kWidthTable + c);
        const int width = in.u8();
        in.seek(kOffsetTable + 2 * c);
        const std::size_t offset = in.u16();
        if (width > kMaxWidth) {
            throw FormatError("glyph wider than allowed");
        }
        Glyph& glyph = font.glyphs[c];
        glyph.width = width;
        glyph.pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(font.height), 0);
        if (width == 0) {
            continue;
        }
        // Two bits per pixel, leftmost pixel in the highest bits; each row
        // starts on a new byte.
        const std::size_t rowBytes = (static_cast<std::size_t>(width) * 2 + 7) / 8;
        in.seek(offset);
        for (int y = 0; y < font.height; ++y) {
            const auto row = in.bytes(rowBytes);
            for (int x = 0; x < width; ++x) {
                const auto bit = static_cast<unsigned>(x) * 2;
                const auto value = (row[bit / 8] >> (6 - bit % 8)) & 3u;
                glyph.pixels[static_cast<std::size_t>(y * width + x)] = static_cast<std::uint8_t>(value);
            }
        }
    }
    return font;
}

Font readFont(std::span<const std::uint8_t> file) {
    const auto entries = readMadspack(file);
    if (entries.empty()) {
        throw FormatError("font file without data");
    }
    return decodeFont(entries[0]);
}

} // namespace viceroy
