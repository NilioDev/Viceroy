// SPDX-License-Identifier: GPL-3.0-or-later
//
// The end of the game: the war of independence won or lost, all colonies
// lost, the other nations' kings letting their colonies go, and the
// Viceroy's retirement. Read from the original with a debugger and a
// disassembler (we only looked at what it does and wrote it down in our own
// words); see docs/rules/the-end.md.
#include "viceroy/game/game.hpp"

#include <algorithm>
#include <string>

namespace viceroy {

namespace {

// Header.
constexpr std::size_t kHeaderWar = 18;
constexpr std::uint8_t kDeclared = 0x01;
constexpr std::uint8_t kRebelsWon = 0x08;
constexpr std::uint8_t kTheEnd = 0x10;          // the end has come (scored)
constexpr std::uint8_t kCrownTookColony = 0x40; // the Crown has taken a colony in the war
constexpr std::size_t kHeaderHints = 22;
constexpr std::uint8_t kToldHowToWin = 0x01;
constexpr std::size_t kHeaderIndependenceWon = 50; // a word
constexpr std::size_t kHeaderKeepPlaying = 82;     // a word
constexpr std::size_t kHeaderPower = 100;          // the power that helps the rebels (a word)
constexpr std::size_t kHeaderRoyalForce = 106;     // regulars, dragoons, men-of-war, artillery

// Nation record.
constexpr std::size_t kNationFlags = 0;
constexpr std::uint8_t kUSA = 0x04;
constexpr std::size_t kRebelShare = 0x19; // its rebels in %
constexpr std::size_t kRebelsTold = 0x1A; // the rebels last reported by the spies
constexpr std::size_t kRelations = 52;
constexpr std::uint8_t kPeace = 0x40;

constexpr std::uint8_t kCoastal = 0x40; // colony byte 28: by the water

constexpr int kRegulars = 6; // rows of @UNIT
constexpr int kCavalry = 8;
constexpr int kArtillery = 11;

constexpr int kIndependenceMood = 3;

template <std::size_t N>
int word(const std::array<std::uint8_t, N>& raw, std::size_t at) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(raw[at] | (raw[at + 1] << 8)));
}

template <std::size_t N>
void setWord(std::array<std::uint8_t, N>& raw, std::size_t at, int value) {
    const auto v = static_cast<std::uint16_t>(value);
    raw[at] = static_cast<std::uint8_t>(v & 0xFF);
    raw[at + 1] = static_cast<std::uint8_t>(v >> 8);
}

} // namespace

bool Game::gameOver() const {
    return (save_.header[kHeaderWar] & kTheEnd) != 0 && word(save_.header, kHeaderKeepPlaying) == 0;
}

void Game::keepPlaying() { setWord(save_.header, kHeaderKeepPlaying, 1); }

void Game::checkTheEnd() {
    auto& war = save_.header[kHeaderWar];
    const bool atWar = (war & kDeclared) != 0;
    const int h = human();
    const int crown = crownNation();
    const auto& player = save_.players[static_cast<std::size_t>(h)];
    const auto coloniesOf = [&](int nation) {
        return static_cast<int>(std::count_if(save_.colonies.begin(), save_.colonies.end(),
                                              [&](const SaveColony& c) { return c.nation == nation; }));
    };
    const auto colonistsOf = [&](int nation) {
        int people = 0;
        for (const auto& c : save_.colonies) {
            people += c.nation == nation ? c.population : 0;
        }
        return people;
    };
    const int colonies = coloniesOf(h);
    // The game goes on (true) or it ends now; a win or a retirement asks
    // whether to keep playing.
    bool scored = true;
    bool ended = false;
    const auto lost = [&] {
        // Read from the original: the final reckoning comes the first time
        // (not built yet), and the game is over even after "Keep playing".
        setWord(save_.header, kHeaderKeepPlaying, 0);
        scored = false;
        ended = true;
    };

    if (save_.year >= 1600 && colonies == 0 && !atWar) {
        // "Viceroy Peter Minuit.  Our efforts in the New World have proven
        // fruitless ..."
        GameNotice notice{"LOSENOCOLONIES", {"", player.name}};
        notice.lines = {{0, true, "DIFFICULTY", save_.difficulty}};
        notices_.push_back(std::move(notice));
        lost();
    } else if (atWar && (war & kRebelsWon) == 0) {
        bool won = false;
        if (crown >= 0 && coloniesOf(crown) == 0) {
            // The Crown holds none of the rebels' colonies. Its land units
            // in the New World must be gone (fewer than 8 once it has taken
            // a colony), and what is left at home too small.
            const int royal =
                static_cast<int>(std::count_if(save_.units.begin(), save_.units.end(), [&](const SaveUnit& u) {
                    return u.nation == crown && (u.type == kRegulars || u.type == kCavalry || u.type == kArtillery);
                }));
            const int fewer = (war & kCrownTookColony) != 0 ? 8 : 1;
            const int home = word(save_.header, kHeaderRoyalForce) +
                             (word(save_.header, kHeaderRoyalForce + 2) > 0 ? 1 : 0) +
                             (word(save_.header, kHeaderRoyalForce + 6) > 0 ? 1 : 0);
            won = royal < fewer && home < 4;
        }
        if (won) {
            // "Royal Expeditionary Force annihilated! General ... ", then
            // the King lets the colonies go.
            GameNotice winning{"WINNING", {player.name, player.country}};
            winning.music = {{MusicCue::Kind::ChangeMood, kIndependenceMood}};
            notices_.push_back(std::move(winning));
            notices_.push_back(GameNotice{"KINGLOSE", {}});
            war |= kRebelsWon;
            ended = true; // nothing more is looked at this turn
        } else {
            int ports = 0;
            for (const auto& c : save_.colonies) {
                ports += c.nation == h && (c.raw[28] & kCoastal) != 0 ? 1 : 0;
            }
            // The Crown's share of the colonists, each side counted one more.
            const int theirs = colonistsOf(crown) + 1;
            const int ours = colonistsOf(h) + 1;
            const int share = theirs * 100 / (theirs + ours);
            int warning = ports < 3 ? 1 : 0;
            int losing = ports == 0 ? 1 : 0;
            if (share >= 80) {
                warning = 3;
            }
            if (share >= 90) {
                losing = 3;
            }
            if (colonies < 3) {
                warning = 2;
            }
            if (colonies == 0) {
                losing = 2;
            }
            if (losing != 0) {
                // "King's Forces control all ports in ...!", and the King
                // gloats.
                GameNotice notice{"LOSING" + std::to_string(losing), {player.country, player.name}};
                notice.countries = {{2, word(save_.header, kHeaderPower)}};
                notices_.push_back(std::move(notice));
                GameNotice king{"KINGWIN", {}};
                king.countries = {{0, h}};
                notices_.push_back(std::move(king));
                lost();
            } else if (warning != 0) {
                notices_.push_back(
                    GameNotice{"WARN" + std::to_string(warning), {player.country}, -1, {ports, colonies, share}});
            }
        }
    }
    if (!ended && !atWar) {
        othersGranted();
    }
    if (!ended && (war & kTheEnd) == 0) {
        const bool declared = (war & kDeclared) != 0;
        if (save_.season == 0 && ((save_.year == 1790 && !declared) || save_.year == 1840)) {
            // "Viceroy Peter Minuit plans to retire in 1800!"
            GameNotice notice{save_.year == 1790 ? "SOONRETIRING0" : "SOONRETIRING1", {"", player.name}};
            notice.lines = {{0, true, "DIFFICULTY", save_.difficulty}};
            notices_.push_back(std::move(notice));
        }
        if ((save_.year == 1800 && !declared) || save_.year == 1850) {
            // He retires near his biggest colony (the last of the biggest;
            // the first colony if he has none).
            int biggest = 0;
            int most = 0;
            for (std::size_t c = 0; c < save_.colonies.size(); ++c) {
                const auto& colony = save_.colonies[c];
                if (colony.nation == h && colony.population >= most) {
                    most = colony.population;
                    biggest = static_cast<int>(c);
                }
            }
            const std::string place =
                save_.colonies.empty() ? std::string{} : save_.colonies[static_cast<std::size_t>(biggest)].name;
            GameNotice notice{declared ? "RETIRING2" : "RETIRING", {"", player.name, place}};
            notice.lines = {{0, true, "DIFFICULTY", save_.difficulty}};
            notices_.push_back(std::move(notice));
            setWord(save_.header, kHeaderKeepPlaying, 0);
            ended = true;
        }
    }
    if (word(save_.header, kHeaderKeepPlaying) != 0) {
        return; // the player kept playing after the end
    }
    if ((war & kRebelsWon) != 0 || ended) {
        if (scored) {
            if ((war & kRebelsWon) != 0) {
                setWord(save_.header, kHeaderIndependenceWon, 1);
            }
            // "Scoring for this game is now complete." (the score itself is
            // not built yet): "That's all." or "Keep playing anyway."
            notices_.push_back(GameNotice{"SCORED", {}});
        }
        war |= kTheEnd;
    }
}

void Game::othersGranted() {
    // Read from the original: a computer nation's King lets its colonies
    // go once enough of its colonists are rebels: 10 x (8 - difficulty) of
    // them, its rebels in % of its people, at most 100. The spies tell of
    // it when they come within 20 of that and grow, or fall by more than 5.
    const int needed = 10 * (8 - save_.difficulty);
    for (int n = 0; n < kEuropeanNations; ++n) {
        auto& player = save_.players[static_cast<std::size_t>(n)];
        auto& nation = save_.nations[static_cast<std::size_t>(n)];
        if (player.control == 0 || (nation.raw[kNationFlags] & kUSA) != 0) {
            continue;
        }
        const int rebels = std::min(100, nation.raw[kRebelShare] * nationPeople(n) / 100);
        if (rebels >= needed) {
            // "The King of France grants independence to New France!"
            GameNotice notice{"OTHERGRANTED", {"", player.country, player.name}};
            notice.countries = {{0, n}};
            if (static_cast<std::size_t>(n) < rules_.independent.size()) {
                player.country = rules_.independent[static_cast<std::size_t>(n)];
            }
            notice.strings.push_back(player.country);
            notices_.push_back(std::move(notice));
            nation.raw[kNationFlags] |= kUSA;
            // It makes peace with every European nation.
            for (int o = 0; o < kEuropeanNations; ++o) {
                if (o == n) {
                    continue;
                }
                for (const auto& [a, b] : {std::pair{n, o}, std::pair{o, n}}) {
                    auto& r = save_.nations[static_cast<std::size_t>(a)].raw[kRelations + static_cast<std::size_t>(b)];
                    r = static_cast<std::uint8_t>((r | kPeace) & ~0xBB);
                }
            }
            continue;
        }
        const int told = nation.raw[kRebelsTold];
        const auto spies = [&](const char* section) {
            GameNotice notice{section, {}, -1, {rebels, nationPeople(n), needed}};
            notice.countries = {{0, n}};
            notice.adjectives = {{1, n}};
            notices_.push_back(std::move(notice));
            nation.raw[kRebelsTold] = static_cast<std::uint8_t>(rebels);
        };
        if (rebels >= needed - 20 && told < rebels) {
            // "the King of France is considering granting independence"
            spies("OTHERMIGHT");
        }
        if (nation.raw[kRebelsTold] - 5 > rebels) {
            // "Spies Report: Only 20 French now support independence"
            spies("OTHERLESS");
        }
    }
}

void Game::coloniesChangeHands(int colony) {
    // Read from the original: during the war the Crown taking a colony
    // makes its defeat harder, and the rebels taking one back hear, the
    // first time, how the war is won.
    if ((save_.header[kHeaderWar] & kDeclared) == 0) {
        return;
    }
    const int owner = save_.colonies[static_cast<std::size_t>(colony)].nation;
    if (owner == crownNation()) {
        save_.header[kHeaderWar] |= kCrownTookColony;
    }
    if (owner == human() && (save_.header[kHeaderHints] & kToldHowToWin) == 0) {
        save_.header[kHeaderHints] |= kToldHowToWin;
        notices_.push_back(GameNotice{"HOWTOWIN", {}});
    }
}

} // namespace viceroy
