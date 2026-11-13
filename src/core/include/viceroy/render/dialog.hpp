// SPDX-License-Identifier: GPL-3.0-or-later
//
// The original's message boxes: text from GAME.TXT in a wooden box, words
// in {braces} in gold, and the choices below with a bar on the chosen one.
// An advisor's portrait may stand beside the box. Measurements are in
// docs/rules/dialogs.md.
#pragma once

#include "viceroy/render/game_view.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace viceroy {

struct Dialog {
    std::vector<std::string> lines;   // wrapped text, still with {braces}
    std::vector<std::string> options; // the choices, top to bottom
    std::vector<std::string> values;  // beside a choice, on the right: "(Cost: 600)"
    std::vector<bool> disabled;       // greyed choices the cursor skips
    int selected = 0;
    bool small = false; // @smallfont: FONTTINY, for long lists
    bool help = false;  // "(F1 for Help)" in the corner
    int optionLeft = 9;
    bool portraitBehind = false; // the box covers the portrait
    int x = 62;      // the box's top-left corner
    int y = 20;
    int width = 190; // as GAME.TXT gives it; the box is 6 wider
    int portrait = -1; // an advisor (kNavalAdvisor, ...) or -1
    int indent = 0;    // room on the left for a unit's picture: the box is that much wider
    int unit = -1;     // the unit whose picture stands there (drawn by the screen)
    int portraitX = 0;
    int portraitY = 0;
};

// Portraits in Art::portraits.
inline constexpr int kNavalAdvisor = 0; // MSS0.SS
inline constexpr int kEuropeAgent = 1;  // MSS2.SS: the man in the home port
inline constexpr int kFrontiersman = 2; // MSS3.SS: tells about pioneers' work
inline constexpr int kReligiousAdvisor = 3; // MSS4.SS: immigration
inline constexpr int kColonialAdvisor = 4;  // MSS1.SS: Sons of Liberty
inline constexpr int kBuyAdvisor = 5;       // MSS5.SS: buying a construction
inline constexpr int kNativePortraits = 6;  // IND0A0.SS to IND7A3.SS: four moods per tribe

// The box for a GAME.TXT section, with %STRING0, %STRING1, ... and
// %NUMBER0, ... filled in. Text is joined and wrapped to the section's
// @width. A missing section gives an empty box.
// %COUNTRY is the nation's country ("the Netherlands" for the Dutch) when
// `nation` is given.
Dialog makeDialog(const Art& art, std::string_view section, const std::vector<std::string>& strings = {},
                  const std::vector<int>& numbers = {}, int nation = -1);

// Adds a choice; a value goes on the right.
void addOption(Dialog& dialog, std::string text, std::string value = {}, bool disabled = false);

// The questions asked in the home port: the agent stands behind the box,
// which is centred on the harbour (or on the screen when it is too tall).
void placeEuropeDialog(Dialog& dialog, bool help = true);

// Messages about the land (a pioneer's work): the box centred on row 144,
// the frontiersman standing behind it.
void placeFrontiersmanDialog(Dialog& dialog);

// Religious unrest brings an immigrant: the box centred on row 126, the
// religious adviser over its top edge.
void placeReligiousDialog(Dialog& dialog);
// A report about a colony (Sons of Liberty): the box at x 39, its middle
// at row 139, the man with the musket at (210, top - 77) in front of it.
void placeColonialDialog(Dialog& dialog);
// The same man for news on the map (a battle, a treaty, an offer of
// mercenaries): the box at x 54, its middle at row 139.
void placeMapDialog(Dialog& dialog);
// BUY on the colony screen: the box centred across the screen with its
// bottom at row 163, the woman in the white cap leaning on its top edge.
// The same woman for the warehouse and food reports about a colony: the box
// centred across the screen and on row 130.
void placeTradeDialog(Dialog& d);
void placeBuyDialog(Dialog& dialog);
// Options for a unit in a colony: the unit's picture at the top left, the
// text beside it, the box centred on the screen.
void placeUnitDialog(Dialog& dialog, int unit);
// A box centred on the screen with nobody beside it (the greeting at a
// native settlement).
void placeCentredDialog(Dialog& dialog);
// A native of the tribe beside the box: what a settlement says. The mood
// is the step of the tribe's tension toward the player (0 calm to 3
// furious); it picks the figure IND<tribe>A<mood>.SS.
void placeNativeDialog(Dialog& dialog, int tribe, int mood = 0);
// The naval advisor over the left end of the box (sea matters).
void placeNavalDialog(Dialog& dialog);
// The tutorial's hints: the naval advisor, his hands over the box.
void placeTutorialDialog(Dialog& dialog);
// The name of a European nation as the messages use it ("the Netherlands").
std::string countryName(const Art& art, int nation);

// The wooden box with its frame that messages are drawn on.
void drawWoodBox(Screen& screen, const Art& art, Rect box);
// The Combat Analysis box shown before a fight when the game option is on.
void drawCombatAnalysis(Screen& screen, const Art& art, const Game& game, const CombatAnalysis& analysis);

// Moves the bar to the next choice that is not greyed, by +1 or -1.
void stepSelection(Dialog& dialog, int by);

// The box's height in pixels.
int dialogHeight(const Dialog& dialog);

// The choice under a point of the screen, or -1.
int dialogOptionAt(const Dialog& dialog, int x, int y);

void drawDialog(Screen& screen, const Art& art, const Dialog& dialog);

// The pieces boxes are made of, for screens that lay out GAME.TXT text
// themselves: the width of text without its {braces}, wrapping (a line
// breaks when the next word would make it wider than `limit`), and drawing
// in the box colours, gold between braces (`gold` carries over lines).
int markedWidth(const Font& font, const std::string& text);
std::vector<std::string> wrapMarkedText(const Font& font, const std::string& text, int limit);
void drawMarkedText(Screen& screen, const Font& font, int x, int y, const std::string& line, bool& gold);

} // namespace viceroy
