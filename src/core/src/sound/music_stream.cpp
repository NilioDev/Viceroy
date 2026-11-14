// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/sound/music_stream.hpp"

#include <algorithm>

extern "C" {
#include "opl3.h"
}

namespace viceroy::sound {

struct OplChip::Impl {
    opl3_chip chip{};
};

OplChip::OplChip(int sampleRate) : impl_(std::make_unique<Impl>()) {
    OPL3_Reset(&impl_->chip, static_cast<std::uint32_t>(sampleRate));
}

OplChip::~OplChip() = default;

void OplChip::write(int set, int reg, int value) {
    OPL3_WriteRegBuffered(&impl_->chip, static_cast<std::uint16_t>(((set & 1) << 8) | (reg & 0xFF)),
                          static_cast<std::uint8_t>(value));
}

void OplChip::render(std::span<std::int16_t> stereo) {
    OPL3_GenerateStream(&impl_->chip, stereo.data(), static_cast<std::uint32_t>(stereo.size() / 2));
}

MusicStream::MusicStream(const MusicDriver& driver, int sampleRate)
    : chip_(sampleRate), player_(driver), samplesPerTick_(sampleRate / MusicPlayer::kTickHz) {
    player_.onWrite = [this](int set, int reg, int value) { chip_.write(set, reg, value); };
    reset(driver.opl3());
}

void MusicStream::reset(bool opl3) {
    for (int set = 0; set < 2; ++set) {
        if (set == 1 && !opl3) {
            break;
        }
        if (set == 1) {
            chip_.write(1, 0x05, 0x01); // OPL3 features on
            chip_.write(1, 0x04, 0x3F); // every channel pair four-operator
        }
        for (int reg = 0x56; reg >= 0x40; --reg) {
            chip_.write(set, reg, 0x3F);
        }
        for (int reg = 0xFF; reg >= 0x60; --reg) {
            chip_.write(set, reg, 0);
        }
        for (int reg = 0x3F; reg >= 0x20; --reg) {
            chip_.write(set, reg, 0);
        }
        chip_.write(set, 0xBD, 0xC0);
    }
}

void MusicStream::render(std::span<std::int16_t> stereo) {
    std::size_t frame = 0;
    const std::size_t frames = stereo.size() / 2;
    while (frame < frames) {
        if (untilTick_ <= 0) {
            player_.tick();
            untilTick_ += samplesPerTick_;
        }
        const std::size_t n = std::min(frames - frame, static_cast<std::size_t>(untilTick_ + 0.999));
        chip_.render(stereo.subspan(frame * 2, n * 2));
        frame += n;
        untilTick_ -= static_cast<double>(n);
    }
}

} // namespace viceroy::sound
