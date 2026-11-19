// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the player sees before a game: the opening animation from the
// original's own pictures, then the menu to start or load a game. See
// docs/rules/opening.md.
#pragma once

#include "viceroy/data/image.hpp"
#include "viceroy/render/dialog.hpp"
#include "viceroy/ui/play.hpp"

#include <array>
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
    // The publisher's logo before the opening, when the game has it.
    std::optional<SpriteSheet> logo;     // MPSLOGO.SS: the turning logo, with the logo's palette
    std::optional<SpriteSheet> logoName; // MPSNAME.SS: the name written out below it
};

// Loads the pictures of the opening. Throws FormatError.
TitleArt loadTitleArt(const std::filesystem::path& folder);

// The publisher's logo that comes before the opening, as OPENING.EXE plays
// it: the logo turns on black, fades in, the publisher's name is written
// out below it, and it fades out again. See docs/rules/opening.md.
class Logo {
public:
    // `sound`: the original waits for the music before it fades in, but
    // only when it has a sound driver.
    Logo(const TitleArt& art, bool sound);

    // Time passes.
    void tick(std::uint64_t milliseconds);
    // Ends the logo, as a key press or a click does.
    void skip() { going_ = false; }
    // The logo is over; the opening comes next.
    bool finished() const { return !going_; }
    // The logo's frame number, counted from 1 as the original does.
    int frame() const { return frame_; }
    void draw(Screen& screen) const;

    // The tune the original starts as the logo comes up.
    static constexpr int kMusic = 0x34;
    // The frames at which the logo fades out and ends, from OPENING.EXE.
    static constexpr int kFadeOutFrame = 196;
    static constexpr int kLastFrame = 229;
    // The frame from which the name is written out.
    static constexpr int kNameFrame = 92;

private:
    void step();      // one frame of the logo (OPENING.EXE's _do_logo)
    void fadeStep();  // one step of the fade in or out

    const SpriteSheet& logo_;
    const SpriteSheet& name_;
    IndexedImage canvas_;
    std::array<std::uint8_t, 768> target_{};  // the logo's palette, 0..63
    std::array<std::uint8_t, 768> current_{}; // after the last step of a fade, 0..63
    std::array<std::uint8_t, 768> shown_{};   // before it: the step reaches the screen a part at a time
    std::array<std::uint8_t, 768> increments_{};
    int fadeUpStart_ = 32;
    int fadeCount_ = 255; // steps of the fade so far; above 16 none runs
    int frame_ = 0;
    int logoSprite_ = 1;
    int nameSprite_ = 1;
    bool going_ = true;
    std::int64_t now_ = 0;      // microseconds since the logo came up
    std::int64_t loop_ = 0;     // when the original's loop next runs
    std::int64_t logoClock_ = -6; // the game clock's tick of the last frame
    std::int64_t stepStart_ = 0;  // when the last step of a fade began
};

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

    // The opening's music: driver command 34h starts with the publisher's
    // logo, so without the logo the tune starts this many driver ticks in,
    // where it is when the map comes up (measured in DOSBox).
    static constexpr int kMusic = 0x34;
    static constexpr int kMusicLeadTicks = 1430;
    // The driver commands the opening sent since the last call, in order:
    // 8026h at frame 25, and 25h as the man starts wading ashore.
    std::vector<int> takeSounds();

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
    int ashore_ = 720; // the first frame of the man wading ashore (series 7)
    std::uint64_t elapsed_ = 0;
    std::vector<int> sounds_;
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
