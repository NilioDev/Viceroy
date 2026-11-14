// SPDX-License-Identifier: GPL-3.0-or-later
//
// A European unit explores a lost city rumour: a Fountain of Youth, one of
// the Seven Cities of Cibola, ruins, burial mounds, a friendly tribe's gift,
// survivors of a lost colony, holy shrines, nothing, or the expedition
// vanishes. Read from the original with a debugger; see docs/rules/rumours.md.
#include "viceroy/game/europe.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"
#include "viceroy/sound/background_music.hpp"

#include <algorithm>
#include <string>

namespace viceroy {

namespace {

constexpr int kDeSoto = 7; // row of @FATHERS
constexpr int kSeasonedScout = 0x16;
constexpr int kTreasure = 10;
constexpr std::uint8_t kIndependence = 0x01; // header byte 18
constexpr std::size_t kPlayerFlags = 48;
constexpr std::uint8_t kMoundsSeen = 0x40; // the nation has met burial mounds
constexpr int kFreeRecruits = 8;

// Tunes of their own (driver commands), read from the original.
constexpr int kFountainTune = 0x37;
constexpr int kCibolaTune = 0x3C;
constexpr int kMoundsTune = 0x33;
constexpr int kTreasureTune = 0x24;
constexpr int kNativesTune = 0x32;

// The outcomes, as the numbers of the LOSTCITY sections.
enum Outcome {
    kFountain = 1,
    kCibola = 2,
    kRuins = 3,
    kMounds = 4,
    kVanish = 5,
    kNothing = 6,
    kTribeGift = 7,
    kShrines = 8,
    kSurvivors = 9,
};

// The square as the original's rumours see it: the terrain (0-26), 27 for
// mountains and 28 for hills.
int squareKind(const SaveMap& map, int x, int y) {
    if (!map.inside(x, y)) {
        return 25;
    }
    const auto& sq = map.at(x, y);
    if ((sq.relief & 1) != 0) {
        return (sq.relief & 4) != 0 ? 27 : 28;
    }
    return sq.terrain;
}

int distance(int dx, int dy) {
    dx = std::abs(dx);
    dy = std::abs(dy);
    return std::max(dx, dy) + std::min(dx, dy) / 2;
}

// The nearest settlement (a later one wins a tie) and how far it is.
std::pair<int, int> nearestSettlement(const SaveGame& save, int x, int y) {
    int best = -1;
    int far = 9999;
    for (std::size_t i = 0; i < save.settlements.size(); ++i) {
        const auto& s = save.settlements[i];
        const int d = distance(s.x - x, s.y - y);
        if (d <= far) {
            far = d;
            best = static_cast<int>(i);
        }
    }
    return {best, far};
}

bool met(const SaveGame& save, int nation, int tribe) {
    return (save.nations[static_cast<std::size_t>(nation)]
                .raw[52 + static_cast<std::size_t>(kEuropeanNations + tribe)] &
            0x20) != 0;
}

} // namespace

bool Game::exploreRumour(int index) {
    if (index < 0 || index >= unitCount()) {
        return true;
    }
    const SaveUnit explorer = unit(index);
    const int nation = explorer.nation;
    if (nation < 0 || nation >= kEuropeanNations) {
        return true;
    }
    const int x = explorer.x;
    const int y = explorer.y;
    const bool human = nation == this->human();
    // A scout counts 1, a seasoned scout 2, and de Soto adds one more and
    // turns every bad outcome into another roll.
    int scout = explorer.type == unit_type::kScout ? (explorer.profession == kSeasonedScout ? 2 : 1) : 0;
    const bool deSoto = scout > 0 && hasFather(nation, kDeSoto);
    if (deSoto) {
        ++scout;
    }
    ++rumoursExplored_;
    const int kind = squareKind(save_.map, x, y);
    const bool independent = (save_.header[18] & kIndependence) != 0;
    int least = 0;
    int outcome = kNothing;
    int quality = 0;
    int gold = 0;
    int treasure = 0; // in hundreds
    int shrineTribe = -1;
    int anger = 0;
    for (;;) {
        outcome = std::max(1 + nextRandom(9), least = std::min(least + 1, 3));
        quality = 1 + nextRandom(100) + scout * 10;
        // A scout often escapes vanishing and the shrines' anger.
        if ((outcome == kVanish || outcome == kShrines) && nextRandom(scout + 1) != 0) {
            if (deSoto) {
                continue;
            }
            outcome = kNothing;
        }
        if (outcome == kFountain) {
            // Only on grassland, savannah, marsh or swamp (and their forests),
            // and not among the first three rumours.
            const bool south = kind < 24 && (kind & 7) >= 4;
            if (!(south && rumoursExplored_ >= 4) && !deSoto) {
                outcome = quality <= 10 ? kVanish : kNothing;
            }
            if (independent) {
                outcome = kCibola;
            }
        }
        if (outcome == kCibola) {
            // Only in hills, mountains or desert, seven at most.
            bool rich = kind == 27 || kind == 28 || (kind < 24 && (kind & 7) == 1);
            if (deSoto && nextRandom(3) == 0) {
                rich = true;
            }
            if (rich && citiesFound_ < 7) {
                treasure = 1 + nextRandom(20) + (scout + 2) * 10;
                if (unitCount() >= kMaxUnits) {
                    return true;
                }
                addBuiltUnit(x, y, nation, kTreasure);
                save_.units.back().profession = treasure;
                save_.units.back().raw[23] = static_cast<std::uint8_t>(treasure);
                ++citiesFound_;
            } else {
                outcome = quality <= 10 ? kVanish : quality < 25 ? kShrines : kNothing;
            }
        }
        if (outcome == kShrines) {
            if (deSoto) {
                continue;
            }
            // The original takes the nearest settlement's number for its
            // distance here: only the first three settlements have shrines.
            const auto [settlement, far] = nearestSettlement(save_, x, y);
            (void)far;
            if (settlement >= 0 && settlement <= 2) {
                const int tribe = tribeOf(save_.settlements[static_cast<std::size_t>(settlement)]);
                anger = 1 + nextRandom(6) + (save_.difficulty - scout + 1) * 5;
                shrineTribe = tribe;
                if (!met(save_, nation, tribe)) {
                    shrineTribe = -1;
                    outcome = kNothing;
                }
            } else {
                outcome = kNothing;
            }
        }
        if (outcome == kRuins) {
            gold = (3 + nextRandom(8) + nextRandom(8) + nextRandom(8)) * 10;
            if (scout > 0) {
                gold = (scout + 2) * gold / 2;
            }
        }
        if (outcome == kTribeGift) {
            gold = (4 + nextRandom(10) + nextRandom(10) + nextRandom(10) + nextRandom(10)) * 2;
        }
        if (outcome == kVanish) {
            // A small nation does not lose its people this way, nor often a
            // pioneer of a nation of eight or fewer; and the first time a
            // nation would, it finds burial mounds instead.
            int colonies = 0;
            for (const auto& c : save_.colonies) {
                colonies += c.nation == nation ? 1 : 0;
            }
            const int people = nationPeople(nation);
            if (people <= 4 && colonies <= 2) {
                outcome = kNothing;
            }
            if (explorer.type == unit_type::kPioneer && people <= 8 && nextRandom(2) == 0) {
                outcome = kNothing;
            }
            auto& flags = save_.players[static_cast<std::size_t>(nation)].raw[kPlayerFlags];
            if ((flags & kMoundsSeen) == 0) {
                flags = static_cast<std::uint8_t>(flags | kMoundsSeen);
                outcome = kMounds;
            }
        }
        if (outcome == kVanish && deSoto) {
            outcome = kNothing;
        }
        if (outcome == kNothing && deSoto) {
            continue;
        }
        break;
    }

    const auto tribeName = [&](int tribe) {
        const auto t = static_cast<std::size_t>(tribe);
        return tribe >= 0 && t < rules_.tribes.size() ? rules_.tribes[t].adjective : std::string{};
    };
    if (human) {
        GameNotice notice{"LOSTCITY" + std::to_string(outcome), {}};
        notice.numbers = {gold, treasure * 100};
        notice.unit = index;
        if (outcome == kShrines) {
            notice.strings = {tribeName(shrineTribe)};
        }
        if (outcome == kSurvivors) {
            notice.strings = {""};
            notice.countries = {{0, nation}};
        }
        // Read from the original: three finds have a tune of their own; an
        // expedition lost makes the next tune a sad one, gold a cheerful
        // one (at once without Background Music).
        const bool background = sound::SoundOptions::fromSave(soundOptionBits()).background;
        const auto mood = [&](int m) {
            notice.music = {{MusicCue::Kind::SetMood, m}};
            if (!background) {
                notice.music.push_back({MusicCue::Kind::ChangeMood, m});
            }
        };
        if (outcome == kFountain) {
            notice.music = {{MusicCue::Kind::Pick, kFountainTune}};
        } else if (outcome == kCibola) {
            notice.music = {{MusicCue::Kind::Pick, kCibolaTune}};
        } else if (outcome == kMounds) {
            notice.music = {{MusicCue::Kind::Pick, kMoundsTune}};
        } else if (outcome == kVanish) {
            mood(1);
        } else if (gold > 0) {
            mood(2);
        }
        notices_.push_back(std::move(notice));
    }
    if (anger != 0 && shrineTribe >= 0) {
        changeTension(shrineTribe, nation, anger);
    }
    if (outcome == kMounds) {
        burial_ = Burial{nation, x, y, scout, quality};
        if (!human) {
            answerBurial(true); // a computer player always searches
        }
    }
    bool kept = true;
    switch (outcome) {
    case kSurvivors:
        if (unitCount() < kMaxUnits) {
            addColonist(x, y, nation);
        }
        break;
    case kFountain:
        // Eight free recruits from the docks: the player picks each one, a
        // computer player takes the second of the three every time.
        for (int i = 0; i < kFreeRecruits; ++i) {
            if (human) {
                notices_.push_back(GameNotice{"LOSTCITY0", {}});
            } else {
                addFreeRecruit(nation, 1);
            }
        }
        break;
    case kVanish:
        removeUnit(index);
        refreshUnitFlags();
        kept = false;
        break;
    default:
        break;
    }
    save_.nations[static_cast<std::size_t>(nation)].gold += gold;
    return kept;
}

void Game::answerBurial(bool search) {
    const Burial b = burial_;
    burial_ = Burial{};
    if (b.nation < 0 || !search) {
        return;
    }
    searchBurial(b);
}

void Game::searchBurial(const Burial& b) {
    const bool human = b.nation == this->human();
    // The mounds may belong to the nearest settlement's tribe: the closer,
    // the likelier, and less so for a scout.
    int tribe = -1;
    const auto [settlement, far] = nearestSettlement(save_, b.x, b.y);
    if (settlement >= 0 && 1 + nextRandom((far + 5) << b.scout) <= 3) {
        tribe = tribeOf(save_.settlements[static_cast<std::size_t>(settlement)]);
    }
    if (tribe >= 0 && !met(save_, b.nation, tribe)) {
        tribe = -1;
    }
    int found = 1;
    int gold = 0;
    int treasure = 0;
    if (b.quality < 25) {
        found = 1;
    } else if (b.quality < 50 || (tribe < 0 && b.quality < 65)) {
        found = 2;
        gold = (3 + nextRandom(8) + nextRandom(8) + nextRandom(8)) * 10;
    } else {
        found = 3;
        treasure = (1 + nextRandom(8) + (b.scout + 5) * 2) * 2;
        if (unitCount() >= kMaxUnits) {
            return;
        }
        addBuiltUnit(b.x, b.y, b.nation, kTreasure);
        save_.units.back().profession = treasure;
        save_.units.back().raw[23] = static_cast<std::uint8_t>(treasure);
    }
    if (human) {
        GameNotice notice{"BURIAL" + std::to_string(found), {}};
        notice.numbers = {gold, treasure * 100};
        if (found == 3 && tribe < 0) {
            notice.music = {{MusicCue::Kind::Pick, kTreasureTune}}; // read from the original
        }
        notices_.push_back(std::move(notice));
    }
    if (tribe >= 0) {
        // "These are the burial grounds of our ancient Sioux fathers! ...
        // Now you must die!"
        if (human) {
            const auto t = static_cast<std::size_t>(tribe);
            GameNotice notice{"SCREWED", {t < rules_.tribes.size() ? rules_.tribes[t].adjective : std::string{}}};
            notice.music = {{MusicCue::Kind::Pick, kNativesTune}}; // read from the original
            notices_.push_back(std::move(notice));
        }
        changeTension(tribe, b.nation, 100);
    }
    save_.nations[static_cast<std::size_t>(b.nation)].gold += gold;
}

int Game::addFreeRecruit(int nation, int slot) {
    auto& n = save_.nations[static_cast<std::size_t>(nation)];
    if (slot < 0 || slot >= static_cast<int>(n.recruits.size()) || unitCount() >= kMaxUnits) {
        return -1;
    }
    const int profession = n.recruits[static_cast<std::size_t>(slot)];
    const int made = addUnitInEurope(professionUnitType(profession), profession, nation);
    n.recruits[static_cast<std::size_t>(slot)] = randomRecruit(nation);
    return made;
}

int Game::recruitFree(int slot) {
    return addFreeRecruit(human(), slot);
}

} // namespace viceroy
