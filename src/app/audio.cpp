// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio.hpp"

#include "viceroy/game_files.hpp"

#include <SDL3/SDL_time.h>

#include <fstream>
#include <iterator>
#include <vector>

namespace {

constexpr int kSampleRate = 44100;

} // namespace

Audio::~Audio() {
    if (stream_ != nullptr) {
        SDL_DestroyAudioStream(stream_);
    }
}

void Audio::open(const std::filesystem::path& gameFolder) {
    if (stream_ != nullptr) {
        return;
    }
    // The OPL3 driver, the one the original loads for Sound Blaster Pro
    // and 16 cards.
    const auto path = viceroy::findFileNoCase(gameFolder, "PSOUND.COL");
    if (!path) {
        SDL_Log("No sound driver in the game files; playing without sound.");
        return;
    }
    std::ifstream in(*path, std::ios::binary);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
    driver_ = viceroy::sound::MusicDriver::load(bytes);
    if (!driver_) {
        SDL_Log("Cannot read the sound driver; playing without sound.");
        return;
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("No sound device: %s", SDL_GetError());
        return;
    }
    music_stream_ = std::make_unique<viceroy::sound::MusicStream>(*driver_, kSampleRate);
    // The recordings for the drums and effects, as with a card that plays
    // samples.
    if (const auto coldig = viceroy::findFileNoCase(gameFolder, "COLDIG.BIN")) {
        std::ifstream recordings(*coldig, std::ios::binary);
        music_stream_->setRecordings({std::istreambuf_iterator<char>(recordings), {}});
    }
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, kSampleRate};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Audio::feed, this);
    if (stream_ == nullptr) {
        SDL_Log("Cannot open the sound device: %s", SDL_GetError());
        return;
    }
    // A new seed each run, as the original's tunes differ from one start
    // to the next.
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    music_ = viceroy::sound::BackgroundMusic(static_cast<std::uint32_t>(now / 1000000));
    SDL_ResumeAudioStreamDevice(stream_);
}

void Audio::setPlaying(bool playing) {
    if (!ready() || playing == playing_) {
        return;
    }
    playing_ = playing;
    if (!playing) {
        send(1);
    }
}

void Audio::update(bool independence) {
    if (!ready() || !playing_) {
        return;
    }
    std::lock_guard lock(mutex_);
    auto& player = music_stream_->player();
    for (const int command : music_.update(player.busy(), independence)) {
        player.command(command);
    }
}

void Audio::send(int command) {
    if (!ready() || !music_.allowed(command)) {
        return;
    }
    std::lock_guard lock(mutex_);
    music_stream_->player().command(command);
}

void SDLCALL Audio::feed(void* self, SDL_AudioStream* stream, int additional, int) {
    auto* audio = static_cast<Audio*>(self);
    std::vector<std::int16_t> pcm(static_cast<std::size_t>(additional) / sizeof(std::int16_t) / 2 * 2);
    if (pcm.empty()) {
        return;
    }
    {
        std::lock_guard lock(audio->mutex_);
        audio->music_stream_->render(pcm);
    }
    SDL_PutAudioStreamData(stream, pcm.data(), static_cast<int>(pcm.size() * sizeof(std::int16_t)));
}
