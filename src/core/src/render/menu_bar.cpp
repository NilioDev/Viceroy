// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/render/menu_bar.hpp"

#include "viceroy/render/game_view.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace viceroy {
namespace {

// Measured on captures of the original.
constexpr std::uint8_t kBlack = 0x00;
constexpr std::uint8_t kGreen = 0x44;
constexpr std::uint8_t kGold = 0x95;
constexpr std::uint8_t kGrey = 0x08;
constexpr std::uint8_t kDark = 0x8A; // the open title and the highlighted entry
constexpr int kBarHeight = 7;
constexpr int kFirstTitle = 13;
constexpr int kTitleGap = 14;
constexpr int kLastTitleRight = 307; // the last title ends here
constexpr int kBoxTop = 9;
constexpr int kLine = 8;      // one entry, or one line between groups
constexpr int kTextLeft = 5;  // from the box's left edge
constexpr int kTextTop = 4;   // from the box's top edge to the first entry
constexpr int kBoxPadding = 10;
constexpr int kRightLimit = 318; // a box is pushed left to end before this

struct Layout {
    std::string_view section;
    std::vector<int> groupEnds;
};

// Where the lines between groups go; MENU.TXT does not say.
const std::array<Layout, 6>& layouts() {
    static const std::array<Layout, 6> kLayouts{{
        {"GAME", {1, 3, 5, 6}},
        {"VIEW", {2, 3, 5, 9}},
        {"ORDERS", {4, 12, 16, 17}}, // seen for a frigate and a pioneer
        {"REPORTS", {0, 4, 8}},
        {"TRADE", {}},
        {"PEDIA", {2}},
    }};
    return kLayouts;
}

std::string trim(std::string_view s) {
    const auto first = s.find_first_not_of(' ');
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(' ');
    return std::string(s.substr(first, last - first + 1));
}

// Draws menu text: '~' turns the next letter gold, '#' is a blank as wide
// as a figure. Returns the x after the text.
int drawMarked(Screen& screen, const Art& art, int x, int y, const std::string& text, std::uint8_t colour,
               std::uint8_t marked) {
    bool gold = false;
    for (const char ch : text) {
        if (ch == '~') {
            gold = true;
            continue;
        }
        if (ch == '#') {
            x += Screen::textWidth(art.tiny, "0");
            continue;
        }
        const std::string one(1, ch);
        x = screen.text(art.tiny, x, y, one, {gold ? marked : colour, kNoColour, kNoColour});
        gold = false;
    }
    return x;
}

// The wood of a box starts at its top-left corner.
void woodFrom(Screen& screen, const IndexedImage& wood, Rect r) {
    if (wood.width <= 0 || wood.height <= 0) {
        return;
    }
    for (int y = 0; y < r.h; ++y) {
        for (int x = 0; x < r.w; ++x) {
            const auto c = wood.pixels[static_cast<std::size_t>((y % wood.height) * wood.width + (x % wood.width))];
            if (c != kTransparent) {
                screen.pixel(r.x + x, r.y + y, c);
            }
        }
    }
}

int titleWidth(const Art& art, const MenuDef& def) {
    return menuTextWidth(art, def.title);
}

struct Row {
    int entry = -1; // -1: a line between groups
};

std::vector<Row> rows(const MenuDef& def, const OpenMenu& open) {
    std::vector<Row> out;
    bool pendingLine = false;
    for (int i = 0; i < static_cast<int>(def.entries.size()); ++i) {
        const auto state = i < static_cast<int>(open.states.size()) ? open.states[static_cast<std::size_t>(i)]
                                                                    : EntryState::Enabled;
        if (state != EntryState::Hidden) {
            if (pendingLine && !out.empty()) {
                out.push_back({-1});
            }
            pendingLine = false;
            out.push_back({i});
        }
        if (std::find(def.groupEnds.begin(), def.groupEnds.end(), i) != def.groupEnds.end()) {
            pendingLine = true;
        }
    }
    return out;
}

} // namespace

int menuTextWidth(const Art& art, const std::string& text) {
    int w = 0;
    for (const char ch : text) {
        if (ch == '~') {
            continue;
        }
        w += Screen::textWidth(art.tiny, ch == '#' ? std::string("0") : std::string(1, ch));
    }
    return w;
}

std::vector<MenuDef> readMenus(const TextFile& menuText) {
    std::vector<MenuDef> menus;
    for (const auto& layout : layouts()) {
        const auto* section = menuText.find(layout.section);
        if (section == nullptr) {
            continue;
        }
        MenuDef def;
        def.section = std::string(layout.section);
        def.groupEnds = layout.groupEnds;
        bool first = true;
        for (const auto& line : section->lines) {
            const std::string text = trim(line);
            if (text.empty()) {
                continue;
            }
            if (first) {
                def.title = text;
                first = false;
                continue;
            }
            MenuEntry entry;
            entry.text = text;
            const auto mark = text.find('~');
            if (mark != std::string::npos && mark + 1 < text.size()) {
                entry.hotkey = static_cast<char>(std::toupper(static_cast<unsigned char>(text[mark + 1])));
            }
            def.entries.push_back(std::move(entry));
        }
        menus.push_back(std::move(def));
    }
    return menus;
}

void placeMenus(const Art& art, std::vector<MenuDef>& menus) {
    int x = kFirstTitle;
    for (std::size_t i = 0; i < menus.size(); ++i) {
        const int w = titleWidth(art, menus[i]);
        // The encyclopedia stands apart at the right end of the bar.
        menus[i].x = (i + 1 == menus.size() && i > 0) ? kLastTitleRight - w : x;
        x += w + kTitleGap;
    }
}

Rect menuBox(const Art& art, const MenuDef& def, const OpenMenu& open) {
    int widest = 0;
    for (const auto& e : def.entries) {
        widest = std::max(widest, menuTextWidth(art, e.text));
    }
    const int w = widest + kBoxPadding;
    const int x = std::min(def.x - 1, kRightLimit - w);
    const int h = static_cast<int>(rows(def, open).size()) * kLine + kTextTop + 1;
    return {x, kBoxTop, w, h};
}

int menuEntryAt(const Art& art, const MenuDef& def, const OpenMenu& open, int x, int y) {
    const Rect box = menuBox(art, def, open);
    if (x <= box.x || x >= box.x + box.w - 1 || y <= box.y) {
        return -1;
    }
    const auto list = rows(def, open);
    const int slot = (y - (box.y + kTextTop - 1)) / kLine;
    if (y < box.y + kTextTop - 1 || slot < 0 || slot >= static_cast<int>(list.size())) {
        return -1;
    }
    return list[static_cast<std::size_t>(slot)].entry;
}

int menuTitleAt(const Art& art, const std::vector<MenuDef>& menus, int x, int y) {
    if (y < 0 || y >= kBarHeight + 1) {
        return -1;
    }
    for (std::size_t i = 0; i < menus.size(); ++i) {
        if (x >= menus[i].x - 1 && x < menus[i].x + titleWidth(art, menus[i]) + 3) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void drawMenuBar(Screen& screen, const Art& art, const std::vector<MenuDef>& menus, const OpenMenu* open) {
    screen.tile(art.wood, {0, 0, Screen::kWidth, kBarHeight});
    screen.fill({0, kBarHeight, Screen::kWidth, 1}, kBlack);
    for (std::size_t i = 0; i < menus.size(); ++i) {
        const auto& def = menus[i];
        if (open != nullptr && open->menu == static_cast<int>(i)) {
            screen.fill({def.x - 1, 0, titleWidth(art, def) + 4, kBarHeight}, kDark);
        }
        drawMarked(screen, art, def.x, 1, def.title, kGreen, kGold);
    }
    if (open == nullptr || open->menu < 0 || open->menu >= static_cast<int>(menus.size())) {
        return;
    }
    const auto& def = menus[static_cast<std::size_t>(open->menu)];
    const Rect box = menuBox(art, def, *open);
    woodFrom(screen, art.wood, box);
    screen.frame(box, kBlack);
    int y = box.y + kTextTop;
    for (const auto& row : rows(def, *open)) {
        if (row.entry < 0) {
            screen.fill({box.x + 1, y + 2, box.w - 2, 1}, kGreen);
        } else {
            const auto& entry = def.entries[static_cast<std::size_t>(row.entry)];
            const bool enabled = row.entry >= static_cast<int>(open->states.size()) ||
                                 open->states[static_cast<std::size_t>(row.entry)] == EntryState::Enabled;
            if (row.entry == open->selected) {
                screen.fill({box.x + 2, y - 1, box.w - 4, kLine - 1}, kDark);
            }
            drawMarked(screen, art, box.x + kTextLeft, y, entry.text, enabled ? kGreen : kGrey,
                       enabled ? kGold : kGrey);
        }
        y += kLine;
    }
}

} // namespace viceroy
