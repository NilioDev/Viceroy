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

} // namespace viceroy
