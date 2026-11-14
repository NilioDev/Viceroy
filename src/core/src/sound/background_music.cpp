// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/sound/background_music.hpp"

namespace viceroy::sound {
namespace {

constexpr int kFadeAll = 1;

// The option bits in the save game's byte (see docs/rules/sound.md).
constexpr std::uint8_t kBackgroundBit = 0x02;
constexpr std::uint8_t kEventsBit = 0x04;
constexpr std::uint8_t kEffectsBit = 0x08;

} // namespace

SoundOptions SoundOptions::fromSave(std::uint8_t bits) {
    return {(bits & kBackgroundBit) != 0, (bits & kEventsBit) != 0, (bits & kEffectsBit) != 0};
}

std::uint8_t SoundOptions::toSave(std::uint8_t bits) const {
    bits = static_cast<std::uint8_t>(bits & ~(kBackgroundBit | kEventsBit | kEffectsBit));
    return static_cast<std::uint8_t>(bits | (background ? kBackgroundBit : 0) | (events ? kEventsBit : 0) |
                                     (effects ? kEffectsBit : 0));
}

BackgroundMusic::BackgroundMusic(std::uint32_t seed) : seed_(seed) {}

int BackgroundMusic::range(int lo, int hi) {
    seed_ = seed_ * 214013u + 2531011u;
    const int r = static_cast<int>((seed_ >> 16) & 0x7FFF);
    return lo + ((hi - lo + 1) * r >> 15);
}

int BackgroundMusic::command(int tune) {
    // Tunes 1-12 are the ones Pick Music names, 13-22 the independence and
    // military tunes, 23-26 the native ones.
    static constexpr int kMore[4] = {0x33, 0x32, 0x35, 0x36};
    if (tune >= 1 && tune <= 12) {
        return kPickMusicTunes[tune - 1];
    }
    if (tune >= 13 && tune <= 22) {
        return tune + 0x1B;
    }
    if (tune >= 23 && tune <= 26) {
        return kMore[tune - 23];
    }
    return 0x25;
}

bool BackgroundMusic::allowed(int command) const {
    if (command < 0x10) {
        return true;
    }
    if ((command & 0x20) != 0 && options.events) {
        return true;
    }
    return (command & 0x40) != 0 && options.effects;
}

void BackgroundMusic::pick(int command) {
    if (command == current_) {
        return;
    }
    requested_ = command;
    if (options.events && !options.background) {
        pending_ = true;
    }
    if (command >= 0) {
        out_.push_back(kFadeAll);
    }
}

void BackgroundMusic::setMood(int mood) { moodNext_ = mood; }

void BackgroundMusic::moodAfter(int mood) { moodAfter_ = mood; }

void BackgroundMusic::changeMood(int mood) {
    moodNext_ = mood;
    if (mood == moodNow_) {
        return;
    }
    if (options.events && !options.background) {
        pending_ = true;
    }
    out_.push_back(kFadeAll);
}

void BackgroundMusic::moodEvent(int mood) {
    if (options.events && !options.background) {
        changeMood(mood);
    } else {
        setMood(mood);
    }
}

std::vector<int> BackgroundMusic::update(bool busy, bool independence) {
    const auto flush = [&] {
        std::vector<int> out;
        for (const int c : out_) {
            if (allowed(c)) {
                out.push_back(c);
            }
        }
        out_.clear();
        return out;
    };
    if ((!options.background && !pending_) || busy) {
        return flush();
    }
    pending_ = false;
    int next = 0;
    if (requested_ >= 0) {
        next = requested_;
        requested_ = -1;
    } else {
        int lo = 1;
        int count = 12;
        if (!independence) {
            if (range(0, 8) == 0) {
                lo = 13;
                count = 11;
            }
        } else {
            lo = 13;
            count = 6;
            if (range(0, 4) == 0) {
                lo = 1;
                count = 12;
            }
        }
        switch (moodNext_) {
        case 1: lo = 1; count = 7; break;
        case 2: lo = 8; count = 5; break;
        case 3: lo = 13; count = 6; break;
        case 4: lo = 19; count = 4; break;
        case 5: if (current_ != 0x33) { lo = 23; count = 1; } break;
        case 6: if (current_ != 0x35) { lo = 25; count = 1; } break;
        case 7: if (current_ != 0x36) { lo = 26; count = 1; } break;
        default: break;
        }
        int tune = lo;
        for (int tries = 0; tries < 64; ++tries) {
            tune = range(0, count - 1) + lo;
            next = command(tune);
            if (next != current_) {
                break;
            }
        }
        if (moodNext_ == 0) {
            static constexpr int kLastTune[6] = {6, 12, 18, 22, 24, 25};
            moodNext_ = 7;
            for (int m = 0; m < 6; ++m) {
                if (tune <= kLastTune[m]) {
                    moodNext_ = m + 1;
                    break;
                }
            }
        }
        moodNow_ = moodNext_;
        moodNext_ = moodAfter_;
        moodAfter_ = 0;
    }
    current_ = next;
    out_.push_back(next);
    return flush();
}

} // namespace viceroy::sound
