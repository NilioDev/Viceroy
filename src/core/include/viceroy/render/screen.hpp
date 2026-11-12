// SPDX-License-Identifier: GPL-3.0-or-later
//
// An 8-bit screen like the original's 320×200 VGA mode: every pixel is a
// palette index. Everything is drawn here first; the app only turns it into
// colours at the end, so what we draw matches the original pixel for pixel.
#pragma once

#include "viceroy/data/font.hpp"
#include "viceroy/data/image.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace viceroy {

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// The three colours a font's pixel values 1, 2 and 3 are drawn with;
// kNoColour means "don't draw".
inline constexpr int kNoColour = -1;
using TextColours = std::array<int, 3>;

class Screen {
public:
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 200;

    Screen() : pixels_(static_cast<std::size_t>(kWidth * kHeight), 0) {}

    const std::vector<std::uint8_t>& pixels() const { return pixels_; }
    std::uint8_t at(int x, int y) const { return pixels_[static_cast<std::size_t>(y * kWidth + x)]; }

    void setClip(Rect clip);
    void resetClip() { clip_ = {0, 0, kWidth, kHeight}; }

    void clear(std::uint8_t colour);
    void fill(Rect r, std::uint8_t colour);
    void frame(Rect r, std::uint8_t colour);
    void pixel(int x, int y, std::uint8_t colour);
    // Draws an image; kTransparent pixels are skipped.
    void blit(const IndexedImage& image, int x, int y);
    // Repeats an image over a rectangle, lined up with the screen's origin.
    void tile(const IndexedImage& image, Rect r);
    // Returns the x just after the text.
    int text(const Font& font, int x, int y, std::string_view s, TextColours colours);
    static int textWidth(const Font& font, std::string_view s);

    // Portraits bring their own colours for part of the palette, as the
    // original loads them with the picture.
    void setColours(const Palette& palette, int first, int last);
    void resetColours() { overridden_.fill(false); }
    Rgb colour(const Palette& base, std::uint8_t index) const {
        return overridden_[index] ? colours_[index] : base[index];
    }

private:
    bool inClip(int x, int y) const {
        return x >= clip_.x && y >= clip_.y && x < clip_.x + clip_.w && y < clip_.y + clip_.h;
    }

    std::vector<std::uint8_t> pixels_;
    Rect clip_{0, 0, kWidth, kHeight};
    Palette colours_{};
    std::array<bool, 256> overridden_{};
};

} // namespace viceroy
