// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/game/hall_of_fame.hpp"

#include <algorithm>
#include <cstdint>

namespace viceroy {

namespace {

constexpr std::size_t kRecord = 42;
constexpr std::size_t kNameBytes = 24;
constexpr std::size_t kNation = 24;
constexpr std::size_t kDeclared = 26;
constexpr std::size_t kWon = 28;
constexpr std::size_t kYear = 30;
constexpr std::size_t kSeason = 32;
constexpr std::size_t kDifficulty = 34;
constexpr std::size_t kScore = 36;
constexpr std::size_t kRating = 38;
constexpr std::size_t kLevel = 40;
constexpr int kPlacesHeld = kHallPlaces + 1;

int word(std::span<const std::uint8_t> b, std::size_t at) {
    return static_cast<std::int16_t>(b[at] | (b[at + 1] << 8));
}

void put(Bytes& b, std::size_t at, int value) {
    b[at] = static_cast<std::uint8_t>(value & 0xFF);
    b[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

} // namespace

Exploits exploitsFor(int score, int difficulty) {
    // Read from the original: the score weighed by the difficulty (4, 5,
    // 6, 8 or 10 in a hundred), then the last step of 1 to 24 whose square
    // over three stays below it; the rating shown is half the weighed
    // score.
    Exploits out;
    if (score <= 0) {
        return out;
    }
    int weight = difficulty + 4;
    if (difficulty >= 3) {
        ++weight;
    }
    if (difficulty >= 4) {
        ++weight;
    }
    const int weighed = weight * score / 100;
    for (int i = 1; i <= 24; ++i) {
        if (i * i / 3 < weighed) {
            out.level = i - 1;
        }
    }
    out.level = std::min(out.level, 23);
    out.rating = weighed >> 1;
    return out;
}

std::vector<HallEntry> readHallOfFame(std::span<const std::uint8_t> file) {
    std::vector<HallEntry> places(kPlacesHeld);
    if (file.size() < kRecord * kHallPlaces) {
        return places;
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(kHallPlaces); ++i) {
        const auto r = file.subspan(i * kRecord, kRecord);
        auto& e = places[i];
        const auto end = std::find(r.begin(), r.begin() + kNameBytes, std::uint8_t{0});
        e.name.assign(r.begin(), end);
        e.nation = word(r, kNation);
        e.declared = word(r, kDeclared) != 0;
        e.won = word(r, kWon) != 0;
        e.year = word(r, kYear);
        e.season = word(r, kSeason);
        e.difficulty = word(r, kDifficulty);
        e.score = word(r, kScore);
        e.rating = word(r, kRating);
        e.level = word(r, kLevel);
    }
    return places;
}

Bytes writeHallOfFame(const std::vector<HallEntry>& places) {
    Bytes out(kRecord * kHallPlaces, 0);
    for (std::size_t i = 0; i < static_cast<std::size_t>(kHallPlaces); ++i) {
        const HallEntry e = i < places.size() ? places[i] : HallEntry{};
        const std::size_t at = i * kRecord;
        const std::size_t n = std::min(e.name.size(), kNameBytes - 1);
        std::copy_n(e.name.begin(), n, out.begin() + static_cast<std::ptrdiff_t>(at));
        put(out, at + kNation, e.nation);
        put(out, at + kDeclared, e.declared ? 1 : 0);
        put(out, at + kWon, e.won ? 1 : 0);
        put(out, at + kYear, e.year);
        put(out, at + kSeason, e.season);
        put(out, at + kDifficulty, e.difficulty);
        put(out, at + kScore, e.score);
        put(out, at + kRating, e.rating);
        put(out, at + kLevel, e.level);
    }
    return out;
}

int enterHallOfFame(std::vector<HallEntry>& places, const HallEntry& entry) {
    places.resize(kPlacesHeld);
    int at = 0;
    while (at < kHallPlaces && places[static_cast<std::size_t>(at)].rating >= entry.rating) {
        ++at;
    }
    for (int j = kHallPlaces; j > at; --j) {
        places[static_cast<std::size_t>(j)] = places[static_cast<std::size_t>(j - 1)];
    }
    places[static_cast<std::size_t>(at)] = entry;
    return at;
}

} // namespace viceroy
