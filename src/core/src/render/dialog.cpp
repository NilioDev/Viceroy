// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/render/dialog.hpp"

#include <algorithm>
#include <sstream>

namespace viceroy {

namespace {

constexpr std::uint8_t kBlack = 0x00;
constexpr std::uint8_t kRing = 0x86;
constexpr std::uint8_t kEdgeDark = 0x80;
constexpr std::uint8_t kEdgeLight = 0x8A;
constexpr std::uint8_t kBar = 0x8A;
// @COLORS in NAMES.TXT: basic 68, hilite 149, enhance 128, shadow 47;
// font pixel values 1, 2 and 3 take the letter, enhance and shadow.
constexpr TextColours kBasic{0x44, 0x80, 0x2F};
constexpr TextColours kHilite{0x95, 0x80, 0x2F};

constexpr int kTextLeft = 5;
constexpr int kTextTop = 6;
constexpr int kLineStep = 10;
constexpr int kOptionGap = 4;
constexpr int kOptionStep = 12;
constexpr int kOptionLeft = 9;

std::string fill(std::string text, const std::vector<std::string>& strings) {
    for (std::size_t i = 0; i < strings.size(); ++i) {
        const std::string key = "%STRING" + std::to_string(i);
        for (auto at = text.find(key); at != std::string::npos; at = text.find(key, at + strings[i].size())) {
            text.replace(at, key.size(), strings[i]);
        }
    }
    return text;
}

// Width of text without the braces.
int plainWidth(const Font& font, const std::string& s) {
    std::string plain;
    for (const char c : s) {
        if (c != '{' && c != '}') {
            plain += c;
        }
    }
    return Screen::textWidth(font, plain);
}

std::vector<std::string> wrap(const Font& font, const std::string& text, int limit) {
    std::vector<std::string> lines;
    std::istringstream words(text);
    std::string word;
    std::string line;
    while (words >> word) {
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && plainWidth(font, candidate) > limit) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

// Draws a line, switching to gold between braces; returns nothing.
void drawMarkedLine(Screen& screen, const Font& font, int x, int y, const std::string& line, bool& gold) {
    std::string run;
    auto flush = [&] {
        x = screen.text(font, x, y, run, gold ? kHilite : kBasic);
        run.clear();
    };
    for (const char c : line) {
        if (c == '{' || c == '}') {
            flush();
            gold = c == '{';
        } else {
            run += c;
        }
    }
    flush();
}

int boxWidth(const Dialog& d) {
    return d.width + 6;
}

} // namespace

Dialog makeDialog(const Art& art, std::string_view section, const std::vector<std::string>& strings) {
    Dialog d;
    const auto* found = art.gameText.find(section);
    if (found == nullptr) {
        return d;
    }
    std::string text;
    bool inOptions = false;
    bool seenText = false;
    for (const auto& raw : found->lines) {
        const std::string line = fill(raw, strings);
        if (!line.empty() && line[0] == '@') {
            if (line.rfind("@width=", 0) == 0) {
                d.width = std::max(40, std::atoi(line.c_str() + 7));
            } else if (line.rfind("@default=", 0) == 0) {
                d.selected = std::max(0, std::atoi(line.c_str() + 9) - 1);
            }
            continue;
        }
        const bool blank = line.find_first_not_of(" \t") == std::string::npos;
        if (!inOptions) {
            if (blank) {
                if (seenText) {
                    inOptions = true;
                }
                continue;
            }
            seenText = true;
            text += (text.empty() ? "" : " ") + line;
        } else {
            if (blank) {
                if (!d.options.empty()) {
                    break;
                }
                continue;
            }
            d.options.push_back(line);
        }
    }
    d.lines = wrap(art.labels, text, d.width - kTextLeft - 1);
    d.selected = d.options.empty() ? 0 : std::min(d.selected, static_cast<int>(d.options.size()) - 1);
    d.x = (Screen::kWidth - boxWidth(d)) / 2;
    d.y = (Screen::kHeight - dialogHeight(d)) / 2;
    return d;
}

int dialogHeight(const Dialog& d) {
    const int textBottom = kTextTop + kLineStep * static_cast<int>(d.lines.size());
    if (d.options.empty()) {
        return textBottom + kOptionGap + 2;
    }
    return textBottom + kOptionGap + kOptionStep * static_cast<int>(d.options.size()) + 2;
}

void drawDialog(Screen& screen, const Art& art, const Dialog& d) {
    const Rect box{d.x, d.y, boxWidth(d), dialogHeight(d)};
    screen.setClip({box.x + 1, box.y + 1, box.w - 2, box.h - 2});
    const auto& wood = art.wood;
    if (wood.width > 0 && wood.height > 0) {
        for (int y = box.y; y < box.y + box.h; ++y) {
            for (int x = box.x; x < box.x + box.w; ++x) {
                screen.pixel(x, y,
                             wood.pixels[static_cast<std::size_t>(((y - box.y) % wood.height) * wood.width +
                                                                  (x - box.x) % wood.width)]);
            }
        }
    }
    screen.resetClip();
    screen.frame(box, kBlack);
    screen.frame({box.x + 1, box.y + 1, box.w - 2, box.h - 2}, kRing);
    screen.fill({box.x + 2, box.y + 2, box.w - 4, 1}, kEdgeDark);
    screen.fill({box.x + box.w - 3, box.y + 2, 1, box.h - 4}, kEdgeDark);
    screen.fill({box.x + 2, box.y + 3, 1, box.h - 5}, kEdgeLight);
    screen.fill({box.x + 2, box.y + box.h - 3, box.w - 4, 1}, kEdgeLight);

    bool gold = false;
    int y = box.y + kTextTop;
    for (const auto& line : d.lines) {
        drawMarkedLine(screen, art.labels, box.x + kTextLeft, y, line, gold);
        y += kLineStep;
    }
    y += kOptionGap;
    for (std::size_t i = 0; i < d.options.size(); ++i) {
        if (static_cast<int>(i) == d.selected) {
            screen.fill({box.x + 4, y - 1, box.w - 8, 11}, kBar);
        }
        bool optionGold = false;
        drawMarkedLine(screen, art.labels, box.x + kOptionLeft, y, d.options[i], optionGold);
        y += kOptionStep;
    }

    if (d.portrait >= 0 && static_cast<std::size_t>(d.portrait) < art.portraits.size() &&
        !art.portraits[static_cast<std::size_t>(d.portrait)].sprites.empty()) {
        screen.blit(art.portraits[static_cast<std::size_t>(d.portrait)].sprites.front().image, d.portraitX,
                    d.portraitY);
    }
}

} // namespace viceroy
