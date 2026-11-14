// SPDX-License-Identifier: GPL-3.0-or-later
//
// The end of a game by retirement: the rating that names something after
// the player, and the Colonization Hall of Fame (HALLFAME.DAT, the
// original's own format). See docs/rules/the-end.md.
#pragma once

#include "viceroy/data/madspack.hpp" // Bytes

#include <span>
#include <string>
#include <vector>

namespace viceroy {

// What a retirement is worth: the rating in percent and the step (0 to 23)
// of the @SCORE list it reaches, -1 for none.
struct Exploits {
    int rating = 0;
    int level = -1;
};
Exploits exploitsFor(int score, int difficulty);

// One place in the Hall of Fame (42 bytes in the file).
struct HallEntry {
    std::string name; // the leader
    int nation = -1;  // -1: an empty place
    bool declared = false;
    bool won = false;
    int year = 0;
    int season = 0;
    int difficulty = 0;
    int score = -1;
    int rating = -1;
    int level = 0;
};

// The five places the file keeps.
inline constexpr int kHallPlaces = 5;

// The places in a file (missing or short: all empty), always six of them:
// the original reads five and keeps a sixth for the newcomer.
std::vector<HallEntry> readHallOfFame(std::span<const std::uint8_t> file);
// The first five places, as the file holds them.
Bytes writeHallOfFame(const std::vector<HallEntry>& places);
// Puts the entry before the first place with a lower rating (else last),
// and returns where it went (0 to 5).
int enterHallOfFame(std::vector<HallEntry>& places, const HallEntry& entry);

} // namespace viceroy
