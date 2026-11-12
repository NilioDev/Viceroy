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
constexpr TextColours kGrey{0x08, 0x80, 0x2F};
constexpr std::uint8_t kGold = 0x95;

constexpr int kTextLeft = 5;
constexpr int kTextTop = 6;
constexpr int kLineStep = 10;
constexpr int kOptionGap = 4;
constexpr int kOptionStep = 12;
// With @smallfont.
constexpr int kSmallLineStep = 6;
constexpr int kSmallOptionStep = 8;
// The values on the right end this far inside the box.
constexpr int kValueRight = 13;

void replaceAll(std::string& text, const std::string& key, const std::string& value) {
    for (auto at = text.find(key); at != std::string::npos; at = text.find(key, at + value.size())) {
        text.replace(at, key.size(), value);
    }
}

std::string fill(std::string text, const std::vector<std::string>& strings, const std::vector<int>& numbers) {
    // Highest first, so %STRING1 is not taken for %STRING10.
    for (std::size_t i = strings.size(); i-- > 0;) {
        replaceAll(text, "%STRING" + std::to_string(i), strings[i]);
    }
    for (std::size_t i = numbers.size(); i-- > 0;) {
        replaceAll(text, "%NUMBER" + std::to_string(i), std::to_string(numbers[i]));
    }
    return text;
}

const Font& fontOf(const Art& art, const Dialog& d) {
    return d.small ? art.tiny : art.labels;
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
void drawMarkedLine(Screen& screen, const Font& font, int x, int y, const std::string& line, bool& gold,
                    const TextColours& colours = kBasic) {
    std::string run;
    auto flush = [&] {
        x = screen.text(font, x, y, run, gold ? kHilite : colours);
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

int markedWidth(const Font& font, const std::string& text) {
    return plainWidth(font, text);
}

std::vector<std::string> wrapMarkedText(const Font& font, const std::string& text, int limit) {
    return wrap(font, text, limit);
}

void drawMarkedText(Screen& screen, const Font& font, int x, int y, const std::string& line, bool& gold) {
    drawMarkedLine(screen, font, x, y, line, gold);
}

Dialog makeDialog(const Art& art, std::string_view section, const std::vector<std::string>& strings,
                  const std::vector<int>& numbers) {
    Dialog d;
    const auto* found = art.gameText.find(section);
    if (found == nullptr) {
        return d;
    }
    std::string text;
    bool inOptions = false;
    bool seenText = false;
    int fixedY = -1;
    for (const auto& raw : found->lines) {
        const std::string line = fill(raw, strings, numbers);
        if (!line.empty() && line[0] == '@') {
            if (line == "@smallfont") {
                d.small = true;
            } else if (line == "@options") {
                inOptions = true;
            } else if (line.rfind("@y=", 0) == 0) {
                fixedY = std::atoi(line.c_str() + 3);
            } else if (line.rfind("@width=", 0) == 0) {
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
            addOption(d, line);
        }
    }
    d.lines = wrap(fontOf(art, d), text, d.width - kTextLeft);
    d.selected = d.options.empty() ? 0 : std::min(d.selected, static_cast<int>(d.options.size()) - 1);
    d.x = (Screen::kWidth - boxWidth(d)) / 2;
    d.y = fixedY >= 0 ? fixedY : (Screen::kHeight - dialogHeight(d)) / 2;
    return d;
}

void addOption(Dialog& d, std::string text, std::string value, bool disabled) {
    d.options.push_back(std::move(text));
    d.values.push_back(std::move(value));
    d.disabled.push_back(disabled);
}

void placeEuropeDialog(Dialog& d, bool help) {
    d.help = help;
    d.optionLeft = 13;
    d.portrait = kEuropeAgent;
    d.portraitX = 99;
    d.portraitBehind = false;
    const int h = dialogHeight(d);
    d.x = (Screen::kWidth - boxWidth(d)) / 2;
    // Centred on the harbour below the agent, rows 80 to 199.
    d.y = (279 - h) / 2;
    if (d.y + h > Screen::kHeight) {
        // Too tall: centred on the screen, and the agent is not shown.
        d.y = (Screen::kHeight - h) / 2;
        d.portrait = -1;
    }
    // The agent leans on the box with his hands over its top edge.
    d.portraitY = d.y - 78;
}

void stepSelection(Dialog& d, int by) {
    const int n = static_cast<int>(d.options.size());
    for (int k = 1; k <= n; ++k) {
        const int at = ((d.selected + by * k) % n + n) % n;
        if (static_cast<std::size_t>(at) >= d.disabled.size() || !d.disabled[static_cast<std::size_t>(at)]) {
            d.selected = at;
            return;
        }
    }
}

int dialogHeight(const Dialog& d) {
    const int lineStep = d.small ? kSmallLineStep : kLineStep;
    const int optionStep = d.small ? kSmallOptionStep : kOptionStep;
    const int textBottom = kTextTop + lineStep * static_cast<int>(d.lines.size());
    const int helpRoom = d.help ? (d.small ? 6 : 3) : 0;
    if (d.options.empty()) {
        return textBottom + kOptionGap + 2 + helpRoom;
    }
    return textBottom + kOptionGap + optionStep * static_cast<int>(d.options.size()) + 2 + helpRoom;
}

int dialogOptionAt(const Dialog& d, int x, int y) {
    const Rect box{d.x, d.y, boxWidth(d), dialogHeight(d)};
    if (x < box.x + 4 || x >= box.x + box.w - 4) {
        return -1;
    }
    int top = box.y + kTextTop + static_cast<int>(d.lines.size()) * (d.small ? kSmallLineStep : kLineStep) + kOptionGap;
    const int step = d.small ? kSmallOptionStep : kOptionStep;
    for (int i = 0; i < static_cast<int>(d.options.size()); ++i) {
        if (y >= top - 1 && y < top - 1 + step) {
            return i;
        }
        top += step;
    }
    return -1;
}

void drawDialog(Screen& screen, const Art& art, const Dialog& d) {
    const Rect box{d.x, d.y, boxWidth(d), dialogHeight(d)};
    const bool hasPortrait = d.portrait >= 0 && static_cast<std::size_t>(d.portrait) < art.portraits.size() &&
                             !art.portraits[static_cast<std::size_t>(d.portrait)].sprites.empty();
    if (hasPortrait) {
        // The portraits' own colours: palette entries 152 to 251.
        const auto& sheet = art.portraits[static_cast<std::size_t>(d.portrait)];
        if (sheet.palette) {
            screen.setColours(*sheet.palette, 152, 251);
        }
    }
    if (hasPortrait && d.portraitBehind) {
        screen.blit(art.portraits[static_cast<std::size_t>(d.portrait)].sprites.front().image, d.portraitX,
                    d.portraitY);
    }
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

    const Font& font = fontOf(art, d);
    bool gold = false;
    int y = box.y + kTextTop;
    for (const auto& line : d.lines) {
        drawMarkedLine(screen, font, box.x + kTextLeft, y, line, gold);
        y += d.small ? kSmallLineStep : kLineStep;
    }
    y += kOptionGap;
    for (std::size_t i = 0; i < d.options.size(); ++i) {
        if (static_cast<int>(i) == d.selected) {
            screen.fill({box.x + 4, y - 1, box.w - 8, d.small ? 7 : 11}, kBar);
        }
        const bool grey = i < d.disabled.size() && d.disabled[i];
        const TextColours& colours = grey ? kGrey : kBasic;
        bool optionGold = false;
        drawMarkedLine(screen, font, box.x + d.optionLeft, y, d.options[i], optionGold, colours);
        if (i < d.values.size() && !d.values[i].empty()) {
            const int right = box.x + box.w - kValueRight;
            screen.text(font, right - Screen::textWidth(font, d.values[i]), y, d.values[i], colours);
        }
        y += d.small ? kSmallOptionStep : kOptionStep;
    }
    if (d.help) {
        const std::string help = "(F1 for Help)";
        screen.text(art.tiny, box.x + box.w - 2 - Screen::textWidth(art.tiny, help), box.y + box.h - (d.small ? 9 : 7), help,
                    {kGold, kNoColour, kNoColour});
    }

    if (hasPortrait && !d.portraitBehind) {
        screen.blit(art.portraits[static_cast<std::size_t>(d.portrait)].sprites.front().image, d.portraitX,
                    d.portraitY);
    }
}

} // namespace viceroy
