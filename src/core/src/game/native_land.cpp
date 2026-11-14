// SPDX-License-Identifier: GPL-3.0-or-later
//
// The natives' land: a tribe claims the squares near its settlements, asks
// a price for them, and bears a grudge over forest cleared and roads built
// there. Read from the original; see docs/rules/natives.md.
#include "viceroy/game/colony.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"
#include "viceroy/game/resource.hpp"

#include <algorithm>
#include <cstdlib>

namespace viceroy {

namespace {

constexpr int kMinuit = 2; // row of @FATHERS: Peter Minuit
constexpr std::size_t kTribeRelations = 58;
constexpr std::size_t kTribeLandSold = 5; // the tribe's count of land sold
constexpr std::uint8_t kTreaty = 0x40;
constexpr std::uint8_t kBought = 0x10; // mask bit: the land has been paid for
constexpr std::size_t kExtra = static_cast<std::size_t>(kTribes) * kTribeRecordSize;
constexpr std::size_t kPeople = kExtra + 28; // each European nation's people

int distance(int dx, int dy) {
    const int ax = std::abs(dx);
    const int ay = std::abs(dy);
    return std::max(ax, ay) + std::min(ax, ay) / 2;
}

std::uint8_t blockByte(const SaveGame& save, std::size_t at) {
    return at < save.tribesAndMore.size() ? save.tribesAndMore[at] : 0;
}

// How far a tribe's land reaches from a settlement, by its level.
int reach(int level) { return level <= 1 ? 1 : level == 2 ? 2 : 3; }

bool isComputer(const SaveGame& save, int nation) {
    return nation < 0 || nation >= kEuropeanNations || save.players[static_cast<std::size_t>(nation)].control != 0;
}

} // namespace

int Game::nearestSettlementIn(int x, int y, int region, int* distanceOut) const {
    // Read from the original: of equals the last in the list.
    int best = -1;
    int bestDistance = 9999;
    for (std::size_t i = 0; i < save_.settlements.size(); ++i) {
        const auto& s = save_.settlements[i];
        if (region >= 0 && landRegion(save_.map, s.x, s.y) != region) {
            continue;
        }
        const int d = distance(s.x - x, s.y - y);
        if (d <= bestDistance) {
            bestDistance = d;
            best = static_cast<int>(i);
        }
    }
    if (distanceOut != nullptr) {
        *distanceOut = bestDistance;
    }
    return best;
}

int Game::landPrice(int settlement, int nation, int x, int y) const {
    // Read from the original: the tribe asks more the nearer the
    // settlement, the higher its level and the more land it has sold,
    // less from a small nation; twice for a resource, more the more tense
    // it is toward the player, half again at its capital, and nothing from
    // a nation with Peter Minuit.
    if (settlement < 0 || settlement >= static_cast<int>(save_.settlements.size()) || nation < 0 ||
        nation >= kEuropeanNations) {
        return 0;
    }
    const auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    const int tribe = tribeOf(s);
    const auto record = static_cast<std::size_t>(tribe) * kTribeRecordSize;
    const int level = tribeLevel(save_, tribe);
    const int sold = blockByte(save_, record + kTribeLandSold);
    const int d = distance(x - s.x, y - s.y);
    const bool human = !isComputer(save_, nation);
    int value = 0;
    int base = 0;
    if (human) {
        value = (save_.difficulty + 3) * 2 + level + sold - d;
        base = 65;
    } else {
        value = level + sold - save_.difficulty - d + 12;
        base = 50;
    }
    value -= std::max(0, (10 - static_cast<int>(blockByte(save_, kPeople + static_cast<std::size_t>(nation)))) >> 1);
    if (save_.map.resourceSeed != 0 && resourceAt(save_.map, x, y) != Resource::None) {
        value *= 2;
    }
    value = std::max(value, 1) * base;
    if (human) {
        value *= tensionLevel(tribeTension(save_, tribe, nation)) + 1;
    }
    if ((s.raw[3] & kSettlementCapital) != 0) {
        value += value >> 1;
    }
    if (hasFather(nation, kMinuit)) {
        value = 0;
    }
    return value >> 1;
}

std::optional<LandClaim> Game::workClaim(int index, int orders) const {
    // Read from the original: before a pioneer of the player starts to
    // clear a forest or build a road on land of a tribe with a treaty.
    const auto& u = unit(index);
    const auto& map = save_.map;
    if (!map.inside(u.x, u.y) || colonyAt(u.x, u.y)) {
        return std::nullopt;
    }
    int d = 0;
    const int settlement = nearestSettlementIn(u.x, u.y, landRegion(map, u.x, u.y), &d);
    if (settlement < 0) {
        return std::nullopt;
    }
    const int tribe = tribeOf(save_.settlements[static_cast<std::size_t>(settlement)]);
    if (reach(tribeLevel(save_, tribe)) < d) {
        return std::nullopt;
    }
    const auto relation = blockByte(save_, static_cast<std::size_t>(tribe) * kTribeRecordSize + kTribeRelations +
                                               static_cast<std::size_t>(u.nation));
    const auto& sq = map.at(u.x, u.y);
    if ((relation & kTreaty) == 0 || (sq.flags & kBought) != 0) {
        return std::nullopt;
    }
    if (orders == SaveUnit::kPlow && !sq.isForest()) {
        return std::nullopt;
    }
    const int price = landPrice(settlement, u.nation, u.x, u.y);
    if (price <= 0) {
        return std::nullopt;
    }
    return LandClaim{tribe, settlement, u.x, u.y, price};
}

std::optional<LandClaim> Game::colonyClaim(int c, int square) const {
    // Read from the original: a colonist of the player put to work on a
    // square a tribe with a treaty claims (as the colony screen marks it).
    if (c < 0 || c >= static_cast<int>(save_.colonies.size()) || square < 0 || square >= 8) {
        return std::nullopt;
    }
    const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    const int tribe = aiTribeClaim(colony, square, landRegion(save_.map, colony.x, colony.y));
    if (tribe < 0 || isComputer(save_, colony.nation)) {
        return std::nullopt;
    }
    const auto relation = blockByte(save_, static_cast<std::size_t>(tribe) * kTribeRecordSize + kTribeRelations +
                                               static_cast<std::size_t>(colony.nation));
    if ((relation & kTreaty) == 0) {
        return std::nullopt;
    }
    const auto& offset = kColonySquares[static_cast<std::size_t>(square)];
    const int x = colony.x + offset.dx;
    const int y = colony.y + offset.dy;
    const int settlement = nearestSettlementIn(x, y, -1, nullptr);
    if (settlement < 0) {
        return std::nullopt;
    }
    return LandClaim{tribe, settlement, x, y, landPrice(settlement, colony.nation, x, y)};
}

bool Game::buyLand(const LandClaim& claim, int nation) {
    if (nation < 0 || nation >= kEuropeanNations || claim.tribe < 0 || !save_.map.inside(claim.x, claim.y)) {
        return false;
    }
    auto& gold = save_.nations[static_cast<std::size_t>(nation)].gold;
    if (gold < claim.price) {
        return false;
    }
    gold -= claim.price;
    const auto at = static_cast<std::size_t>(claim.tribe) * kTribeRecordSize + kTribeLandSold;
    if (at < save_.tribesAndMore.size()) {
        save_.tribesAndMore[at] = static_cast<std::uint8_t>(save_.tribesAndMore[at] + 1);
    }
    auto& sq = save_.map.at(claim.x, claim.y);
    sq.flags = static_cast<std::uint8_t>(sq.flags | kBought);
    return true;
}

void Game::landGrudge(int index, bool road) {
    // Read from the original: when a pioneer has cleared a forest or built
    // a road on a tribe's land that was not paid for, a computer player
    // buys it when it can keep half the price besides; otherwise the tribe
    // grows tense: 5 for a forest, 3 for a road (plus the difficulty level
    // for the player), twice that within 2 of the settlement, three times
    // next to it.
    const auto& u = unit(index);
    const int nation = u.nation;
    const auto& map = save_.map;
    if (nation < 0 || nation >= kEuropeanNations || !map.inside(u.x, u.y)) {
        return;
    }
    int d = 0;
    const int settlement = nearestSettlementIn(u.x, u.y, landRegion(map, u.x, u.y), &d);
    if (settlement < 0 || (map.at(u.x, u.y).flags & kBought) != 0 || colonyAt(u.x, u.y)) {
        return;
    }
    const int tribe = tribeOf(save_.settlements[static_cast<std::size_t>(settlement)]);
    if (d > reach(tribeLevel(save_, tribe)) || hasFather(nation, kMinuit)) {
        return;
    }
    if (isComputer(save_, nation)) {
        const int price = landPrice(settlement, nation, u.x, u.y);
        const int gold = save_.nations[static_cast<std::size_t>(nation)].gold;
        if (gold - price >= price / 2 && buyLand(LandClaim{tribe, settlement, u.x, u.y, price}, nation)) {
            return;
        }
    }
    const int step = (road ? 3 : 5) + (isComputer(save_, nation) ? 0 : save_.difficulty);
    int amount = step;
    if (d <= 2) {
        amount = 2 * step;
    }
    if (d <= 1) {
        amount += step;
    }
    changeTension(tribe, nation, amount);
}

} // namespace viceroy
