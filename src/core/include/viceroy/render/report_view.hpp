// SPDX-License-Identifier: GPL-3.0-or-later
//
// The reports of the REPORTS menu, drawn as the original draws them.
#pragma once

#include "viceroy/game/hall_of_fame.hpp"
#include "viceroy/render/game_view.hpp"
#include "viceroy/render/screen.hpp"

namespace viceroy {

class Game;

// A row of one icon standing for `count` things, `have` of them drawn,
// squeezed into `width` pixels as the original's bell bar is (the number of
// those drawn on a black box at the left when they are packed tight).
void drawIconBar(Screen& screen, const Art& art, int icon, int count, int have, int x, int y, int width);

// Groups of icons side by side, each icon standing for one thing, squeezed
// into `width` pixels with `gap` pixels between the groups. With `numbers`
// each group shows its count on a black box; without, only a group packed
// tight does.
struct IconGroup {
    int icon = 0;
    int count = 0;
};
void drawIconGroups(Screen& screen, const Art& art, const std::vector<IconGroup>& groups, int x, int y, int width,
                    int gap, bool numbers);

// F3: the Continental Congress report for the nation.
void drawCongressReport(Screen& screen, const Art& art, const Game& game, int nation);

// F2: the Religious Adviser report.
void drawReligionReport(Screen& screen, const Art& art, const Game& game, int nation);

// F4: the Labor Adviser report: the grid of professions (zoom -1), or one
// profession zoomed. A click on the grid points at a profession, or -1.
int laborReportHit(int x, int y);
void drawLaborReport(Screen& screen, const Art& art, const Game& game, int nation, int zoom);

// F5: the Economic Adviser report: European trade, then the cargo in port
// in the colonies, 17 to a page.
int economyReportPages(const Game& game, int nation);
void drawEconomyReport(Screen& screen, const Art& art, const Game& game, int nation, int page);

// F6: the Colony Adviser report: the military garrisons, then the Sons of
// Liberty, nine colonies to a page.
int colonyAdviserPages(const Game& game, int nation);
void drawColonyAdviser(Screen& screen, const Art& art, const Game& game, int nation, int page);

// F7: the Naval Adviser report, seven units to a page.
int navalReportPages(const Game& game, int nation);
void drawNavalReport(Screen& screen, const Art& art, const Game& game, int nation, int page);

// F8: the Foreign Affairs report (with Jan de Witt, each nation's figures).
void drawForeignReport(Screen& screen, const Art& art, const Game& game, int nation);

// F9: the Indian Adviser report: the tribes the nation has met, with their
// mood, settlements, missions, muskets and horses.
void drawIndianReport(Screen& screen, const Art& art, const Game& game, int nation);

// F10: the Colonization Score, and the score alone (0 once scoring is
// complete).
void drawScoreReport(Screen& screen, const Art& art, const Game& game, int nation);
int colonizationScore(const Game& game, int nation);

// A retirement: what the citizens name after the player, then the Hall of
// Fame with the newcomer's place (0 to 5).
void drawExploits(Screen& screen, const Art& art, const Game& game, int nation, const Exploits& exploits);
void drawHallOfFame(Screen& screen, const Art& art, const std::vector<HallEntry>& places, int newcomer);

// The report of the REPORTS menu with this function key (2 to 10), at the
// page given for those with more than one (for F4, the profession
// zoomed + 1, or 0).
void drawReport(Screen& screen, const Art& art, const Game& game, int nation, int report, int page);

} // namespace viceroy
