// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/game/natives.hpp"

#include "viceroy/game/game.hpp"

#include <algorithm>
#include <cstdlib>

namespace viceroy {

namespace {

// The block after the settlements starts with the eight tribe records.
const std::uint8_t* tribeRecord(const SaveGame& save, int tribe) {
    const auto offset = static_cast<std::size_t>(tribe) * kTribeRecordSize;
    if (tribe < 0 || tribe >= kTribes || save.tribesAndMore.size() < offset + kTribeRecordSize) {
        return nullptr;
    }
    return save.tribesAndMore.data() + offset;
}

int word(const std::uint8_t* p) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(p[0] | (p[1] << 8)));
}

int signedByte(std::uint8_t b) {
    return static_cast<std::int8_t>(b);
}

// Division that rounds towards zero, as the original's does.
int divide(int a, int b) {
    return b == 0 ? 0 : a / b;
}

// The 20 squares around a centre: the 8 neighbours, then the 12 two steps
// away without the corners. Colonies keep who works each of them.
constexpr std::array<std::array<int, 2>, 20> kRing{{{0, -1},  {1, 0},   {0, 1},   {-1, 0}, {-1, -1},
                                                    {1, -1},  {1, 1},   {-1, 1},  {0, -2}, {2, 0},
                                                    {0, 2},   {-2, 0},  {-1, -2}, {1, -2}, {-1, 2},
                                                    {1, 2},   {-2, -1}, {-2, 1},  {2, -1}, {2, 1}}};

int ringIndex(int dx, int dy) {
    for (std::size_t i = 0; i < kRing.size(); ++i) {
        if (kRing[i][0] == dx && kRing[i][1] == dy) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// The original only counts squares off the map's outer edge.
bool innerSquare(const SaveMap& map, int x, int y) {
    return x >= 1 && y >= 1 && x < map.width - 1 && y < map.height - 1;
}

// A square as the economy sees it: the terrain (0-26), 27 for mountains
// and 28 for hills; off the inner map it counts as ocean.
constexpr int kOcean = 25;
constexpr int kSeaLane = 26;
constexpr int kArctic = 24;
constexpr int kMountainKind = 27;
constexpr int kHillKind = 28;

int squareKind(const SaveMap& map, int x, int y) {
    if (!innerSquare(map, x, y)) {
        return kOcean;
    }
    const auto& sq = map.at(x, y);
    if ((sq.relief & 1) != 0) { // hills, hills with a river, mountains
        return (sq.relief & 4) != 0 ? kMountainKind : kHillKind;
    }
    return sq.terrain;
}

// Goods in @CARGO order.
constexpr int kFood = 0;
constexpr int kSugar = 1;
constexpr int kTobacco = 2;
constexpr int kCotton = 3;
constexpr int kFurs = 4;
constexpr int kOre = 6;
constexpr int kSilver = 7;
constexpr int kHorses = 8;
constexpr int kRum = 9;
constexpr int kCigars = 10;
constexpr int kCloth = 11;
constexpr int kCoats = 12;
constexpr int kTradeGoods = 13;
constexpr int kTools = 14;
constexpr int kMuskets = 15;

// Jobs taught instead of a good.
constexpr int kFishermanJob = 8;
constexpr int kScoutJob = 22;

} // namespace

int OriginalRandom::next() {
    state_ = state_ * 214013U + 2531011U;
    return static_cast<int>((state_ >> 16) & 0x7FFF);
}

int OriginalRandom::between(int low, int high) {
    return low + static_cast<int>((static_cast<long>(high - low + 1) * next()) >> 15);
}

int tribeOf(const SaveSettlement& settlement) {
    return settlement.nation - kEuropeanNations;
}

int tribeLevel(const SaveGame& save, int tribe) {
    const auto* t = tribeRecord(save, tribe);
    return t ? t[2] : 0;
}

int tribeTension(const SaveGame& save, int tribe, int nation) {
    const auto* t = tribeRecord(save, tribe);
    if (!t || nation < 0 || nation >= kEuropeanNations) {
        return 0;
    }
    return word(t + 70 + 2 * nation);
}

void setTribeTension(SaveGame& save, int tribe, int nation, int tension) {
    const auto offset = static_cast<std::size_t>(tribe) * kTribeRecordSize + 70 + 2 * static_cast<std::size_t>(nation);
    if (tribe < 0 || tribe >= kTribes || nation < 0 || nation >= kEuropeanNations || save.tribesAndMore.size() < offset + 2) {
        return;
    }
    const auto value = static_cast<std::uint16_t>(tension);
    save.tribesAndMore[offset] = static_cast<std::uint8_t>(value & 0xFF);
    save.tribesAndMore[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

int missionOf(const SaveSettlement& settlement) {
    const auto byte = static_cast<std::int8_t>(settlement.raw[5]);
    return byte < 0 ? -1 : byte & 0x0F;
}

int nearestColony(const SaveGame& save, int x, int y, int nation) {
    int best = -1;
    int bestDistance = 9999;
    for (std::size_t i = 0; i < save.colonies.size(); ++i) {
        const auto& c = save.colonies[i];
        if (c.nation != nation) {
            continue;
        }
        const int dx = std::abs(c.x - x);
        const int dy = std::abs(c.y - y);
        const int distance = std::max(dx, dy) + std::min(dx, dy) / 2;
        if (distance <= bestDistance) {
            best = static_cast<int>(i);
            bestDistance = distance;
        }
    }
    return best;
}

int tensionLevel(int tension) {
    return tension < 25 ? 0 : tension < 50 ? 1 : tension < 75 ? 2 : 3;
}

bool metTribe(const SaveGame& save, int nation, int tribe) {
    if (nation < 0 || nation >= kEuropeanNations || tribe < 0 || tribe >= kTribes) {
        return false;
    }
    return (save.nations[static_cast<std::size_t>(nation)].raw[static_cast<std::size_t>(52 + kEuropeanNations + tribe)] &
            0x40) != 0;
}

std::string villageGreeting(const SaveGame& save, int settlement, int nation) {
    // Read from the original: the tribe's tension decides, then the
    // settlement's own alarm; the Arawaks keep scalps even when happy.
    const auto& s = save.settlements[static_cast<std::size_t>(settlement)];
    const int tension = tribeTension(save, tribeOf(s), nation);
    if (tension >= 75) {
        return "VILLAGEWAR";
    }
    if (tension >= 50) {
        return "VILLAGEBAD";
    }
    const int alarm = nation >= 0 && nation < kEuropeanNations
                          ? word(s.raw.data() + 10 + 2 * nation)
                          : 0;
    if (tension >= 25 || alarm >= 0x80) {
        return "VILLAGEMEDIUM";
    }
    constexpr int kArawaks = 2;
    return tribeOf(s) == kArawaks ? "VILLAGESAVAGE" : "VILLAGEHAPPY";
}

VillageEconomy villageEconomy(const SaveGame& save, int settlement) {
    // Read from the original with a debugger and checked against the skills
    // of eleven villages; see docs/rules/natives.md for the steps.
    const auto& s = save.settlements[static_cast<std::size_t>(settlement)];
    const auto& map = save.map;
    const int tribe = tribeOf(s);
    const auto* t = tribeRecord(save, tribe);
    const int level = t ? t[2] : 0;

    // Squares worked by a colony are left out. The original tests them
    // with the colony and the village swapped; that is kept.
    std::array<bool, 25> taken{};
    for (const auto& colony : save.colonies) {
        for (int dy = 0; dy < 5; ++dy) {
            for (int dx = 0; dx < 5; ++dx) {
                const int a = s.x - colony.x + dx;
                const int b = s.y - colony.y + dy;
                if (!innerSquare(map, a - 2, b - 2) || a < 0 || a >= 5 || b < 0 || b >= 5) {
                    continue;
                }
                if (dx != 2 || dy != 2) {
                    const int k = ringIndex(dx - 2, dy - 2);
                    if (k < 0 || signedByte(colony.raw[static_cast<std::size_t>(0x70 + k)]) < 0) {
                        continue;
                    }
                }
                taken[static_cast<std::size_t>(dy * 5 + dx)] = true;
            }
        }
    }

    int mountains = 0, hills = 0, arctic = 0, land = 0, water = 0, sugar = 0, tobacco = 0, cotton = 0,
        open = 0, food = 0, lightForest = 0, heavyForest = 0;
    for (int y = s.y - 2; y <= s.y + 2; ++y) {
        for (int x = s.x - 2; x <= s.x + 2; ++x) {
            if (!innerSquare(map, x, y) || taken[static_cast<std::size_t>((y - s.y + 2) * 5 + (x - s.x + 2))]) {
                continue;
            }
            const int k = squareKind(map, x, y);
            if (k == kMountainKind) {
                ++mountains;
            }
            if (k == kHillKind) {
                ++hills;
            }
            if (k == kArctic) {
                arctic += 4;
            }
            if (k >= 8 && k < 24) { // forest
                ++land;
                const int base = k < 16 ? k - 8 : k - 16;
                if (base < 3) {
                    ++lightForest;
                    arctic += 2;
                    continue;
                }
                ++heavyForest;
                ++food;
                if (base == 5) {
                    sugar += 2;
                }
                if (base == 4) {
                    tobacco += 2;
                }
                if (base == 3) {
                    cotton += 2;
                }
                continue;
            }
            if (k == kOcean || k == kSeaLane) {
                water += level + 1;
                while (water >= 3) {
                    land += 2;
                    water -= 3;
                }
                continue;
            }
            if (k >= 8) {
                continue;
            }
            if (k == 5) {
                sugar += 4;
            }
            if (k == 7) {
                sugar += 2;
            }
            if (k == 4) {
                tobacco += 4;
            }
            if (k == 6) {
                tobacco += 2;
            }
            if (k == 3) {
                cotton += 4;
            }
            if (k == 0) {
                open += 2;
            }
            if (k == 2) {
                ++cotton;
                land += 2;
            }
            if (k > 1) {
                land += 2;
                if (k >= 6) {
                    ++open;
                    continue;
                }
                ++land;
                if ((k & 4) != 0) {
                    food += 2;
                } else {
                    arctic += 2;
                }
                continue;
            }
            if (k == 1) {
                food += 4;
            } else {
                arctic += 3;
            }
        }
    }
    // The names above follow what each counter feeds below; "arctic" also
    // counts light forest and plain land, "food" heavy forest and more.

    VillageEconomy e;
    auto& A = e.wants;
    auto& B = e.makes;
    const int n = s.population + 1;
    int settlementsOfTribe = 0;
    for (const auto& other : save.settlements) {
        settlementsOfTribe += tribeOf(other) == tribe ? 1 : 0;
    }
    B[kFood] += divide((level + n) * land, 7 - level);
    A[kFood] = (n * n * 4) >> (level > 1 ? 1 : 0);
    if (level >= 1) {
        if (level >= 2) {
            B[kSilver] = divide(t ? word(t + 0x0C) : 0, std::max(settlementsOfTribe, 1));
            int extra = 4 * mountains;
            if (level > 2) {
                extra *= 2;
            }
            B[kSilver] += extra;
        }
        B[kOre] += 2 * hills + mountains + open;
    }
    B[kFurs] += divide(2 * lightForest + (heavyForest >> 1), level + 1);
    B[kCoats] = 2 * divide(2 * (B[kFurs] + level), 4);
    B[kTobacco] += tobacco;
    B[kSugar] += sugar;
    A[kCloth] = (level + n) * n + (food >> 1) + arctic;
    B[kCotton] += cotton;
    B[kCloth] = 2 * divide(2 * (level + B[kCotton]), 4);
    A[kCotton] = B[kCloth] + food;
    A[kTobacco] = (6 - level) * n + 2 * arctic + 5;
    A[kCigars] = 2 * (2 * n - level + 7);
    A[kCoats] = 8 * arctic + B[kFurs];
    A[kRum] = 2 * (2 * (2 * level + n) + food);
    A[kTradeGoods] = (level + 2) * (n + 3) + 8;
    A[kTools] = static_cast<std::int16_t>((level * n) << ((arctic >> 1) + 1));
    A[kMuskets] = 4 * ((7 - (t ? signedByte(t[7]) : 0)) - level);
    B[kHorses] = divide(t ? word(t + 0x0A) : 0, (settlementsOfTribe >> 1) + 1);
    A[kHorses] = 4 * ((9 - (t ? signedByte(t[8]) : 0)) - level);
    B[kMuskets] = 0;
    for (auto& a : A) {
        a = std::clamp(a, 0, 50);
    }
    if ((s.raw[3] & kSettlementCapital) != 0) {
        for (int i = 0; i < 8; ++i) {
            A[static_cast<std::size_t>(i)] *= 2;
        }
        for (int i = 13; i < 16; ++i) {
            A[static_cast<std::size_t>(i)] += A[static_cast<std::size_t>(i)] >> 1;
        }
        for (int i = 7; i < 16; ++i) {
            B[static_cast<std::size_t>(i)] *= 2;
        }
    }
    // What the tribe has on hand shifts both, then each side eats into the
    // other.
    for (std::size_t i = 0; i < 16; ++i) {
        const int held = t ? word(t + 0x0E + 2 * i) : 0;
        const int made = B[i];
        const int wanted = A[i];
        if (held > 0) {
            A[i] = wanted + 2 * divide(-50 - held, 100);
        } else if (held < 0) {
            B[i] += 2 * divide(held + 50, 100);
        }
        const int adjustedMade = B[i];
        B[i] = std::max(B[i] - (A[i] >> 1), made > 0 ? 1 : 0);
        A[i] = std::max(A[i] - (adjustedMade >> 1), wanted > 0 ? 1 : 0);
    }
    return e;
}

int villageSkill(const SaveGame& save, int settlement) {
    const auto& s = save.settlements[static_cast<std::size_t>(settlement)];
    auto weights = villageEconomy(save, settlement).makes;
    const int level = tribeLevel(save, tribeOf(s));
    // The pick is seeded with the square and the game's own seed, so a
    // village always teaches the same skill.
    std::uint32_t gameSeed = 0;
    if (save.tail.size() >= 612) {
        for (int i = 3; i >= 0; --i) {
            gameSeed = (gameSeed << 8) | save.tail[static_cast<std::size_t>(608 + i)];
        }
    }
    OriginalRandom random((static_cast<std::uint32_t>((s.y << 8) | s.x) + gameSeed) & 0x7FFF);
    weights[kHorses] = 0;
    if (level < 1) {
        weights[kCoats] = 0;
        weights[kOre] = 0;
        weights[kFood] >>= 1;
    }
    if (level < 2) {
        weights[kCloth] = 0;
        weights[kCigars] = 0;
        weights[kSilver] = 0;
        weights[kFood] -= weights[kFood] >> 2;
    }
    if (level < 3) {
        weights[kRum] = 0;
    }
    if (level == 3) {
        weights[kSilver] += weights[kSilver] >> 1;
    }
    int total = 0;
    for (const int w : weights) {
        total += w;
    }
    int left = random.between(1, total);
    int skill = -1;
    do {
        ++skill;
        left -= weights[static_cast<std::size_t>(skill)];
    } while (left > 0 && skill < 15);
    if (skill == kFurs && (s.x + s.y) % 3 == 0) {
        skill = kScoutJob;
    }
    if (skill == kFood) {
        // Farmers by the sea teach fishing, more likely the more water
        // there is around.
        int sea = 0;
        for (const auto& [dx, dy] : kRing) {
            const int x = s.x + dx;
            const int y = s.y + dy;
            sea += save.map.inside(x, y) && save.map.at(x, y).isWater() ? 1 : 0;
        }
        if (random.between(1, 20) < sea) {
            skill = kFishermanJob;
        }
    }
    return skill;
}

// The original's own sort, read with a debugger: ascending, carrying an
// index along. It takes the first value smaller than the one before it
// and moves it in front of the first value that is not smaller; equal
// values can change order, so the names the chief gives depend on it.
void originalSort(std::array<int, 16>& values, std::array<int, 16>& order) {
    for (std::size_t i = 0; i < 16; ++i) {
        order[i] = static_cast<int>(i);
    }
    const auto less = [](int a, int b) {
        return static_cast<std::uint16_t>(a) < static_cast<std::uint16_t>(b);
    };
    std::size_t i = 0;
    while (true) {
        while (i + 1 < 16 && !less(values[i + 1], values[i])) {
            ++i;
        }
        if (i + 1 >= 16) {
            return;
        }
        const int value = values[i + 1];
        const int index = order[i + 1];
        for (std::size_t k = i + 1; k + 1 < 16; ++k) {
            values[k] = values[k + 1];
            order[k] = order[k + 1];
        }
        std::size_t at = 0;
        while (at < 15 && less(values[at], value)) {
            ++at;
        }
        for (std::size_t k = 15; k > at; --k) {
            values[k] = values[k - 1];
            order[k] = order[k - 1];
        }
        values[at] = value;
        order[at] = index;
    }
}


VillageMarket villageMarket(const SaveGame& save, int settlement) {
    // Read from the original with a debugger: the goods the village makes
    // most of are not wanted, nor food when it makes more food than trade
    // goods.
    auto e = villageEconomy(save, settlement);
    VillageMarket m;
    m.wants = e.wants;
    m.makes = e.makes;
    const int food = m.makes[kFood];
    m.makes[kFood] = 0;
    if (food > m.makes[kTradeGoods]) {
        m.wants[kFood] = 0;
    }
    originalSort(m.makes, m.makesOrder);
    for (std::size_t k = 1; k <= 3; ++k) {
        m.wants[static_cast<std::size_t>(m.makesOrder[16 - k])] = 0;
    }
    return m;
}

std::array<int, 3> topWants(std::array<int, 16> wants, const SaveSettlement& s) {
    // The goods of the settlement's bytes 8 and 9 are left out.
    for (const std::size_t byte : {std::size_t{8}, std::size_t{9}}) {
        const auto good = static_cast<std::int8_t>(s.raw[byte]);
        if (good >= 0 && good < 16) {
            wants[static_cast<std::size_t>(good)] = 0;
        }
    }
    std::array<int, 16> order{};
    originalSort(wants, order);
    return {order[15], order[14], order[13]};
}

std::array<int, 3> villageWants(const SaveGame& save, int settlement) {
    // Checked against what six chiefs asked for.
    return topWants(villageMarket(save, settlement).wants, save.settlements[static_cast<std::size_t>(settlement)]);
}

namespace {
// Professions (rows of @JOB) the original tests.
constexpr int kServant = 0x19;
constexpr int kCriminal = 0x1A;
constexpr int kConvert = 0x1B;
constexpr int kFreeColonist = 0x1C;
constexpr int kColonistType = 0;
constexpr int kPioneerType = 2;
} // namespace

bool Game::canLiveAmongNatives(int index, int settlement) const {
    // Read from the original: only after contact with the tribe, and only
    // for colonists and pioneers (units that are people, not soldiers,
    // scouts or missionaries).
    const auto& u = unit(index);
    const auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    return (u.type == kColonistType || u.type == kPioneerType) && u.nation < kEuropeanNations &&
           metTribe(save_, u.nation, tribeOf(s));
}

Game::NativeLesson Game::liveAmongNatives(int index, int settlement) {
    // Read from the original, in its order: an angry tribe, a criminal, a
    // convert, an expert, a village that has taught already (a capital
    // teaches again), a slow learner, then the offer.
    auto& u = save_.units[static_cast<std::size_t>(index)];
    const auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    const int tribe = tribeOf(s);
    NativeLesson lesson;
    lesson.skill = villageSkill(save_, settlement);
    const auto jobName = [&](int job) {
        return job >= 0 && static_cast<std::size_t>(job) < rules_.jobs.size() ? rules_.jobs[static_cast<std::size_t>(job)].name
                                                                             : std::string();
    };
    const std::string tribeName =
        tribe >= 0 && static_cast<std::size_t>(tribe) < rules_.tribes.size() ? rules_.tribes[static_cast<std::size_t>(tribe)].name
                                                                             : std::string();
    lesson.strings = {tribeName, jobName(lesson.skill)};
    // Measured: going in uses up the unit's moves.
    u.movesUsed = movementAllowance(index);
    const int level = tensionLevel(tribeTension(save_, tribe, u.nation));
    if (level > 1) {
        lesson.section = "LEARNMAD";
    } else if (u.profession == kCriminal) {
        lesson.section = "LEARNCRIMINAL";
    } else if (u.profession == kConvert) {
        lesson.section = "TEACHCONVERT";
    } else if (u.profession != kFreeColonist && u.profession != kServant) {
        lesson.section = "LEARNMASTER";
        lesson.strings[1] = jobName(u.profession);
    } else if ((s.raw[3] & kSettlementTaught) != 0 && (s.raw[3] & kSettlementCapital) == 0) {
        lesson.section = "LEARNALREADY";
    } else if (level > 0 && 1 + nextRandom(1000) < 200 * save_.difficulty + 100) {
        lesson.section = "LEARNSLOW";
    } else {
        lesson.section = "LEARNSTAY";
    }
    return lesson;
}

bool Game::canSpeakWithChief(int index, int settlement) const {
    const auto& u = unit(index);
    const auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    return u.type == unit_type::kScout && u.nation < kEuropeanNations && metTribe(save_, u.nation, tribeOf(s));
}

Game::ChiefTalk Game::speakWithChief(int index, int settlement) {
    // Read from the original with a debugger, in its order; see
    // docs/rules/natives.md.
    constexpr int kCoronado = 6; // row of @FATHERS
    constexpr int kArawaks = 2;
    constexpr int kTalesRadius = 6;
    auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    const int tribe = tribeOf(s);
    const int nation = unit(index).nation;
    const int expert = unit(index).profession == kScoutJob ? 1 : 0;
    const int tension = tribeTension(save_, tribe, nation);
    const auto roll = [this](int low, int high) { return low + nextRandom(high - low + 1); };
    ChiefTalk talk;
    save_.units[static_cast<std::size_t>(index)].movesUsed = movementAllowance(index);

    // Taboos: a furious tribe, the chance an alarmed one finds offence, and
    // the Arawaks' own temper.
    bool taboo = tension >= 75;
    const int luck = taboo ? 0 : roll(0, 100 + 40 * expert);
    if (!taboo && tension >= 25 && (tension >> 2) >= luck) {
        taboo = true;
    }
    if (!taboo && tribe == kArawaks && roll(0, (8 - save_.difficulty) << expert) == 0) {
        taboo = true;
    }
    if (taboo) {
        talk.answer = hasFather(nation, kCoronado) ? ChiefAnswer::Bored : ChiefAnswer::Kill;
        if (talk.answer == ChiefAnswer::Kill) {
            removeUnit(index);
        }
        return talk;
    }

    talk.welcome = true;
    talk.skill = villageSkill(save_, settlement);
    talk.wants = villageWants(save_, settlement);
    if (luck <= tension || (s.raw[3] & kSettlementVisited) != 0) {
        talk.answer = ChiefAnswer::Bored;
        return talk;
    }
    s.raw[3] = static_cast<std::uint8_t>(s.raw[3] | kSettlementVisited);
    auto& u = save_.units[static_cast<std::size_t>(index)];
    switch (roll(1, 3)) {
    case 1:
        if (expert == 0) {
            talk.answer = ChiefAnswer::Guides;
            u.profession = kScoutJob;
            break;
        }
        [[fallthrough]];
    case 2:
        talk.answer = ChiefAnswer::Tales;
        revealLand(u.x, u.y, kTalesRadius, nation);
        break;
    default: {
        talk.answer = ChiefAnswer::Gift;
        const int most = 10 - save_.difficulty;
        const int beads = roll(1, most) + roll(1, most) + roll(1, most);
        talk.gold = beads * roll(1, 6) * 4 * (tribeLevel(save_, tribe) + 1);
        save_.nations[static_cast<std::size_t>(nation)].gold += talk.gold;
        break;
    }
    }
    return talk;
}

bool Game::canEstablishMission(int index, int settlement) const {
    const auto& u = unit(index);
    const auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    return u.type == unit_type::kMissionary && u.nation < kEuropeanNations && missionOf(s) < 0 &&
           metTribe(save_, u.nation, tribeOf(s));
}

Game::MissionNews Game::establishMission(int index, int settlement) {
    // Read from the original with a debugger; see docs/rules/natives.md.
    constexpr int kPocahontas = 16, kBrebeuf = 22, kSepulveda = 23, kLasCasas = 24; // rows of @FATHERS
    constexpr int kFrench = 1;
    constexpr int kJesuitJob = 24;
    auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    const int tribe = tribeOf(s);
    const auto& u = unit(index);
    const int nation = u.nation;
    // The tribe minds each mission the nation already has among it.
    int missions = 0;
    for (const auto& other : save_.settlements) {
        missions += tribeOf(other) == tribe && missionOf(other) == nation ? 1 : 0;
    }
    if (hasFather(nation, kSepulveda)) {
        missions <<= 1;
    }
    if (hasFather(nation, kLasCasas)) {
        missions >>= 1;
    }
    if (hasFather(nation, kPocahontas)) {
        missions >>= 1;
    }
    if (nation == kFrench) {
        missions >>= 1;
    }
    const int tension = tribeTension(save_, tribe, nation);
    const int level = tensionLevel(tension);
    constexpr std::array<int, 4> kCalm{25, 15, 10, 5};
    int change = missions * 8 - kCalm[static_cast<std::size_t>(level)];
    if ((s.raw[3] & kSettlementCapital) != 0) {
        change += 8 * (change > 0 ? 1 : change < 0 ? -1 : 0);
    }
    MissionNews news;
    news.reaction = level;
    if (change >= -5) {
        news.reaction = std::max(news.reaction, 1);
    }
    if (change > 0) {
        news.reaction = std::max(news.reaction, 2);
    }
    if (change >= 10) {
        news.reaction = 3;
    }
    news.colony = nearestColony(save_, s.x, s.y, nation);
    s.raw[5] = static_cast<std::uint8_t>(nation);
    if (u.profession == kJesuitJob || hasFather(nation, kBrebeuf)) {
        s.raw[5] = static_cast<std::uint8_t>(s.raw[5] | kExpertMission);
    }
    removeUnit(index);
    setTribeTension(save_, tribe, nation, std::max(0, tension + change));
    return news;
}

void Game::learnFromNatives(int index, int settlement) {
    auto& u = save_.units[static_cast<std::size_t>(index)];
    auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    u.profession = villageSkill(save_, settlement);
    s.raw[3] = static_cast<std::uint8_t>(s.raw[3] | kSettlementTaught);
}

} // namespace viceroy
