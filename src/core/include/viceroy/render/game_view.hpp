// SPDX-License-Identifier: GPL-3.0-or-later
//
// Draws the main game screen the way the original does: the menu bar, the
// map window of 15×12 squares, and the information panel on the right.
// All pictures come from the player's own copy of the game (Art).
#pragma once

#include "viceroy/data/font.hpp"
#include "viceroy/data/image.hpp"
#include "viceroy/data/text_table.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/render/screen.hpp"

#include <filesystem>
#include <string>

namespace viceroy {

struct Art {
    Palette palette;
    SpriteSheet terrain; // TERRAIN.SS: base squares
    SpriteSheet phys;    // PHYS0.SS: forests, hills, rivers, roads, fog
    SpriteSheet icons;   // ICONS.SS: units, colonies, settlements, goods
    IndexedImage wood;   // WOODTILE.SS: the wooden background
    Font tiny;           // FONTTINY.FF
    Font small;          // FONTSMAL.FF
    Font labels;         // FONTINTR.FF: names on the map
    TextFile names;      // NAMES.TXT
    TextFile colonyNames; // COLONY.TXT
    Ruleset rules;
};

// Loads everything from the imported original files. Throws FormatError.
Art loadArt(const std::filesystem::path& folder);

// The map window and the panel.
inline constexpr int kViewColumns = 15;
inline constexpr int kViewRows = 12;
inline constexpr int kSquare = 16;
inline constexpr Rect kMapArea{0, 8, kViewColumns * kSquare, kViewRows * kSquare};
inline constexpr Rect kPanelArea{241, 8, 79, 192};

struct ViewState {
    int originX = 0; // map square shown at the top-left of the map window
    int originY = 0;
    bool blinkOn = true; // the active unit flashes
    std::string message; // a line shown at the bottom of the panel
};

// Puts the square in the middle of the map window, as the original does
// with the active unit.
void centreOn(ViewState& view, const Game& game, int x, int y);

void drawGameScreen(Screen& screen, const Art& art, const Game& game, const ViewState& view);

// Pieces, exposed for tests and other screens.
void drawMap(Screen& screen, const Art& art, const Game& game, const ViewState& view);
void drawPanel(Screen& screen, const Art& art, const Game& game, const ViewState& view);
void drawMenuBar(Screen& screen, const Art& art);
// The sprite in ICONS.SS for a unit.
int unitIcon(const Game& game, int index);
std::uint8_t nationColour(const Art& art, int nation);

} // namespace viceroy
