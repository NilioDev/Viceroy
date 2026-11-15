// SPDX-License-Identifier: GPL-3.0-or-later
//
// The woodcuts: a picture in a wooden frame the first time something
// happens to the human player. Read from the original; see
// docs/rules/woodcuts.md.
#include "viceroy/game/game.hpp"
#include "viceroy/sound/background_music.hpp"

namespace viceroy {

namespace {
constexpr std::size_t kWoodcutBits = 154;
constexpr int kWoodcuts = 13;
} // namespace

bool Game::woodcutShown(int n) const {
    if (n < 1 || n > 16) {
        return true;
    }
    return (save_.header[kWoodcutBits + static_cast<std::size_t>((n - 1) / 8)] & (1 << ((n - 1) % 8))) != 0;
}

std::optional<GameNotice> Game::woodcut(int n) {
    if (n < 1 || n > kWoodcuts || woodcutShown(n)) {
        return std::nullopt;
    }
    auto& bits = save_.header[kWoodcutBits + static_cast<std::size_t>((n - 1) / 8)];
    bits = static_cast<std::uint8_t>(bits | (1 << ((n - 1) % 8)));
    GameNotice notice{"WOODCUT", {}, n};
    // Each woodcut starts its own music, read from the original: the
    // discovery a cheerful mood at once (only with Event Music alone), the
    // first colony a cheerful event mood, the natives their tunes, the
    // cargo for Europe a cheerful mood at once.
    const auto options = sound::SoundOptions::fromSave(soundOptionBits());
    switch (n) {
    case 1:
        if (options.events && !options.background) {
            notice.music = {{MusicCue::Kind::ChangeMood, 2}};
        }
        break;
    case 2:
        notice.music = {{MusicCue::Kind::MoodEvent, 2}};
        break;
    case 3:
        notice.music = {{MusicCue::Kind::Pick, 0x33}};
        break;
    case 4:
        notice.music = {{MusicCue::Kind::Pick, 0x35}};
        break;
    case 5:
        notice.music = {{MusicCue::Kind::Pick, 0x36}};
        break;
    case 6:
        notice.music = {{MusicCue::Kind::Pick, 0x39}};
        break;
    case 9:
        notice.music = {{MusicCue::Kind::ChangeMood, 2}};
        break;
    default:
        break;
    }
    return notice;
}

void Game::pushWithWoodcut(GameNotice notice, int n) {
    if (auto picture = woodcut(n)) {
        auto music = std::move(notice.music);
        notice.music.clear();
        music.insert(music.end(), picture->music.begin(), picture->music.end());
        picture->music = std::move(music);
        notices_.push_back(std::move(*picture));
    }
    notices_.push_back(std::move(notice));
}

} // namespace viceroy
