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
#include "viceroy/render/menu_bar.hpp"
#include "viceroy/render/screen.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace viceroy {

struct Art {
    Palette palette;
    SpriteSheet terrain; // TERRAIN.SS: base squares
    SpriteSheet phys;    // PHYS0.SS: forests, hills, rivers, roads, fog
    SpriteSheet icons;   // ICONS.SS: units, colonies, settlements, goods
    IndexedImage wood;   // WOODTILE.SS: the wooden background
    SpriteSheet cursor;  // CURSOR.SS: the mouse pointers
    Font tiny;           // FONTTINY.FF
    Font small;          // FONTSMAL.FF
    Font labels;         // FONTINTR.FF: names on the map
    TextFile names;      // NAMES.TXT
    TextFile colonyNames; // COLONY.TXT
    TextFile labelText;   // LABELS.TXT: words on the screens
    TextFile gameText;    // GAME.TXT: the messages
    std::vector<MenuDef> menus; // MENU.TXT: the menu bar
    std::vector<SpriteSheet> portraits; // advisors: MSS0.SS, ...
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
    // A unit gliding into its square: drawn on top, this many pixels away
    // from the square it is moving to.
    int slideUnit = -1;
    int slideX = 0;
    int slideY = 0;
    std::string message; // a line shown at the bottom of the panel
    // With no unit to move, the panel tells about this square (the view
    // cursor's, or at the end of the turn the last unit's) and its units.
    int lookX = -1;
    int lookY = -1;
    bool endOfTurn = false; // "End of Turn" below that
    // While the messages of a turn's end are up, the panel still shows the
    // turn that ended (seen); -1 for the game's own date.
    int shownYear = -1;
    int shownSeason = 0;
};

// Puts the square in the middle of the map window, as the original does
// with the active unit.
void centreOn(ViewState& view, const Game& game, int x, int y);

// The small map of the world in the panel, frame included: map columns 1 to
// 56 and 39 rows around the view, one pixel each.
inline constexpr Rect kMinimapArea{251, 8, 58, 41};
int minimapTop(const Game& game, const ViewState& view);
// The map square under a point of the small map, if any.
std::optional<std::pair<int, int>> minimapSquare(const Game& game, const ViewState& view, int x, int y);

void drawGameScreen(Screen& screen, const Art& art, const Game& game, const ViewState& view);

// Pieces, exposed for tests and other screens.
void drawMap(Screen& screen, const Art& art, const Game& game, const ViewState& view);
void drawPanel(Screen& screen, const Art& art, const Game& game, const ViewState& view);
// The panel's lines about ViewState::lookX/lookY when no unit is active.
void drawSquarePanel(Screen& screen, const Art& art, const Game& game, const ViewState& view);
void drawMenuBar(Screen& screen, const Art& art);
// The sprite in ICONS.SS for a unit.
int unitIcon(const Game& game, int index);
// "Spring" or "Autumn" from NAMES.TXT.
std::string seasonName(const Art& art, int season);
// The sprite in ICONS.SS for a colonist with this skill.
int colonistIcon(int skill);
// One map square: terrain, forest, hills, rivers, roads, coast; no units.
void drawTerrain(Screen& screen, const Art& art, const Game& game, int x, int y, int px, int py);
// Draws a sprite of a sheet if it exists.
void blitSprite(Screen& screen, const SpriteSheet& sheet, int index, int x, int y);
std::uint8_t nationColour(const Art& art, int nation);
// The box with the nation's colour and the orders letter beside a unit
// drawn at (px, py).
void drawUnitOrders(Screen& screen, const Art& art, const Game& game, int index, int px, int py);
// A colony's colonist standing up to defend it, like a unit at (px, py):
// in plain clothes, or with a musket when the colony arms them.
void drawColonyDefender(Screen& screen, const Art& art, const Game& game, int colony, int colonist, bool armed,
                        int px, int py);
// A colony's map picture (with its stockade) and pennant at (px, py).
void drawColonyPicture(Screen& screen, const Art& art, const SaveColony& colony, int px, int py);
// A native settlement as on the map, with its mission's cross.
void drawSettlementPicture(Screen& screen, const Art& art, const SaveSettlement& settlement, int px, int py);
// A village's own brave in the combat analysis.
void drawBraveDefender(Screen& screen, const Art& art, const Game& game, int type, int nation, int px, int py);
// The same in the colony's list of units present: a unit without orders
// shows as on sentry, and artillery has a smaller picture.
void drawUnitInColonyList(Screen& screen, const Art& art, const Game& game, int index, int px, int py);
// A unit standing small on the horizon of the Units Present panel, centred on
// px; nothing is drawn from x `right` on.
void drawSmallUnit(Screen& screen, const Art& art, const Game& game, int index, int px, int py, int right);
// The first field of a row of a section of a text file, or `fallback`.
std::string textLine(const TextFile& file, const char* section, int row, const char* fallback);

} // namespace viceroy
