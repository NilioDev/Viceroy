// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Continental Congress: liberty bells gather for each nation, the
// founding fathers are put up for election and join. Read from the
// original; see docs/rules/congress.md.
#include "viceroy/game/europe.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"

#include <algorithm>
#include <cstdint>

namespace viceroy {

namespace {

// Nation record.
constexpr std::size_t kFatherBits = 7;  // one bit per father, 32 bits
constexpr std::size_t kBells = 0x0C;    // bells toward the next father (16 bits)
constexpr std::size_t kBellsToo = 0x0E; // a second count the bells go into (16 bits)
constexpr std::size_t kCurrent = 0x12;  // the father up for election, -1 none (16 bits)
constexpr std::size_t kCount = 0x14;    // fathers in congress
constexpr std::size_t kBoycotts = 0x20; // goods boycotted, one bit each (16 bits)
constexpr std::size_t kSeaLane = 50;    // where the nation's ships come out (x, y)

// Header.
constexpr std::size_t kIndependence = 18;   // bit 0: independence declared
constexpr std::size_t kFirstNation = 57;    // per father: the first nation that had it, -1 none
constexpr std::size_t kHumanRebels = 96;    // the human's rebels in all colonies, in % (16 bits)

// Unit record.
constexpr std::size_t kUnitDestX = 9;
constexpr std::size_t kUnitDestY = 10;
constexpr std::size_t kUnitByte22 = 22;

// Settlement record.
constexpr std::size_t kMission = 5;

// Colony record: per nation, whether the nation knows the colony and a
// byte cleared with it (read from the original, not yet understood).
constexpr std::size_t kColonyKnown = 0xBA;
constexpr std::size_t kColonyKnownToo = 0xBE;

// Rows of @FATHERS whose joining does something at once.
constexpr int kFugger = 1;
constexpr int kCoronado = 6;
constexpr int kLaSalle = 9;
constexpr int kJohnPaulJones = 14;
constexpr int kPocahontas = 16;
constexpr int kBolivar = 18;
constexpr int kBrewster = 20;
constexpr int kBrebeuf = 22;
constexpr int kLasCasas = 24;

constexpr int kFrigate = 17;      // row of @UNIT
constexpr int kServant = 0x19;    // professions
constexpr int kCriminal = 0x1A;
constexpr int kConvert = 0x1B;
constexpr int kColonistType = 0;

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

int Game::fatherEra() const {
    // The weights of @FATHERS: before 1600, before 1700, and after.
    return (save_.year >= 1600 ? 1 : 0) + (save_.year >= 1700 ? 1 : 0);
}

int Game::congressBells(int nation) const {
    if (nation < 0 || nation >= kEuropeanNations) {
        return 0;
    }
    return static_cast<std::uint16_t>(word(save_.nations[static_cast<std::size_t>(nation)].raw, kBells));
}

int Game::fatherInElection(int nation) const {
    if (nation < 0 || nation >= kEuropeanNations) {
        return -1;
    }
    return word(save_.nations[static_cast<std::size_t>(nation)].raw, kCurrent);
}

int Game::fatherCount(int nation) const {
    if (nation < 0 || nation >= kEuropeanNations) {
        return 0;
    }
    return save_.nations[static_cast<std::size_t>(nation)].raw[kCount];
}

bool Game::independenceDeclared() const {
    return (save_.header[kIndependence] & 0x01) != 0;
}

int Game::bellsForNextFather(int nation) const {
    // Read from the original. After the declaration of independence the
    // bells serve another end (not done yet).
    const bool human = nation >= 0 && nation < kEuropeanNations &&
                       save_.players[static_cast<std::size_t>(nation)].control == 0;
    int base = human ? (save_.difficulty + 3) * 2 : 14 - save_.difficulty;
    base *= 8;
    for (const int year : {1600, 1650, 1700, 1750}) {
        if (save_.year >= year) {
            base += base >> 1;
        }
    }
    const int count = fatherCount(nation);
    int need = (count + 1) * base + 1;
    if (count == 0) {
        need >>= 1;
    }
    return need;
}

int Game::fathersAvailable(int nation, int category) const {
    // The fathers of the category not yet in congress whose weight in this
    // era is not 0.
    const int era = fatherEra();
    int n = 0;
    for (int f = 0; f < static_cast<int>(rules_.fathers.size()) && f < kFoundingFathers; ++f) {
        const auto& father = rules_.fathers[static_cast<std::size_t>(f)];
        if (!hasFather(nation, f) && father.category == category &&
            father.weight[static_cast<std::size_t>(era)] != 0) {
            ++n;
        }
    }
    return n;
}

int Game::aiFatherCategory(int nation) const {
    // The computer's nations take the category with the most fathers
    // still to come (the last of equals).
    if (fatherCount(nation) >= kFoundingFathers) {
        return -1;
    }
    int best = -1;
    int most = -1;
    for (int c = 0; c < kFatherCategories; ++c) {
        const int n = fathersAvailable(nation, c);
        if (n >= most) {
            most = n;
            best = c;
        }
    }
    return best;
}

void Game::electFathers(int nation) {
    // One candidate per category, drawn by the weights of the era.
    const int era = fatherEra();
    bool any = false;
    for (int c = 0; c < kFatherCategories; ++c) {
        auto& candidate = fatherCandidates_[static_cast<std::size_t>(c)];
        candidate = -1;
        if (fathersAvailable(nation, c) == 0) {
            continue;
        }
        any = true;
        int sum = 0;
        for (int f = 0; f < kFoundingFathers && f < static_cast<int>(rules_.fathers.size()); ++f) {
            const auto& father = rules_.fathers[static_cast<std::size_t>(f)];
            if (!hasFather(nation, f) && father.category == c) {
                sum += father.weight[static_cast<std::size_t>(era)];
            }
        }
        int roll = aiRandom(1, sum);
        for (int f = 0; candidate < 0 && f < kFoundingFathers && f < static_cast<int>(rules_.fathers.size()); ++f) {
            const auto& father = rules_.fathers[static_cast<std::size_t>(f)];
            if (!hasFather(nation, f) && father.category == c) {
                roll -= father.weight[static_cast<std::size_t>(era)];
                if (roll <= 0) {
                    candidate = f;
                }
            }
        }
    }
    const int ai = aiFatherCategory(nation);
    auto& raw = save_.nations[static_cast<std::size_t>(nation)].raw;
    if (save_.players[static_cast<std::size_t>(nation)].control == 0) {
        // The player picks one (@WHICHFREEDOM).
        if (any) {
            electionNation_ = nation;
            notices_.push_back(GameNotice{"WHICHFREEDOM", {}, nation});
        }
        return;
    }
    setWord(raw, kCurrent, ai >= 0 ? fatherCandidates_[static_cast<std::size_t>(ai)] : -1);
}

void Game::chooseFather(int category) {
    const int nation = std::exchange(electionNation_, -1);
    if (nation < 0 || category < 0 || category >= kFatherCategories) {
        return;
    }
    const int father = fatherCandidates_[static_cast<std::size_t>(category)];
    setWord(save_.nations[static_cast<std::size_t>(nation)].raw, kCurrent, father);
    // In the original the choice comes while the bells are counted, and
    // enough of them bring the father in at once.
    if (father >= 0 && bellsForNextFather(nation) <= congressBells(nation)) {
        joinCongress(nation, father);
        setWord(save_.nations[static_cast<std::size_t>(nation)].raw, kBells, 0);
    }
}

void Game::addCongressBells(int nation, int bells) {
    if (nation < 0 || nation >= kEuropeanNations) {
        return;
    }
    auto& raw = save_.nations[static_cast<std::size_t>(nation)].raw;
    setWord(raw, kBells, word(raw, kBells) + bells);
    setWord(raw, kBellsToo, word(raw, kBellsToo) + bells);
    if (independenceDeclared()) {
        return; // the bells after the declaration: not done yet
    }
    if (electionNation_ == nation) {
        return; // the player has not chosen yet
    }
    if (word(raw, kCurrent) < 0) {
        electFathers(nation);
        if (electionNation_ == nation) {
            return;
        }
    }
    if (bellsForNextFather(nation) <= static_cast<std::uint16_t>(word(raw, kBells))) {
        const int father = word(raw, kCurrent);
        if (father >= 0) {
            joinCongress(nation, father);
        }
        setWord(raw, kBells, 0);
    }
}

void Game::joinCongress(int nation, int father) {
    auto& raw = save_.nations[static_cast<std::size_t>(nation)].raw;
    raw[kFatherBits + static_cast<std::size_t>(father / 8)] |= static_cast<std::uint8_t>(1U << (father % 8));
    auto& first = save_.header[kFirstNation + static_cast<std::size_t>(father)];
    if (static_cast<std::int8_t>(first) < 0) {
        first = static_cast<std::uint8_t>(nation);
    }
    const bool human = save_.players[static_cast<std::size_t>(nation)].control == 0;
    if (human) {
        // "Dutch Founding Fathers announce that Peter Minuit has joined
        // the Continental Congress!", then the father in the hall.
        const std::string name = static_cast<std::size_t>(father) < rules_.fathers.size()
                                     ? rules_.fathers[static_cast<std::size_t>(father)].name
                                     : "";
        GameNotice notice{"FREEDOM", {name, ""}, father};
        notice.adjectives = {{1, nation}};
        notices_.push_back(std::move(notice));
        notices_.push_back(GameNotice{"CONGRESSHALL", {}, father});
    }
    raw[kCount] = static_cast<std::uint8_t>(raw[kCount] + 1);
    setWord(raw, kCurrent, -1);

    switch (father) {
    case kFugger:
        // The boycotts end.
        setWord(raw, kBoycotts, 0);
        break;
    case kLaSalle:
        // Every colony of three or more gets a stockade.
        for (auto& c : save_.colonies) {
            if (c.nation == nation && c.population >= 3) {
                c.buildings |= 1;
            }
        }
        break;
    case kJohnPaulJones: {
        // A frigate on its way to the New World.
        const int i = addUnitInEurope(kFrigate, 0, nation);
        auto& u = save_.units[static_cast<std::size_t>(i)];
        setEuropeStage(u, 1);
        u.orders = 0;
        u.raw[kUnitDestX] = raw[kSeaLane];
        u.raw[kUnitDestY] = raw[kSeaLane + 1];
        u.raw[kUnitByte22] = 0;
        break;
    }
    case kPocahontas:
        // Every tribe forgets its anger, every settlement its alarm.
        for (int t = 0; t < kTribes; ++t) {
            const int tension = tribeTension(save_, t, nation);
            if (tension > 0) {
                changeTension(t, nation, -tension);
            }
        }
        for (auto& s : save_.settlements) {
            setWord(s.raw, 10 + 2 * static_cast<std::size_t>(nation), 0);
        }
        break;
    case kBolivar:
        // Only for the player: its share of rebels in all colonies.
        if (human) {
            setWord(save_.header, kHumanRebels, std::min(100, word(save_.header, kHumanRebels) + 20));
        }
        break;
    case kBrewster:
        // No criminals or servants wait on the docks any more.
        for (auto& r : save_.nations[static_cast<std::size_t>(nation)].recruits) {
            if (r == kServant || r == kCriminal) {
                r = kFreeColonist;
            }
        }
        break;
    case kBrebeuf:
        // The nation's missions are run by experts.
        for (auto& s : save_.settlements) {
            const auto m = s.raw[kMission];
            if (static_cast<std::int8_t>(m) >= 0 && (m & 0x0F) == nation) {
                s.raw[kMission] = static_cast<std::uint8_t>(m | kExpertMission);
            }
        }
        break;
    case kLasCasas:
        // Native converts become free colonists.
        for (auto& u : save_.units) {
            if (u.nation == nation && u.type == kColonistType && u.profession == kConvert) {
                u.profession = kFreeColonist;
            }
        }
        for (auto& c : save_.colonies) {
            if (c.nation != nation) {
                continue;
            }
            for (int k = 0; k < c.population && k < kMaxColonists; ++k) {
                if (c.skills[static_cast<std::size_t>(k)] == kConvert) {
                    c.skills[static_cast<std::size_t>(k)] = kFreeColonist;
                }
            }
        }
        break;
    case kCoronado:
        // The land around every colony, and the colonies there.
        for (const auto& c : save_.colonies) {
            for (int y = c.y - 5; y <= c.y + 5; ++y) {
                for (int x = c.x - 5; x <= c.x + 5; ++x) {
                    if (x < 1 || y < 1 || x >= save_.map.width - 1 || y >= save_.map.height - 1) {
                        continue;
                    }
                    auto& sq = save_.map.at(x, y);
                    sq.seen = static_cast<std::uint8_t>(sq.seen | (0x10U << nation));
                    const auto n = static_cast<std::size_t>(nation);
                    for (auto& other : save_.colonies) {
                        if (other.x == x && other.y == y && other.raw[kColonyKnown + n] == 0) {
                            other.raw[kColonyKnown + n] = 1;
                            other.raw[kColonyKnownToo + n] = 0;
                        }
                    }
                }
            }
        }
        break;
    default:
        break;
    }
}

} // namespace viceroy
