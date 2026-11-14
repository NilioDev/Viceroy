// SPDX-License-Identifier: GPL-3.0-or-later
//
// The original's music and sound effects, played the way its FM sound
// drivers play them. The drivers (ASOUND.COL, PSOUND.COL) carry their own
// pieces: per voice a stream of notes and commands, plus instruments. We read
// those from the player's copy and play them with our own player, which turns
// them into register writes for an OPL chip. See docs/formats/sound-driver.md.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace viceroy::sound {

// One piece as a driver command starts it: which voices start on which
// stream, and which voice (if any) plays its notes as digitised drums.
struct Piece {
    struct Start {
        int voice = 0;
        std::uint16_t stream = 0;
    };
    std::vector<Start> starts;
    int drumVoice = -1;
    bool stopFirst = false;     // the other voices fade out first
    bool onlyIfIdle = false;    // started only when the first stream is not already playing

    // A piece that cuts in on a beat: the driver keeps a clock of `period`
    // ticks. With nothing else playing (or no clock yet) it starts at once
    // with the voices silenced; otherwise it waits for the clock to come
    // round, as `later` or, while one of `ifPlaying` plays, as `laterAfter`.
    struct Segue {
        int period = 0;
        std::vector<Start> now;
        std::vector<Start> laterAfter;
        std::array<std::uint16_t, 2> ifPlaying{};
    };
    std::optional<Segue> segue;

    // A sound effect: a recording to play with a card that plays samples
    // (-1: none), else the streams to start on free effect voices.
    int sample = -1;
    std::vector<std::uint16_t> effectStreams;
};

// Where a recording lies in COLDIG.BIN, and how fast it plays.
struct Sample {
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
    int rate = 0; // samples a second
};

// The data of one FM driver file.
// Commands 0x8020 and up: a few more pieces built like the tunes.
inline constexpr int kSpecialPieces = 7;

class MusicDriver {
public:
    // Reads a driver file (ASOUND.COL or PSOUND.COL). Empty when the file is
    // not one of the original's FM drivers.
    static std::optional<MusicDriver> load(std::span<const std::uint8_t> file);

    // The piece a driver command starts: 0x20-0x3F are tunes, 0x40-0x5D sound
    // effects, 0x8020-0x8026 more pieces. Null when the command starts nothing this player can play.
    const Piece* piece(int command) const;

    // True for the OPL3 driver (four-operator voices on two register sets).
    bool opl3() const { return opl3_; }

    // The recordings of COLDIG.BIN, as the driver lists them.
    const std::vector<Sample>& samples() const { return samples_; }

    std::uint8_t byte(std::uint16_t at) const { return data_[at]; }
    std::uint16_t word(std::uint16_t at) const {
        return static_cast<std::uint16_t>(data_[at] | (data_[static_cast<std::uint16_t>(at + 1)] << 8));
    }
    const std::vector<std::uint8_t>& data() const { return data_; }

    // A driver routine that a stream calls (command C4), as far as this
    // player knows it: one that writes two bytes, in a random order, into
    // four places of the stream.
    struct Shuffle {
        std::uint8_t first = 0, second = 0;
        std::array<std::uint16_t, 4> at{};
        std::array<bool, 4> takesSecond{};
    };
    std::optional<Shuffle> routine(std::uint16_t address) const;

    // Where the driver keeps things in its data.
    std::uint16_t instruments = 0;   // 64 bytes per instrument
    std::uint16_t notes = 0;         // twelve frequency numbers, C to B
    std::uint16_t stopStream = 0;    // where a faded voice goes to end
    std::uint16_t randomSeed = 0;
    std::array<std::uint8_t, 9> port{};        // 1, 2, or 3 for both register sets
    std::array<std::uint8_t, 9> channel{};
    std::array<std::uint8_t, 36> operators{};  // four operator offsets per voice

private:
    std::vector<std::uint8_t> data_;           // the data segment, 64 KiB
    std::vector<std::uint8_t> code_;
    // Commands 0x20..0x5D, then 0x8020..0x8026 (kSpecialPieces).
    std::array<Piece, 0x45> pieces_{};
    std::array<bool, 0x45> known_{};
    std::vector<Sample> samples_;
    bool opl3_ = false;
};

// Plays a driver's pieces. Call tick() at the driver's rate (kTickHz); every
// register write goes to onWrite.
class MusicPlayer {
public:
    static constexpr double kTickHz = 60.877;

    explicit MusicPlayer(const MusicDriver& driver);

    // A driver command: 0 resets, 1 fades every voice out, 0x20-0x5D
    // start a piece. Returns false when nothing starts.
    bool command(int command);
    // Fades every voice out, as the driver's "stop" command does.
    void stop();
    // Silences every voice at once and sets the chip up, as the driver's
    // "reset" command does.
    void reset();
    // True while any voice is playing: the game asks this before it picks
    // the next tune.
    bool busy() const;

    void tick();

    // With a sound card that plays samples, drum voices and sample commands
    // play digitised sounds (onSample) instead of FM notes.
    void setDigital(bool on) { digital_ = on; }

    std::function<void(int port, int reg, int value)> onWrite;
    // A recording to queue; false when the queue is full.
    std::function<bool(int sample)> onSample;

private:
    struct Voice {
        std::array<std::uint8_t, 0x32> r{};
        std::uint16_t pc = 0, start = 0, mark = 0, outerMark = 0, restart = 0, ret = 0;
        std::uint16_t noise = 0, noiseStep = 0, noiseMask = 0;
        int innerCount = 0, outerCount = 0;
    };

    void write(int port, int reg, int value);
    int shadow(int port, int reg) const;
    std::uint16_t random();
    void startVoice(int voice, std::uint16_t stream);
    void startEffect(std::uint16_t stream);
    void fadeVoice(int voice);
    void update(int voice);
    void interpret(int voice, bool& refresh);
    void noteOn(int voice);
    void keyOff(int voice);
    void setFrequency(int voice);
    void bend(int voice);
    void setPan(int voice);
    void setLevels(int voice);
    void setLevel(int voice, int op, int base);
    void loadInstrument(int voice);
    void fadeStep();
    void noiseStep(int voice);
    bool playing(std::uint16_t stream) const;
    void startNow(const std::vector<Piece::Start>& starts);
    int pan(int voice) const;
    std::uint16_t instrument(int voice) const;

    const MusicDriver& driver_;
    std::vector<std::uint8_t> mem_;   // the driver data; pieces may rewrite their own streams
    std::array<Voice, 9> voices_{};
    std::array<std::array<std::uint8_t, 256>, 2> regs_{};
    std::array<std::uint8_t, 256> vars_{};
    std::uint16_t seed_ = 0;
    int drumVoice_ = -1;
    bool digital_ = false;
    bool fading_ = false;
    // The segue clock and the piece waiting for it.
    int clock_ = 0;
    int clockPeriod_ = 0;
    const std::vector<Piece::Start>* waiting_ = nullptr;
};

} // namespace viceroy::sound
