// SPDX-License-Identifier: GPL-3.0-or-later
//
// The menu bar along the top of the map screen and the menus that drop
// from it, read from MENU.TXT and drawn as the original draws them. See
// docs/rules/menus.md.
#pragma once

#include "viceroy/data/text_table.hpp"
#include "viceroy/render/screen.hpp"

#include <string>
#include <vector>

namespace viceroy {

struct Art;

struct MenuEntry {
    std::string text; // as in MENU.TXT: '~' marks a gold letter, '#' a figure-wide space
    char hotkey = 0;  // the first gold letter, upper case; 0 for none
};

struct MenuDef {
    std::string section; // GAME, VIEW, ORDERS, REPORTS, TRADE, PEDIA
    std::string title;   // as in MENU.TXT, with '~'
    int x = 0;           // where the title stands on the bar
    std::vector<MenuEntry> entries;
    std::vector<int> groupEnds; // entries after which a line follows
};

// The six menus of the bar, in order. Missing sections give no menu.
std::vector<MenuDef> readMenus(const TextFile& menuText);
// Sets where each title stands on the bar (it depends on the font).
void placeMenus(const Art& art, std::vector<MenuDef>& menus);

enum class EntryState { Hidden, Disabled, Enabled };

struct OpenMenu {
    int menu = 0;     // index into the menus
    int selected = -1; // the highlighted entry, -1 for none
    std::vector<EntryState> states; // one per entry
};

// The box of an open menu.
Rect menuBox(const Art& art, const MenuDef& def, const OpenMenu& open);
// The entry under a point of the screen, or -1.
int menuEntryAt(const Art& art, const MenuDef& def, const OpenMenu& open, int x, int y);
// The menu whose title is under a point of the bar, or -1.
int menuTitleAt(const Art& art, const std::vector<MenuDef>& menus, int x, int y);

// Width in pixels of menu text, without its marks.
int menuTextWidth(const Art& art, const std::string& text);

void drawMenuBar(Screen& screen, const Art& art, const std::vector<MenuDef>& menus, const OpenMenu* open);

} // namespace viceroy
