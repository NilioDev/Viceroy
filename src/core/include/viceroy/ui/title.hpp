// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the player sees before a game: the opening animation from the
// original's own pictures, then the menu to start or load a game. See
// docs/rules/opening.md.
#pragma once

#include "viceroy/data/image.hpp"
#include "viceroy/render/dialog.hpp"
#include "viceroy/ui/play.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace viceroy {

struct TitleArt {
    Picture menu;   // OPENMENU.PIK: the last picture, behind the menu
    Picture border; // OPENBORD.PIK: wood above and below the map
    Picture map;    // OPENING.PIK: the old map the view sails across
    TextFile script; // OPENING.TXT: when the credits and animations show
    SpriteSheet ship; // OPENSHIP.SS: the ship the view follows
    std::vector<SpriteSheet> credits;    // OPENCRD1.SS to OPENCRD3.SS
    std::vector<SpriteSheet> animations; // by series number of @OPENING
};

// Loads the pictures of the opening. Throws FormatError.
TitleArt loadTitleArt(const std::filesystem::path& folder);

// The opening: the view sails west across the old map from Europe to the
// coast of the New World while the credits show below, a ship runs aground
// and a man wades ashore, then the title comes up. Timings were measured on
// the original; see docs/rules/opening.md.
class Intro {
public:
    explicit Intro(const TitleArt& art);

    // Time passes.
    void tick(std::uint64_t milliseconds);
    // Jumps to the end, as a key press does.
    void skip();
    // The opening has played; the menu comes up over its last picture.
    bool finished() const;
    // The animation's frame number, as OPENING.TXT counts them.
    int frame() const;
    void draw(Screen& screen, const TitleArt& art) const;

    // How long one frame lasts, in milliseconds.
    static constexpr int kFrameMs = 132;

private:
    struct Credit {
        int first = 0;
        int last = 0;
        int sheet = 0;
        int sprite = 0;
    };
    struct Animation {
        int series = 0;
        int first = 0;
        int repeats = 0;
        int baseX = 0;
    };
    std::vector<Credit> credits_;
    std::vector<Animation> animations_;
    int end_ = 891;
    std::uint64_t elapsed_ = 0;
};

enum class TitleChoice { NewWorld, America, Customize, Load, HallOfFame };

class MainMenu {
public:
    // `version` and `date` go in the heading.
    MainMenu(const Art& art, const std::string& version, const std::string& date);

    std::optional<TitleChoice> handle(Command command);
    std::optional<TitleChoice> click(int x, int y);
    void say(std::string text) { message_ = std::move(text); }
    // Over the opening's last picture when one is given, else over the
    // still title picture.
    void draw(Screen& screen, const Art& art, const TitleArt& title, const Intro* behind = nullptr) const;
    const Dialog& dialog() const { return dialog_; }

private:
    Dialog dialog_;
    std::string message_;
};

} // namespace viceroy
