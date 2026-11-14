// SPDX-License-Identifier: GPL-3.0-or-later
//
// Turns the music player into sound: an emulated OPL3 chip plays the
// player's register writes, and the player ticks at the original's rate
// while samples are made.
#pragma once

#include "viceroy/sound/music_driver.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace viceroy::sound {

class OplChip {
public:
    explicit OplChip(int sampleRate);
    ~OplChip();
    OplChip(const OplChip&) = delete;
    OplChip& operator=(const OplChip&) = delete;

    // set 0 or 1 is the chip's first or second register set.
    void write(int set, int reg, int value);
    // Interleaved stereo frames.
    void render(std::span<std::int16_t> stereo);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class MusicStream {
public:
    MusicStream(const MusicDriver& driver, int sampleRate);

    MusicPlayer& player() { return player_; }
    // Fills interleaved stereo frames, ticking the player as time passes.
    void render(std::span<std::int16_t> stereo);

private:
    // Sets the chip up as the driver does when it starts.
    void reset(bool opl3);

    OplChip chip_;
    MusicPlayer player_;
    double samplesPerTick_;
    double untilTick_ = 0;
};

} // namespace viceroy::sound
