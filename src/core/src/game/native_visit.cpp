// SPDX-License-Identifier: GPL-3.0-or-later
//
// A brave visits a European colony, wagon train or unit it comes to stand
// next to: a tribe short of food begs for some, a friendly one brings food,
// goods or converts, an unfriendly one demands goods as reparations or
// only speaks its concern. Read from the original with a debugger; see
// docs/rules/natives.md ("Braves visit").
#include "viceroy/game/colony.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"

#include <algorithm>

namespace viceroy {

namespace {

constexpr int kSpain = 2;
constexpr int kFirstShip = 13;
constexpr int kLastShip = 18;
constexpr int kConvert = 0x1B;

// Rows of @CARGO.
constexpr int kFood = 0;
constexpr int kSilver = 7;
constexpr int kHorses = 8;
constexpr int kMuskets = 15;

// Unit bytes.
constexpr std::size_t kUnitFlags = 4;
constexpr std::uint8_t kVisitDue = 0x08;
constexpr std::size_t kHome = 6;

// Settlement bytes: the mission at 5 (its nation in the low four bits,
// 0x10 for an expert).
constexpr std::size_t kMission = 5;

// Tribe bytes: muskets at 7, horse herds at 8, horses at 10 (16 bits), and
// at 46 a word per nation: 1 after a demand (or an attack) this turn, 2
// after a meeting.
constexpr std::size_t kTribeMuskets = 7;
constexpr std::size_t kTribeHerds = 8;
constexpr std::size_t kTribeHorses = 10;
constexpr std::size_t kWarning = 46;
constexpr int kDemanded = 1;
constexpr int kMet = 2;

constexpr int kAngryAlarm = 0x80;
constexpr int kWarTension = 75;
constexpr int kWaryTension = 50;
constexpr int kCalmTension = 70; // a calming visit takes the tribe at least this low
constexpr int kBegStock = 75;    // the colony needs this much food to be begged
constexpr int kLowFood = 25;     // a gift of food comes to a colony at or below this

std::size_t tribeByte(int tribe, std::size_t at) {
    return static_cast<std::size_t>(tribe) * kTribeRecordSize + at;
}

int alarmOf(const SaveSettlement& s, int nation) {
    const auto at = 10 + 2 * static_cast<std::size_t>(nation);
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(s.raw[at] | (s.raw[at + 1] << 8)));
}

void setAlarm(SaveSettlement& s, int nation, int value) {
    const auto at = 10 + 2 * static_cast<std::size_t>(nation);
    const auto v = static_cast<std::uint16_t>(value);
    s.raw[at] = static_cast<std::uint8_t>(v & 0xFF);
    s.raw[at + 1] = static_cast<std::uint8_t>(v >> 8);
}

bool armed(int type) {
    return type == 1 || type == 4 || type == 11 || type == 20 || type == 22;
}

bool mounted(int type) {
    return type == 4 || type == 5 || type == 21 || type == 22;
}

} // namespace

bool Game::braveVisit(int brave, int direction, int nation) {
    if (brave < 0 || brave >= unitCount() || direction < 0 || direction >= static_cast<int>(kSteps.size()) ||
        nation < 0 || nation >= kEuropeanNations) {
        return false;
    }
    const auto& b = unit(brave);
    // Only a tribe's unit visits. (The original also looks at a European
    // unit next to a tribe's wagon train, which there never is.)
    const int tribe = b.nation - kEuropeanNations;
    if (tribe < 0 || tribe >= kTribes) {
        return false;
    }
    // Not on the turn it set out for the visit, nor from inside a village.
    if ((b.raw[kUnitFlags] & kVisitDue) != 0 || settlementAt(b.x, b.y)) {
        return false;
    }
    const int x = b.x + kSteps[static_cast<std::size_t>(direction)].dx;
    const int y = b.y + kSteps[static_cast<std::size_t>(direction)].dy;
    BraveVisitState v;
    v.nation = nation;
    v.tribe = tribe;
    v.brave = brave;
    const auto here = unitsAt(x, y);
    if (!here.empty() && unit(here.front()).type == unit_type::kWagonTrain) {
        v.wagon = here.front();
    }
    if (const auto c = colonyAt(x, y)) {
        v.colony = *c;
    }
    v.home = static_cast<std::int8_t>(b.raw[kHome]);
    if (v.home < 0 || v.home >= static_cast<int>(save_.settlements.size())) {
        return false;
    }
    auto& block = save_.tribesAndMore;
    if (block.size() < tribeByte(tribe, kTribeRecordSize)) {
        return false;
    }
    auto& home = save_.settlements[static_cast<std::size_t>(v.home)];
    const int alarm = alarmOf(home, nation);
    const int tension = tribeTension(save_, tribe, nation);
    const auto warning = tribeByte(tribe, kWarning + 2 * static_cast<std::size_t>(nation));
    const bool angry = alarm >= kAngryAlarm || block[warning] == kDemanded;
    if (tension >= kWarTension) {
        return false;
    }
    if (angry && 1 + nextRandom(128) < alarm - kAngryAlarm) {
        return false;
    }
    // A friendly visit: the calmer the tribe and the settlement, the likelier.
    v.calm = std::max(0, tension - 25) * 4 + alarm <= 1 + nextRandom(328);
    v.friendly = v.calm && !angry;
    if (v.friendly && tension >= kWaryTension) {
        v.friendly = false;
        block[warning] = kMet;
        block[warning + 1] = 0;
    }
    if (nation == human() && v.colony >= 0) {
        // The original picks here whether the tribe's music plays.
        nextRandom(9);
    }
    const auto economy = villageEconomy(save_, v.home);
    v.wants = economy.wants;
    v.makes = economy.makes;
    const int shortfall = v.wants[kFood] - v.makes[kFood];
    if (shortfall > 0 && v.colony >= 0) {
        const auto& c = save_.colonies[static_cast<std::size_t>(v.colony)];
        const int food = c.stock[kFood];
        if (food >= kBegStock && 1 + nextRandom(100) <= shortfall) {
            // "The Arawak tribe has fallen upon hard times ... Will our
            // brothers of Jamestown share the bounty of their harvests?"
            v.amount = food / 2;
            if (nation == human()) {
                const auto t = static_cast<std::size_t>(tribe);
                GameNotice notice{"INDIANBEGFOOD",
                                  {t < rules_.tribes.size() ? rules_.tribes[t].adjective : "", c.name}};
                notice.value = tribe;
                notice.unit = brave;
                notice.colony = v.colony;
                notice.numbers = {v.amount, food};
                notices_.push_back(std::move(notice));
                v.kind = 0;
                visits_.push_back(v);
            } else {
                // A computer player gives, except Spain.
                visitAfterBegging(v, nation != kSpain);
            }
            save_.units[static_cast<std::size_t>(brave)].movesUsed = movementAllowance(brave);
            return true;
        }
    }
    if (!visitOn(v)) {
        return false;
    }
    // The visit is the brave's turn.
    save_.units[static_cast<std::size_t>(brave)].movesUsed = movementAllowance(brave);
    return true;
}

void Game::visitAfterBegging(BraveVisitState v, bool give) {
    auto& block = save_.tribesAndMore;
    auto& home = save_.settlements[static_cast<std::size_t>(v.home)];
    const auto warning = tribeByte(v.tribe, kWarning + 2 * static_cast<std::size_t>(v.nation));
    if (give) {
        auto& c = save_.colonies[static_cast<std::size_t>(v.colony)];
        c.stock[kFood] -= v.amount;
        setAlarm(home, v.nation, 0);
        int calming = (home.raw[3] & kSettlementCapital) != 0 ? -10 : -5;
        while (tribeTension(save_, v.tribe, v.nation) + calming > kCalmTension) {
            calming -= 5;
        }
        changeTension(v.tribe, v.nation, calming);
        v.begged = true;
        if (!v.friendly) {
            if (nextRandom(save_.difficulty + 1) != 0) {
                return;
            }
            v.friendly = true;
            block[warning] = kMet;
            block[warning + 1] = 0;
        }
    } else {
        // The settlement takes it badly. (The original also means to make
        // the tribe tenser, but passes the tribe where the nation belongs,
        // so the tension stays as it was.)
        const int alarm = alarmOf(home, v.nation);
        setAlarm(home, v.nation, alarm + (alarm >> 1));
        if (block[warning] == kMet) {
            block[warning] = 0;
            block[warning + 1] = 0;
        }
        if (v.friendly) {
            return;
        }
    }
    visitOn(v);
}

bool Game::visitOn(BraveVisitState v) {
    auto& block = save_.tribesAndMore;
    const auto warning = tribeByte(v.tribe, kWarning + 2 * static_cast<std::size_t>(v.nation));
    if (!v.friendly) {
        // An unfriendly visit: a demand, unless the tribe met the nation
        // this turn already; or only words.
        if (block[warning] != kMet && (v.colony >= 0 || v.wagon >= 0)) {
            return visitDemand(v);
        }
        if (v.nation == human()) {
            // "The Arawak are pleased to see the prosperity of our
            // neighbors the English. We are concerned, however, ..."
            const auto t = static_cast<std::size_t>(v.tribe);
            GameNotice notice{"INDIANCOMMENT", {t < rules_.tribes.size() ? rules_.tribes[t].adjective : "", ""}};
            notice.adjectives = {{1, v.nation}};
            notice.value = v.tribe;
            notice.unit = v.brave;
            notices_.push_back(std::move(notice));
        }
        setAlarm(save_.settlements[static_cast<std::size_t>(v.home)], v.nation, 0);
        return false;
    }
    if (v.colony < 0) {
        return false;
    }
    block[warning] = kMet;
    block[warning + 1] = 0;
    auto& home = save_.settlements[static_cast<std::size_t>(v.home)];
    auto& c = save_.colonies[static_cast<std::size_t>(v.colony)];
    const int capacity = storageLimit(c);
    setAlarm(home, v.nation, 0);
    const auto t = static_cast<std::size_t>(v.tribe);
    const std::string tribeName = t < rules_.tribes.size() ? rules_.tribes[t].adjective : "";
    const int level = tribeLevel(save_, v.tribe);
    const bool human = v.nation == this->human();
    // Converts, from the nation's own mission in the brave's settlement.
    if ((home.raw[kMission] & 0x0F) == v.nation) {
        int chance = level + 2;
        if ((home.raw[kMission] & kExpertMission) != 0) {
            chance *= 2;
        }
        if (nextRandom(16) < chance) {
            if (human) {
                // "The wisdom of your missionaries has convinced some of
                // us to join your colony at Jamestown ..."
                GameNotice notice{"INDIANSCONVERT", {c.name}};
                notice.value = v.tribe;
                notice.unit = v.brave;
                notice.colony = v.colony;
                notices_.push_back(std::move(notice));
            }
            if (unitCount() >= kMaxUnits) {
                return false;
            }
            addColonist(c.x, c.y, c.nation);
            save_.units.back().profession = kConvert;
            save_.units.back().raw[23] = kConvert;
            return true;
        }
    }
    if (v.makes[kFood] > v.wants[kFood] && !v.begged && c.stock[kFood] <= kLowFood) {
        // "The Arawak see that your food stores are low this season ..."
        const int gift = kBegStock - c.stock[kFood];
        c.stock[kFood] += gift;
        if (human) {
            GameNotice notice{"INDIANGIVEFOOD", {tribeName}};
            notice.value = v.tribe;
            notice.unit = v.brave;
            notice.colony = v.colony;
            notice.numbers = {gift};
            notices_.push_back(std::move(notice));
        }
        return true;
    }
    // A gift of one of the three goods the settlement makes most of, but
    // not food, not what the colony has (almost) no room for, and silver
    // only from a tribe of cities.
    auto makes = v.makes;
    makes[kFood] = 0;
    std::array<int, 16> order{};
    for (std::size_t g = 0; g < makes.size(); ++g) {
        if (c.stock[g] > capacity - 10) {
            makes[g] = 1;
        }
    }
    originalSort(makes, order);
    int place = 1 + nextRandom(3);
    int good = -1;
    while (place <= 16) {
        good = order[static_cast<std::size_t>(16 - place)];
        if (good != kFood && !(good == kSilver && level < 2)) {
            break;
        }
        ++place;
    }
    if (place > 16) {
        return true;
    }
    const auto g = static_cast<std::size_t>(good);
    int amount = 100 / (bidPrice(save_.nations[static_cast<std::size_t>(c.nation)], good) + 1);
    amount = std::min(amount, makes[static_cast<std::size_t>(16 - place)] + 5);
    amount = std::max(5, std::min(amount, 100));
    amount = std::max(2, std::min(amount, capacity - c.stock[g]));
    c.stock[g] += amount;
    if (human) {
        // "The Arawak tribe is pleased to see the progress of our
        // neighbors at Jamestown. We have come to offer you 40 furs ..."
        GameNotice notice{"INDIANGIVESTUFF", {tribeName, c.name, g < rules_.cargo.size() ? rules_.cargo[g].name : ""}};
        notice.value = v.tribe;
        notice.unit = v.brave;
        notice.colony = v.colony;
        notice.numbers = {amount};
        notices_.push_back(std::move(notice));
    }
    return true;
}

bool Game::visitDemand(BraveVisitState v) {
    if (v.colony < 0) {
        return visitWagons(v);
    }
    auto& block = save_.tribesAndMore;
    const auto& c = save_.colonies[static_cast<std::size_t>(v.colony)];
    const auto& prices = save_.nations[static_cast<std::size_t>(v.nation)];
    const int level = tribeLevel(save_, v.tribe);
    // The good the tribe wants most from the colony stores: its worth in
    // Europe times how much there is (up to 100); horses count more for a
    // tribe with few herds, muskets with the difficulty.
    int best = 0;
    int good = -1;
    int amount = 0;
    for (int g = 0; g < kGoodsKinds; ++g) {
        const int have = std::min(c.stock[static_cast<std::size_t>(g)], 100);
        int worth = bidPrice(prices, g);
        if (g == kHorses) {
            worth = worth - static_cast<std::int8_t>(block[tribeByte(v.tribe, kTribeHerds)]) + 10;
        }
        if (g == kMuskets) {
            worth += 1 + nextRandom(4) - level + save_.difficulty + 4;
        }
        const int score = worth * have;
        if (score > best) {
            best = score;
            good = g;
            amount = have;
        }
    }
    if (good < 0) {
        // Nothing in the stores worth asking for: the wagon train, if any,
        // or words.
        v.colony = -1;
        return visitOn(v);
    }
    const auto warning = tribeByte(v.tribe, kWarning + 2 * static_cast<std::size_t>(v.nation));
    block[warning] = kDemanded;
    block[warning + 1] = 0;
    if (nextRandom(save_.difficulty + 2) == 0) {
        amount >>= 1;
    }
    v.kind = 1;
    v.good = good;
    v.amount = amount;
    v.score = best;
    if (v.nation == human()) {
        // "The English settlers in Jamestown have committed intolerable
        // acts of destruction against Arawak lands ... We therefore demand
        // 50 furs from the colony stores as reparations."
        const auto t = static_cast<std::size_t>(v.tribe);
        const auto gi = static_cast<std::size_t>(good);
        GameNotice notice{"INDIANCITY",
                          {"", t < rules_.tribes.size() ? rules_.tribes[t].adjective : "",
                           gi < rules_.cargo.size() ? rules_.cargo[gi].name : "", c.name}};
        notice.adjectives = {{0, v.nation}};
        notice.value = v.tribe;
        notice.unit = v.brave;
        notice.colony = v.colony;
        notice.numbers = {amount};
        notices_.push_back(std::move(notice));
        visits_.push_back(v);
        return true;
    }
    // A computer player hands them over, but not muskets, and not when
    // there are soldiers in the colony.
    bool soldiers = false;
    for (const int i : unitsAt(c.x, c.y)) {
        const int type = unit(i).type;
        if (type >= kFirstShip && type <= kLastShip) {
            continue;
        }
        if (type >= 0 && static_cast<std::size_t>(type) < rules_.units.size() &&
            rules_.units[static_cast<std::size_t>(type)].attack > 1) {
            soldiers = true;
        }
    }
    answerDemand(v, good != kMuskets && !soldiers);
    return true;
}

bool Game::visitWagons(BraveVisitState v) {
    if (v.wagon < 0) {
        return false;
    }
    auto& block = save_.tribesAndMore;
    const auto warning = tribeByte(v.tribe, kWarning + 2 * static_cast<std::size_t>(v.nation));
    block[warning] = kDemanded;
    block[warning + 1] = 0;
    const auto& w = unit(v.wagon);
    if (w.holds <= 0) {
        // (The original goes on with the demand for nothing.)
        return false;
    }
    v.kind = 2;
    v.good = holdGood(w, 0);
    v.amount = holdAmount(w, 0);
    if (v.nation == human()) {
        // "The English settlers have committed intolerable acts of
        // destruction ... We therefore demand all 100 of the furs in these
        // wagons as reparations."
        const auto t = static_cast<std::size_t>(v.tribe);
        const auto g = static_cast<std::size_t>(v.good);
        GameNotice notice{"INDIANWAGONS",
                          {"", t < rules_.tribes.size() ? rules_.tribes[t].adjective : "",
                           g < rules_.cargo.size() ? rules_.cargo[g].name : ""}};
        notice.adjectives = {{0, v.nation}};
        notice.value = v.tribe;
        notice.unit = v.brave;
        notice.numbers = {v.amount};
        notices_.push_back(std::move(notice));
        visits_.push_back(v);
        return true;
    }
    answerDemand(v, true); // a computer player always hands them over
    return true;
}

void Game::answerDemand(const BraveVisitState& v, bool give) {
    auto& home = save_.settlements[static_cast<std::size_t>(v.home)];
    if (!give) {
        setAlarm(home, v.nation, alarmOf(home, v.nation) + kAngryAlarm);
        return;
    }
    auto& block = save_.tribesAndMore;
    const auto g = static_cast<std::size_t>(v.good);
    if (v.kind == 2) {
        // The wagon train's first load goes, which calms the tribe by its
        // worth.
        const int calming = bidPrice(save_.nations[static_cast<std::size_t>(v.nation)], v.good) * v.amount * 4 / -100;
        setAlarm(home, v.nation, 0);
        changeTension(v.tribe, v.nation, calming);
        if (v.wagon >= 0 && v.wagon < unitCount()) {
            removeHold(save_.units[static_cast<std::size_t>(v.wagon)], 0);
        }
        return;
    }
    setAlarm(home, v.nation, 0);
    int calming = v.score * 4 / -100;
    while (tribeTension(save_, v.tribe, v.nation) + calming > kCalmTension) {
        calming -= 5;
    }
    changeTension(v.tribe, v.nation, calming);
    auto& c = save_.colonies[static_cast<std::size_t>(v.colony)];
    c.stock[g] -= v.amount;
    if (v.brave < 0 || v.brave >= unitCount()) {
        return;
    }
    auto& b = save_.units[static_cast<std::size_t>(v.brave)];
    if (v.good == kMuskets) {
        // Muskets arm the brave, or go to the tribe's store.
        if (!armed(b.type)) {
            ++b.type;
        } else {
            auto& muskets = block[tribeByte(v.tribe, kTribeMuskets)];
            muskets = static_cast<std::uint8_t>(muskets + 1);
        }
    }
    if (v.good == kHorses) {
        // Horses mount the brave, or join the tribe's horses; the herds
        // grow either way.
        if (!mounted(b.type)) {
            b.type += 2;
        } else {
            const auto at = tribeByte(v.tribe, kTribeHorses);
            const int horses = static_cast<std::int16_t>(block[at] | (block[at + 1] << 8)) + 50;
            block[at] = static_cast<std::uint8_t>(horses & 0xFF);
            block[at + 1] = static_cast<std::uint8_t>((horses >> 8) & 0xFF);
        }
        auto& herds = block[tribeByte(v.tribe, kTribeHerds)];
        herds = static_cast<std::uint8_t>(herds + 1);
    }
}

void Game::answerVisit(bool give) {
    if (visits_.empty()) {
        return;
    }
    const auto v = visits_.front();
    visits_.erase(visits_.begin());
    if (v.home < 0 || v.home >= static_cast<int>(save_.settlements.size())) {
        return;
    }
    if (v.kind == 0) {
        visitAfterBegging(v, give);
    } else {
        answerDemand(v, give);
    }
}

} // namespace viceroy
