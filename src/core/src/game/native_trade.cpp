// SPDX-License-Identifier: GPL-3.0-or-later
//
// Trade with the natives, read from the original with a debugger and
// checked against its offers; see docs/rules/natives.md.
#include "viceroy/game/europe.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"

#include <algorithm>

namespace viceroy {

namespace {

constexpr int kHorses = 8;
constexpr int kRum = 9;
constexpr int kTradeGoods = 13;
constexpr int kTools = 14;
constexpr int kMuskets = 15;
constexpr int kFood = 0;
constexpr int kSilver = 7;

// Settlement bytes: 7 the good it no longer takes after haggling (0xFE:
// it sells nothing more), 8 the good last sold to it, 9 the good last
// bought from it; from 10 one alarm word per European nation.
constexpr std::size_t kRefused = 7;
constexpr std::size_t kLastSold = 8;
constexpr std::size_t kLastBought = 9;
constexpr std::uint8_t kNoGood = 0xFF;
constexpr std::uint8_t kRefusesToSell = 0xFE;
// Tribe record: from byte 58 one relation byte per European nation, 0x20
// once its men have met the tribe on land.
constexpr std::size_t kTribeRelations = 58;
constexpr std::uint8_t kTribeMet = 0x20;
constexpr int kTenseTrade = 75;   // tension from which a tribe refuses trade
constexpr int kAlarmedShips = 64; // a settlement's alarm that sends ships away

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

// A sale or a gift calms the settlement: its alarm drops by the amount
// (all of it for a full hundred).
void calmAlarm(SaveSettlement& s, int nation, int by, int amount) {
    setAlarm(s, nation, amount == 100 ? 0 : std::max(0, alarmOf(s, nation) - by));
}

std::uint8_t* tribeBytes(SaveGame& save, int tribe) {
    const auto offset = static_cast<std::size_t>(tribe) * kTribeRecordSize;
    if (tribe < 0 || tribe >= kTribes || save.tribesAndMore.size() < offset + kTribeRecordSize) {
        return nullptr;
    }
    return save.tribesAndMore.data() + offset;
}

void addWord(std::uint8_t* p, int by) {
    const auto v = static_cast<std::uint16_t>(static_cast<std::int16_t>(static_cast<std::uint16_t>(p[0] | (p[1] << 8))) + by);
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}

} // namespace

bool Game::canTradeWithVillage(int index, int settlement) const {
    const auto& u = unit(index);
    const auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    return (u.type == unit_type::kWagonTrain || isShip(index)) && u.nation < kEuropeanNations &&
           metTribe(save_, u.nation, tribeOf(s));
}

std::string Game::shipTradeRefusal(int index, int settlement) const {
    // Read from the original: a ship that comes to a village of a tribe
    // that has not met the nation's men on land is sent away; so is one
    // when the tribe's tension is 75 or more or the settlement's alarm 64.
    const auto& u = unit(index);
    if (!isShip(index) || u.nation < 0 || u.nation >= kEuropeanNations) {
        return {};
    }
    const auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    const int tribe = tribeOf(s);
    const auto at =
        static_cast<std::size_t>(tribe) * kTribeRecordSize + kTribeRelations + static_cast<std::size_t>(u.nation);
    const int relation = at < save_.tribesAndMore.size() ? save_.tribesAndMore[at] : 0;
    if ((relation & kTribeMet) == 0) {
        return "DONTKNOWSHIPS";
    }
    if (tribeTension(save_, tribe, u.nation) >= kTenseTrade || alarmOf(s, u.nation) >= kAlarmedShips) {
        return "MADATSHIPS";
    }
    return {};
}

std::string Game::wagonTradeWelcome(int index, int settlement) {
    // Read from the original: a tribe whose tension is 75 or more rolls
    // 0..500 against it: at or under the tension the wagon train vanishes,
    // under twice the tension the tribe will not trade, and above that it
    // trades grudgingly.
    const auto& u = unit(index);
    if (u.type != unit_type::kWagonTrain || u.nation < 0 || u.nation >= kEuropeanNations) {
        return {};
    }
    const int tribe = tribeOf(save_.settlements[static_cast<std::size_t>(settlement)]);
    const int tension = tribeTension(save_, tribe, u.nation);
    if (tension < kTenseTrade) {
        return {};
    }
    const int r = nextRandom(501);
    if (r <= tension) {
        removeUnit(index);
        return "KILLWAGONS";
    }
    return 2 * tension >= r ? "MADATWAGONS" : "GRUDGEWAGONS";
}

Game::VillageSale Game::offerToVillage(int index, int settlement, int hold) {
    auto& u = save_.units[static_cast<std::size_t>(index)];
    const auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    const int tribe = tribeOf(s);
    u.movesUsed = movementAllowance(index);
    const auto roll = [this](int low, int high) { return low + nextRandom(high - low + 1); };
    VillageSale sale;
    sale.hold = hold;
    sale.good = holdGood(u, hold);
    sale.amount = holdAmount(u, hold);
    const auto market = villageMarket(save_, settlement);
    const auto& wants = market.wants;
    const int g = sale.good;
    const int want = wants[static_cast<std::size_t>(g)];
    if (g == static_cast<std::int8_t>(s.raw[kLastSold]) || g == static_cast<std::int8_t>(s.raw[kLastBought]) ||
        want == 0) {
        sale.refusal = "BADCARGO";
        sale.wants = topWants(wants, s);
        return sale;
    }
    if (g == static_cast<std::int8_t>(s.raw[kRefused])) {
        sale.refusal = "BADHAGGLE1";
        return sale;
    }
    const std::uint8_t* t = tribeBytes(save_, tribe);
    const int luck = roll(1, 5);
    int worth = g > kHorses ? 7 : 6;
    if (g == kTradeGoods) {
        worth -= roll(0, 7);
    }
    if (g == kMuskets) {
        worth += 12 - (t ? static_cast<std::int8_t>(t[7]) : 0);
    }
    if (g == kHorses) {
        worth += 10 - (t ? static_cast<std::int8_t>(t[8]) : 0);
    }
    if (g == kTools) {
        worth += 1;
    }
    int temper = 2 * tensionLevel(tribeTension(save_, tribe, u.nation));
    if (g == kMuskets || g == kHorses) {
        temper = 0;
    }
    if (want >= 20) {
        temper >>= 1;
    }
    const int weight = std::max(0, 2 * (worth - save_.difficulty - temper + luck + 4) * want);
    sale.temper = temper;
    sale.price = std::max(1, ((5 * luck + weight) * sale.amount / 100) / 2);
    sale.quality = std::min(3, (want - temper + 4) / 10);
    sale.calm = roll(0, 1) + ((want - temper + 4) >> 2);
    sale.ask = (want + 1) * 4 + sale.price;
    return sale;
}

void Game::sellToVillage(int index, int settlement, const VillageSale& sale) {
    auto& u = save_.units[static_cast<std::size_t>(index)];
    auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    const int tribe = tribeOf(s);
    const int nation = u.nation;
    removeHold(u, sale.hold);
    save_.nations[static_cast<std::size_t>(nation)].gold += sale.price;
    auto* t = tribeBytes(save_, tribe);
    if (t) {
        addWord(t + 0x0E + 2 * sale.good, sale.amount);
    }
    s.raw[kRefused] = kNoGood;
    if (sale.calm > 0) {
        changeTension(tribe, nation, -2 * sale.calm);
        calmAlarm(s, nation, sale.amount, sale.amount);
    }
    // The original compares the unit's number, not the good, with muskets
    // and horses here; kept as it is.
    s.raw[kLastSold] = index == kMuskets || index == kHorses ? kNoGood : static_cast<std::uint8_t>(sale.good);
    if (t && sale.good == kMuskets) {
        t[7] = static_cast<std::uint8_t>(t[7] + (sale.amount >= 25 ? 1 : 0) + (sale.amount >= 50 ? 1 : 0));
    }
    if (t && sale.good == kHorses) {
        addWord(t + 0x0A, sale.amount >> 2);
        t[8] = static_cast<std::uint8_t>(t[8] + (sale.amount >= 25 ? 1 : 0) + (sale.amount >= 50 ? 1 : 0));
    }
}

bool Game::haggleSale(int index, int settlement, VillageSale& sale) {
    const int nation = unit(index).nation;
    auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    const auto roll = [this](int low, int high) { return low + nextRandom(high - low + 1); };
    if (sale.calm > 0 && roll(1, 8 * sale.calm) > save_.difficulty) {
        --sale.calm;
        const int want = villageMarket(save_, settlement).wants[static_cast<std::size_t>(sale.good)];
        sale.price += std::max(1, roll(want / 2 + 1, 2 * want + 1) * sale.amount / 100);
        if (sale.price >= sale.ask) {
            sale.ask = sale.price + 10;
        }
        sale.haggles = 1;
        return true;
    }
    s.raw[kRefused] = static_cast<std::uint8_t>(sale.good);
    changeTension(tribeOf(s), nation, (sale.temper >> 1) + 1);
    return false;
}

void Game::giveToVillage(int index, int settlement, const VillageSale& sale) {
    auto& u = save_.units[static_cast<std::size_t>(index)];
    auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    const int tribe = tribeOf(s);
    const int nation = u.nation;
    removeHold(u, sale.hold);
    s.raw[kRefused] = kNoGood;
    s.raw[kLastSold] = index == kMuskets || index == kHorses ? kNoGood : static_cast<std::uint8_t>(sale.good);
    if (sale.calm >= 0) {
        changeTension(tribe, nation, -4 * (sale.calm + 1));
        calmAlarm(s, nation, 2 * sale.amount, sale.amount);
    }
    auto* t = tribeBytes(save_, tribe);
    if (t && sale.good == kMuskets) {
        ++t[7];
    }
    if (t && sale.good == kHorses) {
        addWord(t + 0x0A, sale.amount >> 2);
        ++t[8];
    }
}

bool Game::hasRoomToBuy(int index) const {
    const auto& u = unit(index);
    if (u.type < 0 || u.type >= static_cast<int>(rules_.units.size())) {
        return false;
    }
    return rules_.units[static_cast<std::size_t>(u.type)].cargoSlots - u.holds >= 1;
}

bool Game::refusesToSell(int settlement) const {
    return save_.settlements[static_cast<std::size_t>(settlement)].raw[kRefused] == kRefusesToSell;
}

std::vector<int> Game::villageGoodsForSale(int settlement) const {
    // What it makes most of, leaving out muskets, food, tools and trade
    // goods.
    const auto market = villageMarket(save_, settlement);
    std::vector<int> goods;
    for (int k = 15; k >= 0 && goods.size() < 3; --k) {
        const int g = market.makesOrder[static_cast<std::size_t>(k)];
        if (g != kMuskets && g != kFood && g != kTools && g != kTradeGoods) {
            goods.push_back(g);
        }
    }
    return goods;
}

Game::VillagePurchase Game::offerFromVillage(int index, int settlement, int good, int sold) {
    const auto& u = unit(index);
    const auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    const int tribe = tribeOf(s);
    const auto roll = [this](int low, int high) { return low + nextRandom(high - low + 1); };
    VillagePurchase p;
    p.good = good;
    p.amount = isShip(index) ? sold >> 2 : sold;
    const auto market = villageMarket(save_, settlement);
    int made = 0;
    for (std::size_t k = 0; k < 16; ++k) {
        if (market.makesOrder[k] == good) {
            made = market.makes[k];
        }
    }
    int price = 200;
    if (good >= kHorses) {
        price = (8 - tribeLevel(save_, tribe)) * 50;
    }
    if (good >= kSilver) {
        const auto& nation = save_.nations[static_cast<std::size_t>(u.nation)];
        price += bidPrice(nation, good) * (save_.difficulty * 2 + 15);
    }
    price += roll(0, price);
    price -= made * 4;
    price += tribeTension(save_, tribe, u.nation) * 4;
    price = p.amount * price / 100;
    price += (save_.difficulty + roll(0, 2)) * 10;
    p.price = std::max(50, price);
    return p;
}

bool Game::buyFromVillage(int index, int settlement, const VillagePurchase& purchase) {
    auto& u = save_.units[static_cast<std::size_t>(index)];
    auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    const int tribe = tribeOf(s);
    auto& nation = save_.nations[static_cast<std::size_t>(u.nation)];
    if (nation.gold < purchase.price) {
        changeTension(tribe, u.nation, 1);
        return false;
    }
    nation.gold -= purchase.price;
    s.raw[kLastBought] = purchase.good == kRum ? kNoGood : static_cast<std::uint8_t>(purchase.good);
    if (auto* t = tribeBytes(save_, tribe)) {
        addWord(t + 0x0E + 2 * purchase.good, -purchase.amount);
    }
    setHold(u, u.holds, purchase.good, purchase.amount);
    ++u.holds;
    const int calm = nextRandom(purchase.price / 25 + 2);
    changeTension(tribe, u.nation, -calm);
    return true;
}

bool Game::hagglePurchase(int index, int settlement, VillagePurchase& purchase) {
    const auto& u = unit(index);
    auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    const int tribe = tribeOf(s);
    const auto roll = [this](int low, int high) { return low + nextRandom(high - low + 1); };
    const auto market = villageMarket(save_, settlement);
    int made = 0;
    for (std::size_t k = 0; k < 16; ++k) {
        if (market.makesOrder[k] == purchase.good) {
            made = market.makes[k];
        }
    }
    const int patience = roll(0, made / 25 + 8);
    if (purchase.price > 10 && patience > save_.difficulty + 1) {
        purchase.price = std::max(10, purchase.price - std::max(1, purchase.price / 4));
        if (roll(1, 8 - save_.difficulty) == 1) {
            changeTension(tribe, u.nation, 1);
        }
        purchase.haggles = 1;
        return true;
    }
    changeTension(tribe, u.nation, 2);
    s.raw[kRefused] = kRefusesToSell;
    return false;
}

} // namespace viceroy
