// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bitmap fonts of the original game (*.FF). See docs/formats/fonts.md.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace viceroy {

struct Glyph {
    int width = 0;
    // width × font height values, row by row: 0 is transparent, 1 to 3 pick
    // one of the three colours the text is drawn with.
    std::vector<std::uint8_t> pixels;
};

struct Font {
    int height = 0;
    int maxWidth = 0;
    std::array<Glyph, 128> glyphs; // by character code; only 1 to 126 have glyphs
};

Font readFont(std::span<const std::uint8_t> file);

// Same as readFont, for the unpacked contents of the container.
Font decodeFont(std::span<const std::uint8_t> data);

} // namespace viceroy
