// SPDX-License-Identifier: GPL-3.0-or-later
//
// Palettes, full-screen pictures (.PIK) and sprite sheets (.SS) of the
// original game. See docs/formats/images.md.
#pragma once

#include "viceroy/data/madspack.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace viceroy {

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb&) const = default;
};

// 256 colours, already scaled from the VGA's 0..63 to 0..255.
using Palette = std::array<Rgb, 256>;

// Reads a VGA palette: 768 bytes of 6-bit red, green, blue. Longer data
// (VICEROY.PAL is 1024 bytes) is allowed; the rest is ignored.
Palette readVgaPalette(std::span<const std::uint8_t> data);

// An 8-bit picture. Pixels are palette indices, row by row.
struct IndexedImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

// A full picture with its own palette, when the file has one.
struct Picture {
    IndexedImage image;
    std::optional<Palette> palette;
};

Picture readPicture(std::span<const std::uint8_t> file);

// Palette index the sprite encoding uses for "nothing drawn here".
inline constexpr std::uint8_t kTransparent = 0xFD;

struct Sprite {
    int x = 0; // position the original artist gave the sprite on its sheet
    int y = 0;
    IndexedImage image; // transparent pixels hold kTransparent
};

struct SpriteSheet {
    std::vector<Sprite> sprites;
    std::optional<Palette> palette;
};

SpriteSheet readSpriteSheet(std::span<const std::uint8_t> file);

// Decodes one run-length encoded sprite (exposed for tests).
IndexedImage decodeSprite(std::span<const std::uint8_t> data, int width, int height);

} // namespace viceroy
