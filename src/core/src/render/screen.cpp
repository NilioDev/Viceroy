// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/render/screen.hpp"

#include <algorithm>

namespace viceroy {

void Screen::setClip(Rect clip) {
    const int x0 = std::clamp(clip.x, 0, kWidth);
    const int y0 = std::clamp(clip.y, 0, kHeight);
    const int x1 = std::clamp(clip.x + clip.w, 0, kWidth);
    const int y1 = std::clamp(clip.y + clip.h, 0, kHeight);
    clip_ = {x0, y0, x1 - x0, y1 - y0};
}

void Screen::clear(std::uint8_t colour) {
    std::fill(pixels_.begin(), pixels_.end(), colour);
}

void Screen::pixel(int x, int y, std::uint8_t colour) {
    if (inClip(x, y)) {
        pixels_[static_cast<std::size_t>(y * kWidth + x)] = colour;
    }
}

void Screen::fill(Rect r, std::uint8_t colour) {
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            pixel(x, y, colour);
        }
    }
}

void Screen::frame(Rect r, std::uint8_t colour) {
    fill({r.x, r.y, r.w, 1}, colour);
    fill({r.x, r.y + r.h - 1, r.w, 1}, colour);
    fill({r.x, r.y, 1, r.h}, colour);
    fill({r.x + r.w - 1, r.y, 1, r.h}, colour);
}

void Screen::blit(const IndexedImage& image, int x, int y) {
    for (int yy = 0; yy < image.height; ++yy) {
        for (int xx = 0; xx < image.width; ++xx) {
            const auto c = image.pixels[static_cast<std::size_t>(yy * image.width + xx)];
            if (c != kTransparent) {
                pixel(x + xx, y + yy, c);
            }
        }
    }
}

void Screen::tile(const IndexedImage& image, Rect r) {
    if (image.width <= 0 || image.height <= 0) {
        return;
    }
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            const auto c = image.pixels[static_cast<std::size_t>((y % image.height) * image.width + (x % image.width))];
            if (c != kTransparent) {
                pixel(x, y, c);
            }
        }
    }
}

int Screen::text(const Font& font, int x, int y, std::string_view s, TextColours colours) {
    for (const char ch : s) {
        const auto code = static_cast<unsigned char>(ch);
        if (code >= font.glyphs.size()) {
            continue;
        }
        const auto& glyph = font.glyphs[code];
        for (int yy = 0; yy < font.height; ++yy) {
            for (int xx = 0; xx < glyph.width; ++xx) {
                const auto v = glyph.pixels[static_cast<std::size_t>(yy * glyph.width + xx)];
                if (v != 0 && colours[v - 1u] >= 0) {
                    pixel(x + xx, y + yy, static_cast<std::uint8_t>(colours[v - 1u]));
                }
            }
        }
        x += glyph.width;
    }
    return x;
}

int Screen::textWidth(const Font& font, std::string_view s) {
    int w = 0;
    for (const char ch : s) {
        const auto code = static_cast<unsigned char>(ch);
        if (code < font.glyphs.size()) {
            w += font.glyphs[code].width;
        }
    }
    return w;
}

} // namespace viceroy
