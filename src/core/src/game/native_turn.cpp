// SPDX-License-Identifier: GPL-3.0-or-later
//
// The natives' part of a turn: each tribe in turn looks after its
// settlements (growth, a new brave for one that lost its own, how missions
// and nearby colonists move the tribe's mood), uses up its goods, breeds
// horses and counts its forces. Read from the original with a debugger and
// checked against its answers; see docs/rules/natives.md.
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"

#include <algorithm>

namespace viceroy {

namespace {

constexpr int kGoods = 16;
constexpr int kRegions = 16;
constexpr int kBrave = 19; // armed +1, mounted +2
constexpr int kGrowthStep = 20;
constexpr int kHorsesForMount = 50;
constexpr int kMoodStep = 8;       // a tribe's mood counter moves its tension by one per 8
constexpr int kMoreMissions = 24;  // row of @FATHERS: missions calm twice as fast
constexpr int kFewerMissions = 23; // row of @FATHERS: missions calm half as fast

// In each tribe's record.
constexpr std::size_t kTribeFlags = 3;
constexpr std::uint8_t kTribeGone = 0x80;
constexpr std::uint8_t kToriesArmed = 0x20;   // it took the Crown's side in the war
constexpr std::uint8_t kBurnedByHuman = 0x40; // the player burned one of its villages
constexpr std::size_t kMuskets = 7;
constexpr std::size_t kBreeding = 8;
constexpr std::size_t kHorses = 10;    // 16 bits
constexpr std::size_t kGoodsHeld = 14; // 16 bits each
constexpr std::size_t kWarning = 46;   // 16 bits per nation, cleared every turn
constexpr std::size_t kMood = 54;      // a signed byte per nation

// In the block after the tribe records.
constexpr std::size_t kExtra = static_cast<std::size_t>(kTribes) * kTribeRecordSize;
constexpr std::size_t kTribeDefence = kExtra + 565;
constexpr std::size_t kTribePeople = kExtra + 573;
constexpr std::size_t kSettlementCounts = kExtra + 581;
constexpr std::size_t kTribeRegionDefence = kExtra + 589;
// The settlements of the tribe counted last, by region (16 bytes).
constexpr std::size_t kRegionSettlements = kExtra + 140;

// In each settlement's record.
constexpr std::uint8_t kNeedsBrave = 0x01; // byte 3: its brave is gone
constexpr std::size_t kGrowth = 6;

std::uint8_t* tribeBytes(SaveGame& save, int tribe) {
    const auto at = static_cast<std::size_t>(tribe) * kTribeRecordSize;
    return at + kTribeRecordSize <= save.tribesAndMore.size() ? save.tribesAndMore.data() + at : nullptr;
}

int word(const std::uint8_t* p) {
    return static_cast<std::int16_t>(p[0] | (p[1] << 8));
}

void setWord(std::uint8_t* p, int value) {
    p[0] = static_cast<std::uint8_t>(value & 0xFF);
    p[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

void addCapped(std::uint8_t& byte, int value) {
    byte = static_cast<std::uint8_t>(std::min(255, byte + value));
}

} // namespace

int Game::addNativeUnit(int type, int tribe, int x, int y) {
    if (unitCount() >= kMaxUnits) {
        return -1;
    }
    SaveUnit u;
    // As the original makes one: no orders, home and links empty, the turn
    // it was made in bytes 18 and 19, 0x1C in byte 23.
    static constexpr std::array<std::uint8_t, 28> kTemplate{
        0, 0, 0, 0, 0, 0, 0xFF, 0x58, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x1C, 0xFF, 0xFF, 0xFF, 0xFF};
    u.raw = kTemplate;
    u.x = x;
    u.y = y;
    u.type = type;
    u.nation = kEuropeanNations + tribe;
    u.profession = 0x1C;
    u.raw[18] = static_cast<std::uint8_t>(save_.turn & 0xFF);
    u.raw[19] = static_cast<std::uint8_t>((save_.turn >> 8) & 0xFF);
    save_.units.push_back(u);
    refreshUnitFlags();
    return unitCount() - 1;
}

void Game::nativesTurn() {
    for (int t = 0; t < kTribes; ++t) {
        if (auto* tribe = tribeBytes(save_, t)) {
            std::fill_n(tribe + kWarning, 2 * kEuropeanNations, std::uint8_t{0});
        }
    }
    for (int t = 0; t < kTribes; ++t) {
        const auto* tribe = tribeBytes(save_, t);
        if (tribe != nullptr && (tribe[kTribeFlags] & kTribeGone) == 0) {
            tribeTurn(t);
        }
    }
}

void Game::torySide(int tribe) {
    // Read from the original: during the war of independence a tribe the
    // player has alarmed (25 or more, by a roll of 1 to 400 at or below its
    // alarm) or whose village the player burned may take the Crown's side,
    // one turn in (5 - difficulty) x 2 + 1, once. It turns against the
    // player, makes peace with the Crown, throws out the player's missions
    // and gets muskets and horses from the Tories.
    auto* t = tribeBytes(save_, tribe);
    const int alarm = tribeTension(save_, tribe, human());
    const bool grudge = (alarm >= 25 && 1 + nextRandom(400) <= alarm) || (t[kTribeFlags] & kBurnedByHuman) != 0;
    if (!grudge || nextRandom((5 - save_.difficulty) * 2 + 1) != 0) {
        return;
    }
    const auto& tribes = rules_.tribes;
    const auto ti = static_cast<std::size_t>(tribe);
    // "Sioux nation holds War Council! Sioux enter the War of Independence
    // on the Tory side!"
    GameNotice notice{"INDIANGRUDGE",
                      {ti < tribes.size() ? tribes[ti].adjective : "", ti < tribes.size() ? tribes[ti].name : ""}};
    notice.value = tribe;
    notices_.push_back(std::move(notice));
    changeTension(tribe, human(), 100);
    changeTension(tribe, crownNation(), -100);
    burnMissions(tribe, human());
    const int villages = kSettlementCounts + ti < save_.tribesAndMore.size()
                             ? static_cast<std::int8_t>(save_.tribesAndMore[kSettlementCounts + ti])
                             : 0;
    // Four muskets a village (the byte wraps as the original's does), and
    // horses for 25 times the breeding.
    const int muskets = std::max<int>(villages, static_cast<std::int8_t>(t[kMuskets]));
    t[kMuskets] = static_cast<std::uint8_t>(muskets << 2);
    const int breeding = std::max<int>(villages, static_cast<std::int8_t>(t[kBreeding]));
    t[kBreeding] = static_cast<std::uint8_t>(breeding);
    setWord(t + kHorses, breeding * 25);
    t[kTribeFlags] |= kToriesArmed;
}

void Game::tribeTurn(int tribe) {
    auto* t = tribeBytes(save_, tribe);
    if (t == nullptr) {
        return;
    }
    if ((save_.header[18] & 0x01) != 0 && (t[kTribeFlags] & kToriesArmed) == 0) {
        torySide(tribe);
    }
    if (static_cast<std::int8_t>(t[kMuskets]) < 0) {
        t[kMuskets] = 0;
    }
    for (int s = 0; s < static_cast<int>(save_.settlements.size()); ++s) {
        if (tribeOf(save_.settlements[static_cast<std::size_t>(s)]) == tribe) {
            settlementTurn(s);
        }
    }
    // The tribe's goods wear away toward nothing by its level plus one.
    const int use = tribeLevel(save_, tribe) + 1;
    for (int g = 0; g < kGoods; ++g) {
        auto* held = t + kGoodsHeld + 2 * static_cast<std::size_t>(g);
        const int have = word(held);
        setWord(held, have > 0 ? std::max(0, have - use) : std::min(0, have + use));
    }
    countTribe(tribe);
    // Horses breed by byte 8 a turn, up to twice the tribe's people plus
    // 25. As in the original, whose count of the settlements leaves it
    // looking at the tribe of the last settlement in the list, it is that
    // tribe's horses that breed, by its own byte 8, every time any tribe
    // has its turn (measured: 3 a turn in byte 8 gave 15 more horses with
    // five tribes left, and the tribe with 5 none).
    auto* breeder = save_.settlements.empty() ? t : tribeBytes(save_, tribeOf(save_.settlements.back()));
    const int breeding = breeder != nullptr ? static_cast<std::int8_t>(breeder[kBreeding]) : 0;
    if (breeding != 0) {
        const int people = save_.tribesAndMore[kTribePeople + static_cast<std::size_t>(tribe)];
        setWord(breeder + kHorses, std::min(word(breeder + kHorses) + breeding, (people + 25) * 2));
    }
    moveTribeUnits(tribe);
}

void Game::countTribe(int tribe) {
    auto& block = save_.tribesAndMore;
    if (block.size() < kTribeRegionDefence + static_cast<std::size_t>(kTribes) * kRegions) {
        return;
    }
    const auto t = static_cast<std::size_t>(tribe);
    block[kTribeDefence + t] = 0;
    block[kTribePeople + t] = 0;
    block[kSettlementCounts + t] = 0;
    std::fill_n(block.begin() + static_cast<std::ptrdiff_t>(kTribeRegionDefence + kRegions * t), kRegions,
                std::uint8_t{0});
    std::fill_n(block.begin() + static_cast<std::ptrdiff_t>(kRegionSettlements), kRegions, std::uint8_t{0});
    for (const auto& s : save_.settlements) {
        if (tribeOf(s) == tribe) {
            const int region = landRegion(save_.map, s.x, s.y);
            if (region >= 0) {
                ++block[kRegionSettlements + static_cast<std::size_t>(region)];
            }
            ++block[kSettlementCounts + t];
            block[kTribePeople + t] = static_cast<std::uint8_t>(block[kTribePeople + t] + s.population);
        }
    }
    for (const auto& u : save_.units) {
        if (u.nation != kEuropeanNations + tribe) {
            continue;
        }
        const int force = unitForce(u, true);
        addCapped(block[kTribeDefence + t], force);
        const int region = landRegion(save_.map, u.x, u.y);
        if (region >= 0) {
            addCapped(block[kTribeRegionDefence + kRegions * t + static_cast<std::size_t>(region)], force);
        }
    }
}

void Game::settlementTurn(int settlement) {
    auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    const int tribe = tribeOf(s);
    auto* t = tribeBytes(save_, tribe);
    if (t == nullptr) {
        return;
    }
    const bool capital = (s.raw[3] & kSettlementCapital) != 0;
    // Growth: up to twice the level plus three people (three times the
    // level plus four at the capital). A settlement whose brave is gone
    // raises a new one instead. Either needs 20 in byte 6, which gains the
    // population each turn.
    const int level = tribeLevel(save_, tribe);
    const int most = capital ? 3 * level + 4 : 2 * level + 3;
    const bool brave = (s.raw[3] & kNeedsBrave) != 0;
    if (brave || most > s.population) {
        s.raw[kGrowth] = static_cast<std::uint8_t>(s.raw[kGrowth] + s.population);
        if (static_cast<std::int8_t>(s.raw[kGrowth]) >= kGrowthStep) {
            s.raw[kGrowth] = 0;
            if (!brave) {
                ++s.population;
            } else {
                int type = kBrave;
                if (static_cast<std::int8_t>(t[kMuskets]) > 0) {
                    if (nextRandom(save_.difficulty + 1) == 0) {
                        --t[kMuskets];
                    }
                    ++type;
                }
                if (word(t + kHorses) >= kHorsesForMount) {
                    setWord(t + kHorses, word(t + kHorses) - kHorsesForMount);
                    type += 2;
                }
                const int unit = addNativeUnit(type, tribe, s.x, s.y);
                if (unit >= 0) {
                    save_.units[static_cast<std::size_t>(unit)].raw[6] = static_cast<std::uint8_t>(settlement);
                    s.raw[3] = static_cast<std::uint8_t>(s.raw[3] & ~kNeedsBrave);
                }
            }
        }
    }
    // The tribe calms toward every nation it has met, faster the tenser it
    // is: (level)² + 1 rolls of 1 in 13 - (level)², the level being the
    // tension's step (0 to 3).
    auto mood = [&](int nation) -> std::int8_t& {
        return reinterpret_cast<std::int8_t&>(t[kMood + static_cast<std::size_t>(nation)]);
    };
    for (int n = 0; n < kEuropeanNations; ++n) {
        if ((t[58 + static_cast<std::size_t>(n)] & 0x20) == 0) {
            continue;
        }
        const int step = tensionLevel(tribeTension(save_, tribe, n));
        const int squared = step * step;
        int calm = 0;
        for (int i = 0; i <= squared; ++i) {
            if (nextRandom(13 - squared) == 0) {
                ++calm;
            }
        }
        mood(n) = static_cast<std::int8_t>(mood(n) + calm);
    }
    // A mission calms the tribe and the settlement; the nation pressing on
    // the settlement hardest angers both.
    int pressure = 0;
    const int presser = settlementPressure(settlement, &pressure);
    const int mission = missionOf(s);
    auto alarm = [&](int nation) {
        const auto at = static_cast<std::size_t>(10 + 2 * nation);
        return static_cast<std::int16_t>(s.raw[at] | (s.raw[at + 1] << 8));
    };
    auto setAlarm = [&](int nation, int value) {
        const auto at = static_cast<std::size_t>(10 + 2 * nation);
        s.raw[at] = static_cast<std::uint8_t>(value & 0xFF);
        s.raw[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    };
    if (mission >= 0) {
        int calm = ((s.raw[5] & kExpertMission) != 0 ? 4 : 1) << (capital ? 1 : 0);
        if (hasFather(mission, kMoreMissions)) {
            calm <<= 1;
        }
        if (hasFather(mission, kFewerMissions)) {
            calm >>= 1;
        }
        mood(mission) = static_cast<std::int8_t>(mood(mission) + calm);
        setAlarm(mission, std::max(0, alarm(mission) - 3 * calm));
    }
    if (presser >= 0) {
        int anger = pressure << (capital ? 1 : 0);
        mood(presser) = static_cast<std::int8_t>(mood(presser) - anger);
        if (presser == mission) {
            anger >>= 1;
        }
        anger += tribeTension(save_, tribe, presser) / 5;
        setAlarm(presser, alarm(presser) + anger);
    }
    if (mission >= 0) {
        while (mood(mission) >= kMoodStep) {
            mood(mission) = static_cast<std::int8_t>(mood(mission) - kMoodStep);
            changeTension(tribe, mission, -1);
        }
    }
    if (presser >= 0) {
        while (mood(presser) <= -kMoodStep) {
            mood(presser) = static_cast<std::int8_t>(mood(presser) + kMoodStep);
            changeTension(tribe, presser, 1);
        }
    }
    for (int n = 0; n < kEuropeanNations; ++n) {
        while (mood(n) >= kMoodStep) {
            mood(n) = static_cast<std::int8_t>(mood(n) - kMoodStep);
            changeTension(tribe, n, -1);
        }
    }
}

} // namespace viceroy
