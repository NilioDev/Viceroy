// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the original plays when: the next tune once the last one ends, the
// mood that game events ask for, Pick Music, and the three Sound Options.
// It decides which driver commands to send; MusicPlayer plays them.
#pragma once

#include <cstdint>
#include <vector>

namespace viceroy::sound {

struct SoundOptions {
    bool background = true; // Background Music: a new tune whenever one ends
    bool events = true;     // Event Music: also needed for any tune at all
    bool effects = true;    // Sound Effects

    // The options as the save game keeps them (a byte of the header).
    static SoundOptions fromSave(std::uint8_t bits);
    std::uint8_t toSave(std::uint8_t bits) const;
};

// The tunes Pick Music lists, in its order, as driver commands.
inline constexpr int kPickMusicTunes[12] = {0x20, 0x21, 0x22, 0x23, 0x3A, 0x3B, 0x38, 0x24, 0x25, 0x26, 0x27, 0x39};

// The original numbers its tunes 1 to 26: the twelve of Pick Music, six
// independence tunes, four military tunes and four native tunes, in the
// order Pick Music and its three lists show them.
inline constexpr int kTuneCount = 26;
// A tune's driver command.
int tuneCommand(int tune);

class BackgroundMusic {
public:
    explicit BackgroundMusic(std::uint32_t seed = 1);

    SoundOptions options;

    // Whether a driver command may be sent with the current options.
    bool allowed(int command) const;

    // Pick Music: play this tune next (fades out the current one).
    void pick(int command);
    // Mood calls the game makes on events (mood 1..7).
    void setMood(int mood);      // the mood of the next tune
    void moodAfter(int mood);    // the mood of the tune after next
    void changeMood(int mood);   // a new mood now: the tune fades if the mood differs
    void moodEvent(int mood);    // changeMood with Event Music only, setMood otherwise

    // Call every frame with the player's busy(). Returns the driver
    // commands to send, in order (1 fades every voice out).
    std::vector<int> update(bool busy, bool independence);

    int current() const { return current_; }

private:
    int range(int lo, int hi); // lo..hi inclusive

    std::vector<int> out_;
    std::uint32_t seed_;
    int requested_ = -1;
    int current_ = 0;
    int moodNext_ = 0;
    int moodAfter_ = 0;
    int moodNow_ = 0;
    bool pending_ = false;
};

} // namespace viceroy::sound
