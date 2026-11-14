// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/sound/music_driver.hpp"

#include <algorithm>
#include <cstring>

namespace viceroy::sound {
namespace {

int s8(int v) {
    v &= 0xFF;
    return v >= 0x80 ? v - 0x100 : v;
}

std::uint8_t u8(int v) { return static_cast<std::uint8_t>(v & 0xFF); }

std::uint16_t le16(std::span<const std::uint8_t> b, std::size_t at) {
    if (at + 1 >= b.size()) {
        return 0;
    }
    return static_cast<std::uint16_t>(b[at] | (b[at + 1] << 8));
}

// Voice state, as offsets into Voice::r.
enum : int {
    kTicks = 0x00,     // ticks left of the current note; 0 = voice off
    kBend = 0x01,      // added to the frequency number every tick
    kFadeStep = 0x02,  // added to the attenuation every kFadeEvery ticks
    kPanStep = 0x03,   // added to the pan every kPanEvery ticks
    kNote = 0x04,
    kInstrument = 0x05,
    kVolume = 0x06,
    kGap = 0x07,       // the note is released this many ticks before its end
    kRelease = 0x08,   // ticks until the key is released
    kFadeCount = 0x09,
    kFadeEvery = 0x0A,
    kPanCount = 0x0B,
    kPanEvery = 0x0C,
    kPan = 0x0D,       // 0x40 is the middle
    kAttenuation = 0x0E,
    kConnection = 0x0F,
    kOpLevel = 0x10,   // four bytes: each operator's level from the instrument
    kNoiseCount = 0x2A,
    kDetune = 0x2C,
    kTranspose = 0x2D,
    kFading = 0x2E,
    kQuiet = 0x2F,
    kHold = 0x30,      // fixed release time, overrides kGap
};

// The code that starts a voice in a driver: "mov bx, voice; jmp start".
// Returns the voice's state address, or 0.
std::uint16_t voiceStub(std::span<const std::uint8_t> code, std::size_t at) {
    if (at + 3 <= code.size() && code[at] == 0xBB) {
        return le16(code, at + 1);
    }
    return 0;
}

} // namespace

// --- reading the driver -------------------------------------------------

std::optional<MusicDriver> MusicDriver::load(std::span<const std::uint8_t> file) {
    if (file.size() < 0x400 || file[0] != 'M' || file[1] != 'Z') {
        return std::nullopt;
    }
    const std::size_t header = static_cast<std::size_t>(le16(file, 8)) * 16;
    if (header >= file.size()) {
        return std::nullopt;
    }
    const std::span<const std::uint8_t> code = file.subspan(header);
    // The driver's own header: its name, its data segment and its entry points.
    if (code.size() < 0x40 || std::memcmp(code.data() + 0x10, "Coloniza", 8) != 0) {
        return std::nullopt;
    }
    const std::size_t dataStart = static_cast<std::size_t>(le16(code, 0x2A)) * 16;
    const int entries = le16(code, 0x30);
    if (dataStart >= code.size() || entries < 3) {
        return std::nullopt;
    }
    MusicDriver d;
    d.data_.assign(0x10000, 0);
    std::copy(code.begin() + static_cast<std::ptrdiff_t>(dataStart), code.end(), d.data_.begin());
    d.code_.assign(code.begin(), code.end());

    // The command entry point dispatches through jump tables, one per
    // command range: find the tunes' table (after "sub bx, 20h") and the
    // effects' table (after "sub bx, 40h").
    const std::size_t entry = le16(code, 0x34);
    std::size_t tunes = 0;
    std::size_t effects = 0;
    for (std::size_t at = entry; at + 8 < code.size() && at < entry + 0x80; ++at) {
        if (code[at] == 0x83 && code[at + 1] == 0xEB) {
            const int base = code[at + 2];
            for (std::size_t k = at + 3; k + 5 < code.size() && k < at + 20; ++k) {
                if (code[k] == 0x2E && code[k + 1] == 0xFF && code[k + 2] == 0x97) {
                    (base == 0x20 ? tunes : effects) = base == 0x20 || base == 0x40 ? le16(code, k + 3) : 0;
                    break;
                }
            }
        }
    }
    if (tunes == 0) {
        return std::nullopt;
    }

    // Each command's code: stops the voices, may name a drum voice, and
    // starts voices on streams with "lea cx, stream; call start_voice_n".
    std::uint16_t firstVoice = 0xFFFF;
    struct Raw {
        std::uint16_t state;
        std::uint16_t stream;
    };
    std::array<std::vector<Raw>, 0x3E> raw{};
    const auto parse = [&](int index, std::size_t at) {
        Piece& p = d.pieces_[static_cast<std::size_t>(index)];
        bool ok = true;
        for (int steps = 0; steps < 40 && at + 6 < code.size(); ++steps) {
            const std::uint8_t op = code[at];
            if (op == 0xC6 && code[at + 1] == 0x06) { // mov byte [x], n
                if (le16(code, at + 2) == 0x1CA) {
                    p.drumVoice = code[at + 4];
                }
                at += 5;
            } else if (op == 0x8D && code[at + 1] == 0x0E) { // lea cx, stream
                const std::uint16_t stream = le16(code, at + 2);
                at += 4;
                if (code[at] != 0xE8 && code[at] != 0xE9) {
                    ok = false;
                    break;
                }
                const std::size_t target =
                    (at + 3 + static_cast<std::size_t>(static_cast<std::int16_t>(le16(code, at + 1)))) & 0xFFFF;
                const std::uint16_t state = voiceStub(code, target);
                const bool last = code[at] == 0xE9;
                at += 3;
                if (state == 0) {
                    // "already playing this stream?" followed by a jump past the piece
                    if (code[at] == 0x75 || code[at] == 0x74) {
                        p.onlyIfIdle = true;
                        at += 2;
                        continue;
                    }
                    ok = false;
                    break;
                }
                raw[static_cast<std::size_t>(index)].push_back({state, stream});
                firstVoice = std::min(firstVoice, state);
                if (last) {
                    break;
                }
            } else if (op == 0xE8) { // a call: the "fade every voice" routine
                const std::size_t target =
                    (at + 3 + static_cast<std::size_t>(static_cast<std::int16_t>(le16(code, at + 1)))) & 0xFFFF;
                (void)target;
                p.stopFirst = true;
                at += 3;
            } else if (op == 0xC3 || op == 0xEB) {
                break;
            } else {
                ok = false;
                break;
            }
        }
        if (!ok || raw[static_cast<std::size_t>(index)].empty()) {
            raw[static_cast<std::size_t>(index)].clear();
        }
    };
    for (int i = 0; i < 0x20; ++i) {
        parse(i, le16(code, tunes + static_cast<std::size_t>(i) * 2));
    }
    if (effects != 0) {
        for (int i = 0; i < 0x1E; ++i) {
            parse(0x20 + i, le16(code, effects + static_cast<std::size_t>(i) * 2));
        }
    }
    const auto toVoice = [&](std::uint16_t state) { return (state - firstVoice) / 0x32; };
    for (std::size_t i = 0; i < raw.size(); ++i) {
        for (const Raw& r : raw[i]) {
            const int voice = toVoice(r.state);
            if (voice >= 0 && voice < 9) {
                d.pieces_[i].starts.push_back({voice, r.stream});
            }
        }
        d.known_[i] = !d.pieces_[i].starts.empty();
    }

    // Segues: "if the clock is not running, start at once; otherwise if
    // stream X or Y plays, set the clock's piece to B; otherwise unless
    // stream Z plays, to A." Read the clock period, the three streams and
    // the two pieces (each: reset, then voices started).
    const auto block = [&](std::size_t at) {
        std::vector<Piece::Start> starts;
        if (at + 3 < code.size() && code[at] == 0xE8) {
            at += 3;
        }
        while (at + 7 < code.size() && code[at] == 0x8D && code[at + 1] == 0x0E && code[at + 4] == 0xE8) {
            const std::size_t target =
                (at + 7 + static_cast<std::size_t>(static_cast<std::int16_t>(le16(code, at + 5)))) & 0xFFFF;
            const std::uint16_t state = voiceStub(code, target);
            if (state == 0) {
                break;
            }
            starts.push_back({toVoice(state), le16(code, at + 2)});
            at += 7;
        }
        return starts;
    };
    for (int i = 0; i < 0x20; ++i) {
        const std::size_t at = le16(code, tunes + static_cast<std::size_t>(i) * 2);
        if (d.known_[static_cast<std::size_t>(i)] || at + 0x60 > code.size() || code[at] != 0x83 ||
            code[at + 1] != 0x3E) {
            continue;
        }
        Piece::Segue s;
        std::vector<std::uint16_t> checks;
        std::vector<std::size_t> pieces;
        for (std::size_t k = at; k < at + 0x50; ++k) {
            if (code[k] == 0x8D && code[k + 1] == 0x0E && code[k + 4] == 0xE8) {
                checks.push_back(le16(code, k + 2));
            } else if (code[k] == 0x8D && code[k + 1] == 0x1E && code[k + 4] == 0x89 && code[k + 5] == 0x1E) {
                pieces.push_back(le16(code, k + 2));
            } else if (code[k] == 0xC7 && code[k + 1] == 0x06 && s.period == 0 && le16(code, k + 4) != 0) {
                s.period = le16(code, k + 4);
            }
        }
        if (checks.size() != 3 || pieces.size() != 2 || s.period == 0) {
            continue;
        }
        s.now = block(pieces[1]);
        s.laterAfter = block(pieces[0]);
        s.ifPlaying = {checks[0], checks[1]};
        if (s.now.empty() || s.laterAfter.empty() || s.now.front().stream != checks[2]) {
            continue;
        }
        Piece& p = d.pieces_[static_cast<std::size_t>(i)];
        p.starts = s.now;
        p.segue = std::move(s);
        d.known_[static_cast<std::size_t>(i)] = true;
    }

    // The tables the player needs, at the places both FM drivers keep them.
    d.instruments = 0x31E;
    d.notes = 0x176;
    d.stopStream = 0x1CC;
    d.randomSeed = d.word(0x56);
    for (int v = 0; v < 9; ++v) {
        d.port[static_cast<std::size_t>(v)] = d.byte(static_cast<std::uint16_t>(0x192 + v));
        d.channel[static_cast<std::size_t>(v)] = d.byte(static_cast<std::uint16_t>(0x19C + v));
    }
    // So far only the OPL3 driver's layout is known: three music voices on
    // each register set, the effect voices on both.
    if (d.port != std::array<std::uint8_t, 9>{1, 1, 1, 2, 2, 2, 3, 3, 3}) {
        return std::nullopt;
    }
    d.opl3_ = true;
    for (int i = 0; i < 36; ++i) {
        d.operators[static_cast<std::size_t>(i)] = d.byte(static_cast<std::uint16_t>(0x1A6 + i));
    }
    return d;
}

std::optional<MusicDriver::Shuffle> MusicDriver::routine(std::uint16_t address) const {
    // "mov bx, two bytes; call random; and ax, 4; jz +2; xchg bl, bh" and
    // then four "mov [place], bl/bh".
    const std::span<const std::uint8_t> c(code_);
    const std::size_t a = address;
    if (a + 30 > c.size() || c[a] != 0xBB || c[a + 3] != 0xE8 || c[a + 6] != 0x25 || c[a + 9] != 0x74 ||
        c[a + 11] != 0x86 || c[a + 12] != 0xDF) {
        return std::nullopt;
    }
    Shuffle s;
    s.first = c[a + 1];
    s.second = c[a + 2];
    for (std::size_t k = 0; k < 4; ++k) {
        const std::size_t at = a + 13 + 4 * k;
        if (c[at] != 0x88 || (c[at + 1] != 0x1E && c[at + 1] != 0x3E)) {
            return std::nullopt;
        }
        s.takesSecond[k] = c[at + 1] == 0x3E;
        s.at[k] = le16(c, at + 2);
    }
    return s;
}

const Piece* MusicDriver::piece(int command) const {
    const int i = command - 0x20;
    if (i < 0 || i >= static_cast<int>(pieces_.size()) || !known_[static_cast<std::size_t>(i)]) {
        return nullptr;
    }
    return &pieces_[static_cast<std::size_t>(i)];
}

// --- the player -----------------------------------------------------------

MusicPlayer::MusicPlayer(const MusicDriver& driver)
    : driver_(driver), mem_(driver.data()), seed_(driver.randomSeed) {}

void MusicPlayer::write(int port, int reg, int value) {
    for (int set = 0; set < 2; ++set) {
        if (port & (1 << set)) {
            regs_[static_cast<std::size_t>(set)][static_cast<std::size_t>(reg & 0xFF)] = u8(value);
            if (onWrite) {
                onWrite(set, reg & 0xFF, value & 0xFF);
            }
        }
    }
}

int MusicPlayer::shadow(int port, int reg) const {
    return regs_[(port & 2) ? 1 : 0][static_cast<std::size_t>(reg & 0xFF)];
}

std::uint16_t MusicPlayer::random() {
    std::uint16_t a = static_cast<std::uint16_t>(0x9248 + seed_);
    a = static_cast<std::uint16_t>((a >> 3) | (a << 13));
    seed_ = a;
    return a;
}

std::uint16_t MusicPlayer::instrument(int voice) const {
    const int n = voices_[static_cast<std::size_t>(voice)].r[kInstrument];
    return static_cast<std::uint16_t>(driver_.instruments + 0x40 * n);
}

void MusicPlayer::startVoice(int voice, std::uint16_t stream) {
    Voice& v = voices_[static_cast<std::size_t>(voice)];
    v = Voice{};
    v.pc = v.start = v.mark = v.outerMark = v.restart = stream;
    v.r[kPan] = 0x40;
    v.r[kTicks] = 1;
}

void MusicPlayer::fadeVoice(int voice) {
    Voice& v = voices_[static_cast<std::size_t>(voice)];
    if (v.r[kTicks] != 0) {
        v.r[kFading] = 0xFF;
        v.restart = 0xFFFF;
    }
    fading_ = true;
}

void MusicPlayer::stop() {
    for (int i = 0; i < 9; ++i) {
        fadeVoice(i);
    }
}

bool MusicPlayer::command(int command) {
    if (command == 0) {
        reset();
        return true;
    }
    if (command == 1) {
        stop();
        return true;
    }
    const Piece* p = driver_.piece(command);
    if (p == nullptr) {
        return false;
    }
    if (p->segue) {
        const Piece::Segue& s = *p->segue;
        bool music = false;
        for (int i = 0; i < 6; ++i) {
            music = music || voices_[static_cast<std::size_t>(i)].r[kTicks] != 0;
        }
        if (clock_ == 0 || !music) {
            clock_ = clockPeriod_ = s.period;
            waiting_ = nullptr;
            startNow(s.now);
        } else if (playing(s.ifPlaying[0]) || playing(s.ifPlaying[1])) {
            waiting_ = &s.laterAfter;
        } else if (!playing(s.now.front().stream)) {
            waiting_ = &s.now;
        }
        return true;
    }
    if (p->onlyIfIdle && !p->starts.empty() && playing(p->starts.front().stream)) {
        return true;
    }
    drumVoice_ = p->drumVoice;
    if (p->stopFirst) {
        stop();
    }
    for (const Piece::Start& s : p->starts) {
        startVoice(s.voice, s.stream);
    }
    return true;
}

bool MusicPlayer::playing(std::uint16_t stream) const {
    for (int i = 0; i < 6; ++i) {
        const Voice& v = voices_[static_cast<std::size_t>(i)];
        if (v.r[kTicks] != 0 && v.restart == stream) {
            return true;
        }
    }
    return false;
}

void MusicPlayer::reset() {
    for (Voice& v : voices_) {
        v.r[kTicks] = v.r[kBend] = v.r[kFadeStep] = v.r[kPanStep] = v.r[kNoiseCount] = 0;
        v.noise = v.noiseStep = v.noiseMask = 0;
    }
    for (int reg = 0x56; reg >= 0x40; --reg) {
        write(3, reg, 0x3F);
    }
    for (int reg = 0xFF; reg >= 0x60; --reg) {
        write(3, reg, 0);
    }
    for (int reg = 0x3F; reg >= 0x20; --reg) {
        write(3, reg, 0);
    }
    write(3, 0xBD, 0xC0);
    drumVoice_ = -1;
}

void MusicPlayer::startNow(const std::vector<Piece::Start>& starts) {
    reset();
    for (const Piece::Start& s : starts) {
        startVoice(s.voice, s.stream);
    }
}

bool MusicPlayer::busy() const {
    return std::any_of(voices_.begin(), voices_.end(), [](const Voice& v) { return v.r[kTicks] != 0; });
}

void MusicPlayer::tick() {
    random();
    for (int i = 0; i < 9; ++i) {
        update(i);
    }
    if (clockPeriod_ != 0 && --clock_ <= 0) {
        clock_ = clockPeriod_;
        if (waiting_ != nullptr) {
            const auto* starts = waiting_;
            waiting_ = nullptr;
            startNow(*starts);
        }
    }
    fadeStep();
    for (int i = 8; i >= 0; --i) {
        noiseStep(i);
    }
}

// --- OPL registers ----------------------------------------------------------

int MusicPlayer::pan(int voice) const {
    const int p = voices_[static_cast<std::size_t>(voice)].r[kPan];
    if (p > 0x2A && p < 0x55) {
        return 0x30;
    }
    return p < 0x2B ? 0x10 : 0x20;
}

void MusicPlayer::setPan(int voice) {
    if (voice >= 6) {
        return;
    }
    const int port = driver_.port[static_cast<std::size_t>(voice)];
    const int ch = driver_.channel[static_cast<std::size_t>(voice)];
    const int p = pan(voice);
    for (int reg : {0xC0 + ch, 0xC3 + ch}) {
        write(port, reg, (shadow(port, reg) & 0x0F) | p);
    }
}

void MusicPlayer::setLevel(int voice, int op, int base) {
    const Voice& v = voices_[static_cast<std::size_t>(voice)];
    const int ksl = (mem_[static_cast<std::size_t>(instrument(voice) + 7 + 14 * op)] << 6) & 0xFF;
    const int level = std::clamp(s8(base - v.r[static_cast<std::size_t>(kOpLevel + op)]), 0, 0x3F);
    write(driver_.port[static_cast<std::size_t>(voice)],
          0x40 + driver_.operators[static_cast<std::size_t>(4 * voice + op)], ksl | level);
}

void MusicPlayer::setLevels(int voice) {
    const Voice& v = voices_[static_cast<std::size_t>(voice)];
    const int connection = v.r[kConnection];
    int base = s8(0x7F - v.r[kVolume] - v.r[kAttenuation] - v.r[kQuiet]);
    if (voice < 6) {
        if (v.r[kPan] > 0x2A && v.r[kPan] < 0x55) {
            base = s8(base + 6);
        }
        setLevel(voice, 3, base);
        if (connection == 1) {
            setLevel(voice, 1, base);
        }
        if (connection & 2) {
            setLevel(voice, 0, base);
        }
        if (connection == 3) {
            setLevel(voice, 2, base);
        }
        return;
    }
    // Effect voices sound on both register sets: the pan sets how loud each side is.
    const int pan = v.r[kPan];
    const auto side = [&](int op, int set, int curve) {
        const int ksl = (mem_[static_cast<std::size_t>(instrument(voice) + 7 + 14 * op)] << 6) & 0xFF;
        const int level = std::clamp(s8(curve - v.r[static_cast<std::size_t>(kOpLevel + op)] + base), 0, 0x3F);
        write(set, 0x40 + driver_.operators[static_cast<std::size_t>(4 * voice + op)], ksl | level);
    };
    const std::size_t left = static_cast<std::size_t>(0xF6 + pan) & 0xFFFF;
    const std::size_t right = static_cast<std::size_t>(0x175 - pan) & 0xFFFF;
    side(1, 1, mem_[left]);
    side(1, 2, mem_[right]);
    if (connection == 0) {
        side(0, 1, mem_[left]);
        side(0, 2, mem_[right]);
    }
}

void MusicPlayer::setFrequency(int voice) {
    const Voice& v = voices_[static_cast<std::size_t>(voice)];
    const int port = driver_.port[static_cast<std::size_t>(voice)];
    const int ch = driver_.channel[static_cast<std::size_t>(voice)];
    const int note = (v.r[kNote] + v.r[kTranspose]) & 0xFF;
    const int octave = note / 12;
    const int base = driver_.word(static_cast<std::uint16_t>(driver_.notes + 2 * (note % 12)));
    const int f = (base + s8(v.r[kDetune])) & 0xFFFF;
    write(port, 0xA0 + ch, f & 0xFF);
    write(port, 0xB0 + ch, (shadow(port, 0xB0 + ch) & 0x20) | (f >> 8) | (octave << 2));
}

void MusicPlayer::bend(int voice) {
    const Voice& v = voices_[static_cast<std::size_t>(voice)];
    const int port = driver_.port[static_cast<std::size_t>(voice)];
    const int ch = driver_.channel[static_cast<std::size_t>(voice)];
    int f = shadow(port, 0xA0 + ch) | ((shadow(port, 0xB0 + ch) & 0x1F) << 8);
    f = (f + s8(v.r[kBend])) & 0xFFFF;
    write(port, 0xA0 + ch, f & 0xFF);
    write(port, 0xB0 + ch, (shadow(port, 0xB0 + ch) & 0x20) | (f >> 8));
}

void MusicPlayer::keyOff(int voice) {
    const int port = driver_.port[static_cast<std::size_t>(voice)];
    const int reg = 0xB0 + driver_.channel[static_cast<std::size_t>(voice)];
    write(port, reg, shadow(port, reg) & 0xDF);
}

void MusicPlayer::noteOn(int voice) {
    Voice& v = voices_[static_cast<std::size_t>(voice)];
    if (voice == drumVoice_ && digital_) {
        // Drums as samples: louder notes pick the stronger hits.
        if (v.r[kFading] == 0 && onSample) {
            const int volume = v.r[kVolume];
            onSample(volume < 0x25 ? 4 : volume < 0x2A ? 3 : volume < 0x32 ? 2 : volume < 0x37 ? 1 : 0);
        }
        return;
    }
    setLevels(voice);
    v.r[kNoiseCount] = mem_[static_cast<std::size_t>(instrument(voice) + 0x38)];
    if (v.r[kNoiseCount] != 0) {
        return; // a noise instrument: noiseStep() keys it on
    }
    setFrequency(voice);
    const int port = driver_.port[static_cast<std::size_t>(voice)];
    const int reg = 0xB0 + driver_.channel[static_cast<std::size_t>(voice)];
    write(port, reg, shadow(port, reg) | 0x20);
}

void MusicPlayer::loadInstrument(int voice) {
    Voice& v = voices_[static_cast<std::size_t>(voice)];
    const int port = driver_.port[static_cast<std::size_t>(voice)];
    const int ch = driver_.channel[static_cast<std::size_t>(voice)];
    const auto op = [&](int k) { return driver_.operators[static_cast<std::size_t>(4 * voice + k)]; };
    const int ops = voice < 6 ? 4 : 2;
    // Silence the operators first.
    write(port, 0x80 + op(0), 0xFF);
    write(port, 0x80 + op(1), 0xFF);
    write(port, 0x40 + op(0), 0x3F);
    write(port, 0x40 + op(1), 0x3F);
    if (voice < 6) {
        write(port, 0x40 + op(2), 0x3F);
        write(port, 0x40 + op(3), 0x3F);
        write(port, 0x80 + op(2), 0xFF);
        write(port, 0x80 + op(3), 0xFF);
    }
    const std::size_t ins = instrument(voice);
    const auto m = [&](std::size_t at) { return static_cast<int>(mem_[(ins + at) & 0xFFFF]); };
    v.r[kConnection] = u8(m(0x0D));
    const int feedback = (m(0x0A) << 1) & 0xFF;
    if (voice < 6) {
        const int p = pan(voice);
        write(port, 0xC0 + ch, ((s8(v.r[kConnection]) >> 1) & 0xFF) | feedback | p);
        write(port, 0xC3 + ch, (v.r[kConnection] & 1) | feedback | p);
    } else {
        const int c = ((~v.r[kConnection]) & 1) | feedback;
        write(2, 0xC0 + ch, c | 0x10);
        write(1, 0xC0 + ch, c | 0x20);
    }
    for (int k = 0; k < ops; ++k) {
        const std::size_t rec = static_cast<std::size_t>(14 * k);
        v.r[static_cast<std::size_t>(kOpLevel + k)] = u8(m(rec + 6));
        write(port, 0x20 + op(k),
              ((((((m(rec + 0x0B) << 1) | m(rec + 0x0C)) << 1 | m(rec + 4)) << 1 | m(rec + 5)) << 4) | m(rec + 9)));
        write(port, 0x60 + op(k), (m(rec) << 4) | m(rec + 1));
        write(port, 0x80 + op(k), (m(rec + 2) << 4) | m(rec + 3));
        write(port, 0xE0 + op(k), m(rec + 8));
    }
    const int c = v.r[kConnection];
    write(port, 0x40 + op(0), (m(0x07) << 6) | ((c & 2) ? 0x3F : 0x3F - m(0x06)));
    write(port, 0x40 + op(1), (m(0x15) << 6) | (c == 1 ? 0x3F : 0x3F - m(0x14)));
    if (ops > 2) {
        write(port, 0x40 + op(3), (m(0x31) << 6) | 0x3F);
        write(port, 0x40 + op(2), (m(0x23) << 6) | (c == 3 ? 0x3F : 0x3F - m(0x22)));
    }
    v.r[kNoiseCount] = u8(m(0x38));
    v.noiseMask = static_cast<std::uint16_t>(m(0x3A) | (m(0x3B) << 8));
    v.noise = static_cast<std::uint16_t>(m(0x3C) | (m(0x3D) << 8));
    v.noiseStep = static_cast<std::uint16_t>(m(0x3E) | (m(0x3F) << 8));
}

// --- the per-tick work ------------------------------------------------------

void MusicPlayer::update(int voice) {
    Voice& v = voices_[static_cast<std::size_t>(voice)];
    auto& r = v.r;
    if (r[kTicks] == 0) {
        return;
    }
    if (r[kRelease] != 0 && --r[kRelease] == 0) {
        keyOff(voice);
    }
    bool refresh = false;
    if (--r[kTicks] == 0) {
        interpret(voice, refresh);
    }
    if (r[kBend] != 0) {
        bend(voice);
    }
    refresh = false;
    if (r[kFadeCount] != 0 || r[kPanCount] != 0) {
        if (--r[kFadeCount] == 0) {
            r[kFadeCount] = r[kFadeEvery];
            if (r[kFadeStep] != 0) {
                r[kAttenuation] = u8(r[kAttenuation] + r[kFadeStep]);
                refresh = true;
            }
        }
        if (--r[kPanCount] == 0) {
            r[kPanCount] = r[kPanEvery];
            if (r[kPanStep] != 0) {
                r[kPan] = u8(r[kPan] + r[kPanStep]);
                setPan(voice);
                refresh = true;
            }
        }
        if (refresh) {
            setLevels(voice);
        }
    }
}

// Reads the voice's stream up to and including its next note.
void MusicPlayer::interpret(int voice, bool& refresh) {
    Voice& v = voices_[static_cast<std::size_t>(voice)];
    auto& r = v.r;
    const auto at = [&](int k) -> std::uint8_t& { return mem_[static_cast<std::uint16_t>(v.pc + k)]; };
    const auto word = [&](int k) {
        return static_cast<std::uint16_t>(at(k) | (at(k + 1) << 8));
    };
    for (int guard = 0; guard < 4096; ++guard) {
        const int op = at(0);
        if (op < 0xBC) {
            r[kNote] = u8(op);
            r[kTicks] = at(1);
            v.pc = static_cast<std::uint16_t>(v.pc + 2);
            if (r[kNote] != 0 && r[kTicks] != 0) {
                r[kRelease] = r[kHold] != 0 ? r[kHold] : u8(r[kTicks] - r[kGap]);
                noteOn(voice);
            } else {
                if (refresh) {
                    setLevels(voice);
                }
                keyOff(voice);
            }
            return;
        }
        refresh = false;
        const int a = at(1);
        const int b = at(2);
        auto skip = [&](int n) { v.pc = static_cast<std::uint16_t>(v.pc + n); };
        switch (op) {
        case 0xBC: case 0xBE: case 0xC0: case 0xC1:
            skip(2);
            break;
        case 0xC3: // play a sample
            if (digital_ && r[kFading] == 0 && onSample) {
                onSample(s8(a));
            }
            skip(2);
            break;
        case 0xBD: case 0xBF: case 0xD5: case 0xD6: case 0xD7: case 0xD8: case 0xD9: case 0xDA:
        case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF: case 0xE1: case 0xE3: case 0xE4:
            skip(3);
            break;
        case 0xC2:
            skip(4);
            break;
        case 0xC4: // a routine of the driver's own
            if (const auto s = driver_.routine(word(1))) {
                std::uint8_t first = s->first;
                std::uint8_t second = s->second;
                if (random() & 4) {
                    std::swap(first, second);
                }
                for (std::size_t k = 0; k < 4; ++k) {
                    mem_[s->at[k]] = s->takesSecond[k] ? second : first;
                }
            }
            skip(3);
            break;
        case 0xC5: case 0xC6: case 0xC7: case 0xC8:
            skip(5);
            break;
        case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE:
        case 0xCF: case 0xD0: case 0xD1: case 0xD2: case 0xD3: case 0xD4: {
            // Compare a variable with a number (or another variable), then
            // call (C9-CC) or jump (CD-D4) when it holds.
            const int var = vars_[static_cast<std::size_t>(a)];
            const int k = s8(b);
            const int other = vars_[static_cast<std::size_t>(b)];
            bool holds = false;
            switch (op) {
            case 0xC9: case 0xD1: holds = k < var; break;
            case 0xCA: case 0xD2: holds = k > var; break;
            case 0xCB: case 0xD3: holds = k != var; break;
            case 0xCC: case 0xD4: holds = k == var; break;
            case 0xCD: holds = other < var; break;
            case 0xCE: holds = other > var; break;
            case 0xCF: holds = other != var; break;
            default: holds = other == var; break;
            }
            if (!holds) {
                skip(5);
            } else {
                if (op <= 0xCC) {
                    v.ret = static_cast<std::uint16_t>(v.pc + 5);
                }
                v.pc = word(3);
            }
            break;
        }
        case 0xE0: // waits forever in the driver; we stop the voice instead
            r[kTicks] = 0;
            return;
        case 0xE2:
            vars_[static_cast<std::size_t>(a)] = u8(vars_[static_cast<std::size_t>(a)] - b);
            skip(3);
            break;
        case 0xE5:
            vars_[static_cast<std::size_t>(a)] = u8(vars_[static_cast<std::size_t>(a)] - 1);
            skip(2);
            break;
        case 0xE6:
            vars_[static_cast<std::size_t>(a)] = u8(vars_[static_cast<std::size_t>(a)] + 1);
            skip(2);
            break;
        case 0xE7: // write a variable into the stream ahead
            at(2 + s8(b) + 1) = vars_[static_cast<std::size_t>(a)];
            skip(3);
            break;
        case 0xE8:
            vars_[static_cast<std::size_t>(a)] = vars_[static_cast<std::size_t>(b)];
            skip(3);
            break;
        case 0xE9:
            vars_[static_cast<std::size_t>(a)] = u8(b);
            skip(3);
            break;
        case 0xEA: { // write the variable'th byte of a list into the stream ahead
            const int n = s8(b);
            const std::uint8_t value = at(3 + vars_[static_cast<std::size_t>(a)]);
            const int offset = s8(at(3 + n));
            at(3 + n + offset + 1) = value;
            skip(n + 4);
            break;
        }
        case 0xEB: { // write a random number in a range into the stream ahead
            const int lo = s8(a);
            const int hi = s8(b);
            const int span = hi - lo + 1;
            const int pick = span > 0 ? (random() & 0x7FFF) % span : 0;
            const int offset = s8(at(3));
            at(3 + offset + 1) = u8(lo + pick);
            skip(4);
            break;
        }
        case 0xEC: { // write a random byte of a list into the stream ahead
            const int n = s8(a);
            const int pick = n > 0 ? (random() & 0x7FFF) % n : 0;
            const std::uint8_t value = at(2 + pick);
            const int offset = s8(at(2 + n));
            at(2 + n + offset + 1) = value;
            skip(n + 3);
            break;
        }
        case 0xED:
            skip(s8(a) + 3);
            break;
        case 0xEE:
            r[kTranspose] = u8(a);
            skip(2);
            break;
        case 0xEF:
            r[kPanEvery] = u8(a);
            r[kPanStep] = u8(b);
            r[kPanCount] = 1;
            skip(3);
            break;
        case 0xF0:
            r[kPan] = u8(a);
            skip(2);
            setPan(voice);
            refresh = true;
            break;
        case 0xF1: {
            const int attenuation = (s8(a) >> 1) - 0x32;
            if (r[kFading] != 0) {
                if (s8(r[kAttenuation]) > attenuation) {
                    r[kAttenuation] = u8(attenuation);
                }
            } else {
                r[kAttenuation] = u8(attenuation);
                refresh = true;
            }
            skip(2);
            break;
        }
        case 0xF2:
            r[kDetune] = u8(a);
            skip(2);
            break;
        case 0xF3:
            if (r[kFading] == 0) {
                r[kFadeEvery] = u8(a);
                r[kFadeStep] = u8(b);
                r[kFadeCount] = 1;
            }
            skip(3);
            break;
        case 0xF4:
            r[kVolume] = u8(s8(a) >> 1);
            skip(2);
            refresh = true;
            break;
        case 0xF5:
            r[kBend] = u8(a);
            skip(2);
            break;
        case 0xF6:
            r[kHold] = u8(a);
            r[kGap] = 0;
            skip(2);
            break;
        case 0xF7:
            r[kGap] = u8(a);
            r[kHold] = 0;
            skip(2);
            break;
        case 0xF8:
            r[kInstrument] = u8(a);
            skip(2);
            loadInstrument(voice);
            break;
        case 0xF9: // return
            if (v.ret != 0) {
                v.pc = v.ret;
                v.ret = 0;
            } else {
                skip(1);
            }
            break;
        case 0xFA: // call
            v.ret = static_cast<std::uint16_t>(v.pc + 3);
            v.pc = word(1);
            break;
        case 0xFB: // jump
            v.pc = word(1);
            break;
        case 0xFC: // new start
            v.pc = v.start = v.mark = v.outerMark = v.restart = word(1);
            break;
        case 0xFD: // back to the start
            if (v.restart == 0xFFFF) {
                r[kTicks] = 0;
                return;
            }
            v.pc = v.start = v.mark = v.outerMark = v.restart;
            for (int k : {kBend, kFadeStep, kPanStep, kDetune, kAttenuation, kVolume, kFadeCount, kPanCount, kGap}) {
                r[static_cast<std::size_t>(k)] = 0;
            }
            v.innerCount = v.outerCount = 0;
            break;
        case 0xFE: // repeat from the outer mark; 0 sets the mark
            if (v.outerCount == 0) {
                if (a == 0) {
                    skip(2);
                    v.outerMark = v.mark = v.pc;
                    v.innerCount = v.outerCount = 0;
                } else {
                    v.outerCount = s8(a);
                    v.pc = v.mark = v.outerMark;
                }
            } else if (--v.outerCount == 0) {
                skip(2);
                v.outerMark = v.mark = v.pc;
            } else {
                v.pc = v.mark = v.outerMark;
            }
            break;
        case 0xFF: // repeat from the inner mark; 0 sets the mark
            if (v.innerCount == 0) {
                if (a == 0) {
                    skip(2);
                    v.mark = v.pc;
                    v.innerCount = 0;
                } else {
                    v.innerCount = s8(a);
                    v.pc = v.mark;
                }
            } else if (--v.innerCount == 0) {
                skip(2);
                v.mark = v.pc;
            } else {
                v.pc = v.mark;
            }
            break;
        default:
            r[kTicks] = 0;
            return;
        }
    }
    r[kTicks] = 0; // a stream that never reaches a note
}

void MusicPlayer::fadeStep() {
    if (!fading_) {
        return;
    }
    for (Voice& v : voices_) {
        auto& r = v.r;
        if (r[kTicks] == 0 || r[kFading] == 0) {
            continue;
        }
        const int level = u8(r[kAttenuation] + r[kVolume]);
        if (level > 0x78 || level <= 8) {
            v.pc = driver_.stopStream;
            r[kFading] = 0;
        } else {
            r[kFadeStep] = 0xFE;
            r[kFadeEvery] = 4;
            if (r[kFadeCount] == 0) {
                r[kFadeCount] = 1;
            }
        }
    }
}

void MusicPlayer::noiseStep(int voice) {
    Voice& v = voices_[static_cast<std::size_t>(voice)];
    if (v.r[kNoiseCount] == 0) {
        return;
    }
    v.noise = static_cast<std::uint16_t>(v.noise + v.noiseStep);
    if (--v.r[kNoiseCount] == 0) {
        const int port = driver_.port[static_cast<std::size_t>(voice)];
        const int ch = driver_.channel[static_cast<std::size_t>(voice)];
        write(port, 0xA0 + ch, v.noise & 0xFF);
        write(port, 0xB0 + ch, (v.noise >> 8) | 0x20);
    }
}

} // namespace viceroy::sound
