// SPDX-License-Identifier: GPL-3.0-or-later
//
// How a tribe's tension toward a nation moves, and a missionary inciting a
// tribe against another European nation. Read from the original with a
// debugger and checked against its answers; see docs/rules/natives.md.
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"

#include <algorithm>

namespace viceroy {

namespace {

constexpr int kFrench = 1;
constexpr int kPocahontas = 16; // row of @FATHERS
constexpr int kMaxTension = 100;
constexpr int kAtWar = 75; // a tribe this tense is already at war
constexpr int kInciteTension = 100;

// Relations: the nation's byte 52 + other, the tribe's byte 58 + nation.
constexpr std::size_t kNationRelations = 52;
constexpr std::size_t kTribeRelations = 58;
constexpr std::uint8_t kMet = 0x20;
constexpr std::uint8_t kWar = 0x04;
constexpr std::uint8_t kAngry = 0x02; // cleared when the tension falls below 75

// In the block after the tribe records: each tribe's defence in all and
// its number of settlements (one byte each).
constexpr std::size_t kExtra = static_cast<std::size_t>(kTribes) * kTribeRecordSize;
constexpr std::size_t kTribeDefence = kExtra + 565;
constexpr std::size_t kSettlementCounts = kExtra + 581;
// The tribe's muskets and the number beside them (bytes 7 and 8).
constexpr std::size_t kTribeMuskets = 7;
constexpr std::size_t kTribeEight = 8;

constexpr int kJesuit = 0x18;
constexpr int kMissionCut = 250;
constexpr int kExpertMissionCut = 1000;
constexpr int kJesuitCut = 1500;
constexpr int kCapitalCut = 500;
constexpr int kLeastPrice = 500;

std::uint8_t blockByte(const SaveGame& save, std::size_t at) {
    return at < save.tribesAndMore.size() ? save.tribesAndMore[at] : 0;
}

void clearRelation(SaveGame& save, int nation, int tribe, std::uint8_t bits) {
    auto& relation = save.nations[static_cast<std::size_t>(nation)]
                         .raw[kNationRelations + static_cast<std::size_t>(kEuropeanNations + tribe)];
    relation = static_cast<std::uint8_t>(relation & ~bits);
    const auto at =
        static_cast<std::size_t>(tribe) * kTribeRecordSize + kTribeRelations + static_cast<std::size_t>(nation);
    if (at < save.tribesAndMore.size()) {
        save.tribesAndMore[at] = static_cast<std::uint8_t>(save.tribesAndMore[at] & ~bits);
    }
}

} // namespace

void Game::changeTension(int tribe, int nation, int amount) {
    if (tribe < 0 || tribe >= kTribes || nation < 0 || nation >= kEuropeanNations) {
        return;
    }
    const int before = std::clamp(tribeTension(save_, tribe, nation), 0, kMaxTension);
    if (amount > 0 && nation == kFrench) {
        amount >>= 1;
    }
    if (amount > 0 && hasFather(nation, kPocahontas)) {
        amount >>= 1;
    }
    const int after = std::clamp(before + amount, 0, kMaxTension);
    setTribeTension(save_, tribe, nation, after);
    if (amount < 0) {
        clearRelation(save_, nation, tribe, kWar);
        if (after < kAtWar) {
            clearRelation(save_, nation, tribe, kAngry);
        }
        // A step of 5 crossed downward calms each of the tribe's
        // settlements toward the nation.
        if (before / 5 != std::min(after, kMaxTension - 1) / 5) {
            const int cap = after < 50 ? 0x20 : 0x60;
            for (auto& s : save_.settlements) {
                if (tribeOf(s) != tribe) {
                    continue;
                }
                const auto at = static_cast<std::size_t>(10 + 2 * nation);
                const int alarm = std::min(s.raw[at] | (s.raw[at + 1] << 8), cap);
                s.raw[at] = static_cast<std::uint8_t>(alarm & 0xFF);
                s.raw[at + 1] = static_cast<std::uint8_t>(alarm >> 8);
            }
        }
    }
    // Not done yet: a rise to 100 with a treaty may make the tribe declare
    // war (a chance of the difficulty level plus 2 in 11).
}

int Game::incitePrice(int index, int settlement) const {
    const auto& u = unit(index);
    const auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    const int tribe = tribeOf(s);
    const int nation = u.nation;
    const auto t = static_cast<std::size_t>(tribe);
    const int settlements = blockByte(save_, kSettlementCounts + t);
    const int defence = blockByte(save_, kTribeDefence + t);
    const int muskets = static_cast<std::int8_t>(blockByte(save_, t * kTribeRecordSize + kTribeMuskets));
    const int eight = static_cast<std::int8_t>(blockByte(save_, t * kTribeRecordSize + kTribeEight));
    // Measured: 6552 for the Apache (6 settlements, defence 56, 0 and 1)
    // at tension 51 toward the Dutch, and 4650 at tension 0 with defence
    // 100.
    long price = static_cast<long>(tribeTension(save_, tribe, nation) + 75) *
                 (6 * settlements + ((defence >> 2) & 0xFE) + 2 * muskets + 2 * eight);
    if (nation == kFrench) {
        price = price * 2 / 3;
    }
    for (const auto& other : save_.settlements) {
        if (tribeOf(other) != tribe || missionOf(other) != nation) {
            continue;
        }
        int cut = (other.raw[5] & kExpertMission) != 0 ? kExpertMissionCut : kMissionCut;
        if ((other.raw[3] & kSettlementCapital) != 0) {
            cut *= 2;
        }
        price -= cut;
    }
    if (u.profession == kJesuit) {
        price -= kJesuitCut;
    }
    if ((s.raw[3] & kSettlementCapital) != 0) {
        price -= kCapitalCut;
    }
    return static_cast<int>(std::max<long>(price, kLeastPrice));
}

bool Game::canIncite(int index, int settlement) const {
    const auto& u = unit(index);
    const auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    return u.type == unit_type::kMissionary && u.nation < kEuropeanNations && metTribe(save_, u.nation, tribeOf(s));
}

std::vector<int> Game::inciteTargets(int index) const {
    std::vector<int> out;
    for (int n = 0; n < kEuropeanNations; ++n) {
        if (n != unit(index).nation) {
            out.push_back(n);
        }
    }
    return out;
}

Game::Incite Game::incite(int index, int settlement, int target, bool pay) {
    const auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    const int tribe = tribeOf(s);
    const int nation = unit(index).nation;
    if (tribe < 0 || tribe >= kTribes || target < 0 || target >= kEuropeanNations || nation < 0 ||
        nation >= kEuropeanNations) {
        return Incite::NoContact;
    }
    const auto& tribes = rules().tribes;
    const auto t = static_cast<std::size_t>(tribe);
    const auto relation = blockByte(save_, t * kTribeRecordSize + kTribeRelations + static_cast<std::size_t>(target));
    if ((relation & kMet) == 0) {
        GameNotice notice{"NOCONTACT", {""}};
        notice.value = tribe;
        notice.adjectives = {{0, target}};
        notices_.push_back(std::move(notice));
        return Incite::NoContact;
    }
    if (!pay) {
        return Incite::Done;
    }
    const int price = incitePrice(index, settlement);
    auto& treasury = save_.nations[static_cast<std::size_t>(nation)].gold;
    if (treasury < price) {
        GameNotice notice{"UNFORTUNATE", {}};
        notice.value = tribe;
        notices_.push_back(std::move(notice));
        return Incite::TooPoor;
    }
    if (tribeTension(save_, tribe, target) >= kAtWar) {
        GameNotice notice{"ALREADYSMITE", {""}};
        notice.value = tribe;
        notice.adjectives = {{0, target}};
        notices_.push_back(std::move(notice));
        return Incite::AlreadyAtWar;
    }
    // "Apache nation holds War Council! Dutch missionaries incite Apache to
    // warfare against the French!"
    GameNotice notice{"INDIANWARFARE",
                      {t < tribes.size() ? tribes[t].adjective : "", "", t < tribes.size() ? tribes[t].name : "", ""}};
    notice.adjectives = {{1, nation}, {3, target}};
    notices_.push_back(std::move(notice));
    changeTension(tribe, target, kInciteTension);
    treasury -= price;
    return Incite::Done;
}

} // namespace viceroy
