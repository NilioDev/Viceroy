// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/game/europe.hpp"

#include <algorithm>

namespace viceroy {

int europeStage(const SaveUnit& unit) {
    if (unit.x != unit.y || unit.x < kEuropeBase || unit.nation < 0 || unit.nation > 3) {
        return -1;
    }
    const int offset = unit.x - kEuropeBase - unit.nation;
    if (offset < 0 || offset % 4 != 0 || offset / 4 > kLastStage) {
        return -1;
    }
    return offset / 4;
}

EuropePlace europePlace(const SaveUnit& unit) {
    const int stage = europeStage(unit);
    if (stage < 0) {
        return EuropePlace::None;
    }
    if (stage == kPortStage) {
        return EuropePlace::InPort;
    }
    return stage < kPortStage ? EuropePlace::ToNewWorld : EuropePlace::ToEurope;
}

void setEuropeStage(SaveUnit& unit, int stage) {
    unit.x = kEuropeBase + (unit.nation & 3) + 4 * std::clamp(stage, 0, kLastStage);
    unit.y = unit.x;
}

bool isShipType(const Ruleset& rules, int type) {
    return type >= 0 && static_cast<std::size_t>(type) < rules.units.size() &&
           rules.units[static_cast<std::size_t>(type)].hull > 0;
}

std::vector<int> unitsInEurope(const SaveGame& save, const Ruleset& rules, int nation, EuropePlace place,
                               bool ships) {
    std::vector<int> out;
    for (std::size_t i = 0; i < save.units.size(); ++i) {
        const auto& u = save.units[i];
        if (u.nation == nation && europePlace(u) == place && isShipType(rules, u.type) == ships) {
            out.push_back(static_cast<int>(i));
        }
    }
    return out;
}

std::vector<int> unitsOnDocks(const SaveGame& save, const Ruleset& rules, int nation) {
    std::vector<int> out;
    for (const int u : unitsInEurope(save, rules, nation, EuropePlace::InPort, false)) {
        if (save.units[static_cast<std::size_t>(u)].orders != SaveUnit::kSentry) {
            out.insert(out.begin(), u);
        }
    }
    return out;
}

int carrierOf(const SaveGame& save, const Ruleset& rules, int unit) {
    const int count = static_cast<int>(save.units.size());
    int at = unit;
    for (int steps = 0; steps < count; ++steps) {
        at = save.units[static_cast<std::size_t>(at)].nextInTransport;
        if (at < 0 || at >= count) {
            return -1;
        }
        if (isShipType(rules, save.units[static_cast<std::size_t>(at)].type)) {
            return at;
        }
    }
    return -1;
}

int bidPrice(const SaveNation& nation, int good) {
    if (good < 0 || good >= kGoodsKinds) {
        return 0;
    }
    return std::max(0, nation.prices[static_cast<std::size_t>(good)] - 1);
}

int askPrice(const Ruleset& rules, const SaveNation& nation, int good) {
    if (good < 0 || good >= kGoodsKinds) {
        return 0;
    }
    const int spread =
        static_cast<std::size_t>(good) < rules.cargo.size() ? rules.cargo[static_cast<std::size_t>(good)].burden : 0;
    return nation.prices[static_cast<std::size_t>(good)] + spread;
}

int holdGood(const SaveUnit& ship, int hold) {
    if (hold < 0 || hold >= kMaxHolds) {
        return -1;
    }
    const auto byte = ship.raw[13 + static_cast<std::size_t>(hold / 2)];
    return hold % 2 == 0 ? (byte & 0x0F) : (byte >> 4);
}

int holdAmount(const SaveUnit& ship, int hold) {
    return hold >= 0 && hold < kMaxHolds ? ship.raw[16 + static_cast<std::size_t>(hold)] : 0;
}

void setHold(SaveUnit& ship, int hold, int good, int amount) {
    if (hold < 0 || hold >= kMaxHolds) {
        return;
    }
    auto& byte = ship.raw[13 + static_cast<std::size_t>(hold / 2)];
    const auto nibble = static_cast<std::uint8_t>(good & 0x0F);
    byte = hold % 2 == 0 ? static_cast<std::uint8_t>((byte & 0xF0) | nibble)
                         : static_cast<std::uint8_t>((byte & 0x0F) | (nibble << 4));
    ship.raw[16 + static_cast<std::size_t>(hold)] = static_cast<std::uint8_t>(std::clamp(amount, 0, 255));
}

void removeHold(SaveUnit& ship, int hold) {
    if (hold < 0 || hold >= ship.holds) {
        return;
    }
    for (int k = hold; k + 1 < ship.holds; ++k) {
        setHold(ship, k, holdGood(ship, k + 1), holdAmount(ship, k + 1));
    }
    setHold(ship, ship.holds - 1, 0, 0);
    --ship.holds;
}

Trade buyGoods(SaveGame& save, const Ruleset& rules, int ship, int good, int freeHolds) {
    Trade trade;
    auto& s = save.units[static_cast<std::size_t>(ship)];
    if (good < 0 || good >= kGoodsKinds || europePlace(s) != EuropePlace::InPort || s.nation < 0 || s.nation > 3) {
        return trade;
    }
    auto& nation = save.nations[static_cast<std::size_t>(s.nation)];
    const int price = askPrice(rules, nation, good);
    int hold = -1;
    for (int k = 0; k < s.holds; ++k) {
        if (holdGood(s, k) == good && holdAmount(s, k) < kHoldSize) {
            hold = k;
            break;
        }
    }
    if (hold < 0) {
        if (freeHolds <= 0 || s.holds >= kMaxHolds) {
            return trade;
        }
        hold = s.holds;
        setHold(s, hold, good, 0);
        ++s.holds;
    }
    int amount = kHoldSize - holdAmount(s, hold);
    if (price > 0) {
        amount = std::min(amount, nation.gold / price);
    }
    if (amount <= 0) {
        if (holdAmount(s, hold) == 0) {
            removeHold(s, hold);
        }
        return trade;
    }
    setHold(s, hold, good, holdAmount(s, hold) + amount);
    trade.amount = amount;
    trade.gold = amount * price;
    nation.gold -= trade.gold;
    return trade;
}

Trade sellGoods(SaveGame& save, int ship, int good) {
    Trade trade;
    auto& s = save.units[static_cast<std::size_t>(ship)];
    if (europePlace(s) != EuropePlace::InPort || s.nation < 0 || s.nation > 3) {
        return trade;
    }
    auto& nation = save.nations[static_cast<std::size_t>(s.nation)];
    for (int k = s.holds - 1; k >= 0; --k) {
        if (holdGood(s, k) == good) {
            trade.amount += holdAmount(s, k);
            removeHold(s, k);
        }
    }
    const int gross = trade.amount * bidPrice(nation, good);
    trade.tax = gross * nation.taxRate / 100;
    trade.gold = gross - trade.tax;
    nation.gold += trade.gold;
    return trade;
}

} // namespace viceroy
