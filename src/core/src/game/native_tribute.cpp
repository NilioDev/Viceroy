// SPDX-License-Identifier: GPL-3.0-or-later
//
// A unit demands tribute from a native settlement, and the count of each
// nation's forces by region that the answer weighs. Read from the original
// with a debugger and checked against its answers; see
// docs/rules/natives.md.
#include "viceroy/game/colony.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"

#include <algorithm>

namespace viceroy {

namespace {

constexpr int kArtillery = 11;
constexpr int kFirstShip = 13;
constexpr int kLastShip = 18;
constexpr int kVeteranSoldier = 0x15;
constexpr int kSpain = 2;
constexpr int kCortes = 10; // row of @FATHERS
constexpr int kRegions = 16;

// Where the count lives in the block after the tribe records (kept in the
// save as the original writes it): each European nation's attack in all
// (16 bits), its attack outside colonies by region, each tribe's defence in
// all and by region (bytes that stop at 255).
constexpr std::size_t kExtra = static_cast<std::size_t>(kTribes) * kTribeRecordSize;
constexpr std::size_t kNationAttack = kExtra + 44;
constexpr std::size_t kNationRegionAttack = kExtra + 300;
constexpr std::size_t kTribeDefence = kExtra + 565;
constexpr std::size_t kTribeRegionDefence = kExtra + 589;
constexpr std::size_t kCountEnd = kTribeRegionDefence + static_cast<std::size_t>(kTribes) * kRegions;

bool isShipType(int type) {
    return type >= kFirstShip && type <= kLastShip;
}

void addCapped(std::uint8_t& byte, int value) {
    byte = static_cast<std::uint8_t>(std::min(255, byte + value));
}

} // namespace

int Game::unitForce(const SaveUnit& u, bool attacking) const {
    const int type = u.type;
    if (type < 0 || type >= static_cast<int>(rules_.units.size())) {
        return 0;
    }
    const auto& t = rules_.units[static_cast<std::size_t>(type)];
    int v = attacking ? t.attack : t.combat;
    if (type == kArtillery && (u.raw[4] & SaveUnit::kDamaged) != 0) {
        v -= 2;
    }
    v *= 8;
    if ((type == unit_type::kSoldier || type == unit_type::kDragoon) && u.profession == kVeteranSoldier) {
        v += v >> 1;
    }
    return v;
}

void Game::countForces() {
    auto& block = save_.tribesAndMore;
    if (block.size() < kCountEnd) {
        return;
    }
    for (int n = 0; n < kEuropeanNations; ++n) {
        block[kNationAttack + 2 * static_cast<std::size_t>(n)] = 0;
        block[kNationAttack + 2 * static_cast<std::size_t>(n) + 1] = 0;
        std::fill_n(block.begin() + static_cast<std::ptrdiff_t>(kNationRegionAttack + kRegions * static_cast<std::size_t>(n)),
                    kRegions, std::uint8_t{0});
    }
    std::fill(block.begin() + static_cast<std::ptrdiff_t>(kTribeDefence),
              block.begin() + static_cast<std::ptrdiff_t>(kTribeDefence + kTribes), std::uint8_t{0});
    std::fill(block.begin() + static_cast<std::ptrdiff_t>(kTribeRegionDefence),
              block.begin() + static_cast<std::ptrdiff_t>(kCountEnd), std::uint8_t{0});
    const auto& map = save_.map;
    for (const auto& u : save_.units) {
        if (isShipType(u.type)) {
            continue;
        }
        const int region = landRegion(save_.map, u.x, u.y);
        if (u.nation >= kEuropeanNations && u.nation < kEuropeanNations + kTribes) {
            const auto tribe = static_cast<std::size_t>(u.nation - kEuropeanNations);
            const int d = unitForce(u, false);
            addCapped(block[kTribeDefence + tribe], d);
            if (region >= 0) {
                addCapped(block[kTribeRegionDefence + kRegions * tribe + static_cast<std::size_t>(region)], d);
            }
            continue;
        }
        if (u.nation < 0 || u.nation >= kEuropeanNations) {
            continue;
        }
        const auto n = static_cast<std::size_t>(u.nation);
        const int a = unitForce(u, true);
        const auto at = kNationAttack + 2 * n;
        const auto total = static_cast<std::uint16_t>((block[at] | (block[at + 1] << 8)) + a);
        block[at] = static_cast<std::uint8_t>(total & 0xFF);
        block[at + 1] = static_cast<std::uint8_t>(total >> 8);
        // Units in a colony stay out of the regional count of a human
        // nation, and of a computer one when byte 7 holds 'A' or 'G'.
        const bool inColony = region >= 0 &&
                              (map.at(u.x, u.y).flags & MapSquare::kHasColony) != 0 &&
                              (map.at(u.x, u.y).region >> 4) != 0x0F;
        if (inColony && (save_.players[n].control == 0 || u.raw[7] == 0x41 || u.raw[7] == 0x47)) {
            continue;
        }
        if (region >= 0) {
            addCapped(block[kNationRegionAttack + kRegions * n + static_cast<std::size_t>(region)], a);
        }
    }
}

bool Game::canDemandTribute(int index, int settlement) const {
    const auto& u = unit(index);
    const auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    if (u.nation >= kEuropeanNations || !metTribe(save_, u.nation, tribeOf(s)) || isShipType(u.type) ||
        u.type == unit_type::kMissionary) {
        return false;
    }
    return u.type >= 0 && u.type < static_cast<int>(rules_.units.size()) &&
           rules_.units[static_cast<std::size_t>(u.type)].attack > 0;
}

Game::Tribute Game::demandTribute(int index, int settlement) {
    auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    const int tribe = tribeOf(s);
    const auto& u = unit(index);
    const int nation = u.nation;
    const int tension = tribeTension(save_, tribe, nation);
    // The original reads the tables at the unit's region even when the
    // unit is not on land; next to a settlement it always is.
    const int unitRegion = landRegion(save_.map, u.x, u.y);
    const int region = std::max(0, unitRegion);
    const auto roll = [this](int low, int high) { return low + nextRandom(high - low + 1); };
    const auto& block = save_.tribesAndMore;
    const auto byteAt = [&](std::size_t at) { return at < block.size() ? int{block[at]} : 0; };
    const auto r = static_cast<std::size_t>(region);
    const auto n = static_cast<std::size_t>(nation);
    const auto t = static_cast<std::size_t>(tribe);
    Tribute answer;
    save_.units[static_cast<std::size_t>(index)].movesUsed = movementAllowance(index);

    // The nation's might near the village against the tribe's.
    const int total =
        static_cast<std::int16_t>(static_cast<std::uint16_t>(byteAt(kNationAttack + 2 * n) | (byteAt(kNationAttack + 2 * n + 1) << 8)));
    int might = byteAt(kNationRegionAttack + kRegions * n + r) + (total >> 1);
    if (nation == kSpain) {
        might += might >> 1;
    }
    if (hasFather(nation, kCortes)) {
        might += might >> 1;
    }
    const int defence =
        2 * (byteAt(kTribeRegionDefence + kRegions * t + r) + (byteAt(kTribeDefence + t) >> 1)) + (tension >> 1);
    int anger = save_.players[n].control == 0 ? save_.difficulty + 1 : 1;
    const int ours = roll(0, might);
    const int theirs = roll(0, defence);
    bool cowed = theirs < ours;
    answer.colony = nearestColony(save_, u.x, u.y, nation, unitRegion);
    if (answer.colony < 0) {
        cowed = false;
    }

    if ((!cowed && might <= defence) || tension >= 75) {
        answer.reply = Tribute::Reply::Laugh;
    } else if (!cowed && tension >= 50) {
        answer.reply = Tribute::Reply::Refuse;
    } else if (!cowed || (s.raw[3] & kSettlementTribute) != 0) {
        answer.reply = Tribute::Reply::Poor;
        anger = 0;
    } else {
        answer.reply = Tribute::Reply::Goods;
        anger *= 2;
        s.raw[3] = static_cast<std::uint8_t>(s.raw[3] | kSettlementTribute);
        // What the village makes most of, as much as the colony's
        // warehouse has room for, at most three times what it makes and
        // 10 more (at most 100), but at least 10.
        auto makes = villageEconomy(save_, settlement).makes;
        std::array<int, 16> order{};
        originalSort(makes, order);
        auto& colony = save_.colonies[static_cast<std::size_t>(answer.colony)];
        answer.good = order[15];
        const auto g = static_cast<std::size_t>(answer.good);
        answer.amount = std::max(10, std::min(storageLimit(colony) - colony.stock[g], std::min(100, 3 * makes[15] + 10)));
        colony.stock[g] += answer.amount;
    }
    if (anger != 0) {
        setTribeTension(save_, tribe, nation, std::max(0, tension + anger));
    }
    return answer;
}

} // namespace viceroy
