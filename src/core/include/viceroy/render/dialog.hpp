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
    int selected = 0;
    int x = 62;      // the box's top-left corner
    int y = 20;
    int width = 190; // as GAME.TXT gives it; the box is 6 wider
    int portrait = -1; // an advisor (kNavalAdvisor, ...) or -1
    int portraitX = 0;
    int portraitY = 0;
};

// Portraits in Art::portraits.
inline constexpr int kNavalAdvisor = 0; // MSS0.SS

// The box for a GAME.TXT section, with %STRING0, %STRING1, ... filled in.
// Text is joined and wrapped to the section's @width. A missing section
// gives an empty box.
Dialog makeDialog(const Art& art, std::string_view section, const std::vector<std::string>& strings = {});

// The box's height in pixels.
int dialogHeight(const Dialog& dialog);

void drawDialog(Screen& screen, const Art& art, const Dialog& dialog);

} // namespace viceroy
