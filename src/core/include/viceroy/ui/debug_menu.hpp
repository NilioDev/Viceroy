// SPDX-License-Identifier: GPL-3.0-or-later
//
// A hidden menu for testing (F12, or Back and Start together on a gamepad).
// It is not part of the original game and only appears when opened; the
// game itself stays as the original plays it.
#pragma once

#include "viceroy/ui/play.hpp"

#include <string>
#include <vector>

namespace viceroy {

class DebugMenu {
public:
    bool open() const { return open_; }
    void toggle(const PlayScreen& play);
    // Up, Down, Confirm and Cancel; returns a line to show the player.
    std::string handle(Command command, PlayScreen& play);
    void draw(Screen& screen, const Art& art) const;

    // The entries for the current screen (the colony screen has its own).
    std::vector<std::string> entries(const PlayScreen& play) const;
    // Carries out an entry by its number in entries().
    std::string apply(int entry, PlayScreen& play);

private:
    bool open_ = false;
    int selected_ = 0;
    std::vector<std::string> shown_;
};

} // namespace viceroy
