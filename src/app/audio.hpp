// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app's sound: the music player from the original's sound driver,
// played through SDL on its own thread, and the choice of the next tune.
#pragma once

#include "viceroy/sound/background_music.hpp"
#include "viceroy/sound/music_driver.hpp"
#include "viceroy/sound/music_stream.hpp"

#include <SDL3/SDL.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>

class Audio {
public:
    Audio() = default;
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    // Reads the sound driver from the imported game and opens the sound
    // device. Without either, the game stays silent.
    void open(const std::filesystem::path& gameFolder);
    bool ready() const { return stream_ != nullptr; }

    viceroy::sound::BackgroundMusic& music() { return music_; }
    // Starts playing during a game; stops it outside one.
    void setPlaying(bool playing);
    // Call every frame: sends what the music asks for.
    void update(bool independence);
    // Sends one driver command now, when the options allow it.
    void send(int command);

private:
    static void SDLCALL feed(void* self, SDL_AudioStream* stream, int additional, int total);

    std::optional<viceroy::sound::MusicDriver> driver_;
    std::unique_ptr<viceroy::sound::MusicStream> music_stream_;
    viceroy::sound::BackgroundMusic music_;
    SDL_AudioStream* stream_ = nullptr;
    std::mutex mutex_;
    bool playing_ = false;
};
