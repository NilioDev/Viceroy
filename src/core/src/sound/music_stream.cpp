// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/sound/music_stream.hpp"

#include <algorithm>

extern "C" {
#include "opl3.h"
}

namespace viceroy::sound {
namespace {

// How loud a recording plays against the chip: measured in DOSBox, where
// the chip plays as loud as ours and a step of the 8-bit recording comes out
// as 42.
constexpr int kRecordingGain = 42;

} // namespace

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
    : driver_(driver), chip_(sampleRate), player_(driver), sampleRate_(sampleRate),
      samplesPerTick_(sampleRate / MusicPlayer::kTickHz) {
    player_.onWrite = [this](int set, int reg, int value) { chip_.write(set, reg, value); };
    reset(driver.opl3());
}

void MusicStream::reset(bool opl3) {
    if (opl3) {
        chip_.write(1, 0x05, 0x01); // OPL3 features on
        chip_.write(1, 0x04, 0x3F); // every channel pair four-operator
    }
    player_.reset();
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
        mixRecordings(stereo.subspan(frame * 2, n * 2));
        frame += n;
        untilTick_ -= static_cast<double>(n);
    }
}

void MusicStream::setRecordings(std::vector<std::uint8_t> coldig) {
    coldig_ = std::move(coldig);
    player_.setDigital(!coldig_.empty() && !driver_.samples().empty());
    player_.onSample = [this](int sample) { return queue(sample); };
}

bool MusicStream::queue(int sample) {
    const auto& table = driver_.samples();
    if (sample < 0 || static_cast<std::size_t>(sample) >= table.size()) {
        return true; // the driver takes it and plays nothing
    }
    if (waiting_.size() >= kQueue) {
        return false;
    }
    waiting_.push_back(sample);
    return true;
}

void MusicStream::mixRecordings(std::span<std::int16_t> stereo) {
    const auto& table = driver_.samples();
    for (std::size_t f = 0; f + 1 < stereo.size(); f += 2) {
        if (playing_ < 0) {
            if (waiting_.empty()) {
                return;
            }
            playing_ = waiting_.front();
            waiting_.pop_front();
            position_ = 0;
        }
        const Sample& s = table[static_cast<std::size_t>(playing_)];
        const auto at = static_cast<std::size_t>(position_);
        if (at >= s.length || s.offset + at >= coldig_.size()) {
            playing_ = -1;
            continue;
        }
        // Unsigned 8-bit, 128 the middle.
        const int value = (static_cast<int>(coldig_[s.offset + at]) - 128) * kRecordingGain;
        for (std::size_t c = 0; c < 2; ++c) {
            const int mixed = stereo[f + c] + value;
            stereo[f + c] = static_cast<std::int16_t>(std::clamp(mixed, -32768, 32767));
        }
        position_ += static_cast<double>(s.rate) / sampleRate_;
    }
}

} // namespace viceroy::sound
