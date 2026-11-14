// SPDX-License-Identifier: GPL-3.0-or-later
//
// Trade routes: the player names a route of up to four stops, with the
// goods to unload and load at each, and sets ships or wagon trains to
// follow it (orders 2). Seen in the original and read from it with a
// debugger; see docs/rules/trade-routes.md.
#include "viceroy/game/europe.hpp"
#include "viceroy/game/game.hpp"

#include <algorithm>

namespace viceroy {

namespace {

// Where the routes are kept: their number is a word in the header, the
// routes themselves are the last 12 × 74 bytes of the save.
constexpr std::size_t kRouteCountAt = 0x30;
constexpr std::size_t kRouteSize = 74;
constexpr std::size_t kRoutesSize = Game::kMaxTradeRoutes * kRouteSize;
constexpr std::size_t kNameBytes = 32;
constexpr std::size_t kTypeAt = 32; // 1 sea, 0 land
constexpr std::size_t kStopCountAt = 33;
constexpr std::size_t kStopsAt = 34;
constexpr std::size_t kStopSize = 10;
// In a stop: the colony (a word), the counts (unload in the low nibble,
// load in the high one), then the goods to load and to unload, a nibble
// each, the low nibble first.
constexpr std::size_t kStopCountsAt = 2;
constexpr std::size_t kStopLoadAt = 3;
constexpr std::size_t kStopUnloadAt = 6;
// A carrier's route (low nibble) and the stop it heads for (high nibble).
constexpr std::size_t kUnitRouteAt = 23;
constexpr std::size_t kDestX = 9;
constexpr std::size_t kDestY = 10;
constexpr int kSeaLane = 26;

std::size_t routesStart(const Bytes& tail) { return tail.size() >= kRoutesSize ? tail.size() - kRoutesSize : 0; }

int nibble(const std::uint8_t* at, int k) { return (at[k / 2] >> ((k % 2) * 4)) & 0x0F; }

void setNibble(std::uint8_t* at, int k, int v) {
    const int shift = (k % 2) * 4;
    at[k / 2] = static_cast<std::uint8_t>((at[k / 2] & ~(0x0F << shift)) | ((v & 0x0F) << shift));
}

} // namespace

int Game::tradeRouteCount() const {
    if (save_.tail.size() < kRoutesSize) {
        return 0;
    }
    const int n = save_.header[kRouteCountAt] | (save_.header[kRouteCountAt + 1] << 8);
    return std::clamp(n, 0, kMaxTradeRoutes);
}

Game::TradeRoute Game::tradeRoute(int route) const {
    TradeRoute r;
    if (route < 0 || route >= tradeRouteCount()) {
        return r;
    }
    const std::uint8_t* at = save_.tail.data() + routesStart(save_.tail) + static_cast<std::size_t>(route) * kRouteSize;
    for (std::size_t k = 0; k < kNameBytes && at[k] != 0; ++k) {
        r.name.push_back(static_cast<char>(at[k]));
    }
    r.sea = at[kTypeAt] != 0;
    const int stops = std::min<int>(at[kStopCountAt], kMaxRouteStops);
    for (int s = 0; s < stops; ++s) {
        const std::uint8_t* stop = at + kStopsAt + static_cast<std::size_t>(s) * kStopSize;
        TradeStop t;
        t.colony = stop[0] | (stop[1] << 8);
        const int unload = std::min(stop[kStopCountsAt] & 0x0F, kMaxStopGoods);
        const int load = std::min(stop[kStopCountsAt] >> 4, kMaxStopGoods);
        for (int k = 0; k < unload; ++k) {
            t.unload.push_back(nibble(stop + kStopUnloadAt, k));
        }
        for (int k = 0; k < load; ++k) {
            t.load.push_back(nibble(stop + kStopLoadAt, k));
        }
        r.stops.push_back(t);
    }
    return r;
}

void Game::setTradeRoute(int route, const TradeRoute& r) {
    if (route < 0 || route >= kMaxTradeRoutes) {
        return;
    }
    if (save_.tail.size() < kRoutesSize) {
        save_.tail.resize(kRoutesSize); // a save made up without the block
    }
    std::uint8_t* at = save_.tail.data() + routesStart(save_.tail) + static_cast<std::size_t>(route) * kRouteSize;
    std::fill(at, at + kNameBytes, std::uint8_t{0});
    for (std::size_t k = 0; k < r.name.size() && k < kRouteNameSize; ++k) {
        at[k] = static_cast<std::uint8_t>(r.name[k]);
    }
    at[kTypeAt] = r.sea ? 1 : 0;
    const int stops = std::min<int>(static_cast<int>(r.stops.size()), kMaxRouteStops);
    at[kStopCountAt] = static_cast<std::uint8_t>(stops);
    for (int s = 0; s < stops; ++s) {
        const auto& t = r.stops[static_cast<std::size_t>(s)];
        std::uint8_t* stop = at + kStopsAt + static_cast<std::size_t>(s) * kStopSize;
        std::fill(stop, stop + kStopSize, std::uint8_t{0});
        stop[0] = static_cast<std::uint8_t>(t.colony & 0xFF);
        stop[1] = static_cast<std::uint8_t>(t.colony >> 8);
        const int unload = std::min<int>(static_cast<int>(t.unload.size()), kMaxStopGoods);
        const int load = std::min<int>(static_cast<int>(t.load.size()), kMaxStopGoods);
        stop[kStopCountsAt] = static_cast<std::uint8_t>(unload | (load << 4));
        for (int k = 0; k < unload; ++k) {
            setNibble(stop + kStopUnloadAt, k, t.unload[static_cast<std::size_t>(k)]);
        }
        for (int k = 0; k < load; ++k) {
            setNibble(stop + kStopLoadAt, k, t.load[static_cast<std::size_t>(k)]);
        }
    }
}

int Game::addTradeRoute(const TradeRoute& r) {
    const int n = tradeRouteCount();
    if (n >= kMaxTradeRoutes) {
        return -1;
    }
    setTradeRoute(n, r);
    save_.header[kRouteCountAt] = static_cast<std::uint8_t>(n + 1);
    save_.header[kRouteCountAt + 1] = 0;
    return n;
}

void Game::deleteTradeRoute(int route) {
    const int n = tradeRouteCount();
    if (route < 0 || route >= n) {
        return;
    }
    // Read from the original: the player's carriers on later routes follow
    // their route's new number; those on this one go back to route 0, stop
    // 0, and stop following it.
    for (int i = 0; i < unitCount(); ++i) {
        auto& u = save_.units[static_cast<std::size_t>(i)];
        const int type = u.type;
        if (u.nation != human() || type < 0 || static_cast<std::size_t>(type) >= rules_.units.size() ||
            rules_.units[static_cast<std::size_t>(type)].cargoSlots == 0) {
            continue;
        }
        const int r = unitTradeRoute(i);
        if (r > route) {
            u.raw[kUnitRouteAt] = static_cast<std::uint8_t>((u.raw[kUnitRouteAt] & 0xF0) | (r - 1));
        } else if (r == route) {
            u.raw[kUnitRouteAt] = 0;
            if (u.orders == kTradeRouteOrders) {
                u.orders = SaveUnit::kNoOrders;
            }
        }
    }
    std::uint8_t* first = save_.tail.data() + routesStart(save_.tail);
    std::copy(first + static_cast<std::size_t>(route + 1) * kRouteSize,
              first + static_cast<std::size_t>(n) * kRouteSize, first + static_cast<std::size_t>(route) * kRouteSize);
    save_.header[kRouteCountAt] = static_cast<std::uint8_t>(n - 1);
    save_.header[kRouteCountAt + 1] = 0;
}

int Game::unitTradeRoute(int index) const { return unit(index).raw[kUnitRouteAt] & 0x0F; }

int Game::unitTradeStop(int index) const { return unit(index).raw[kUnitRouteAt] >> 4; }

void Game::beginTradeRoute(int index, int route, int stop) {
    auto& u = save_.units[static_cast<std::size_t>(index)];
    u.raw[kUnitRouteAt] = static_cast<std::uint8_t>((route & 0x0F) | ((stop & 0x0F) << 4));
    u.orders = kTradeRouteOrders;
}

bool Game::tradeRouteStep(int index) {
    auto& u = save_.units[static_cast<std::size_t>(index)];
    if (u.orders != kTradeRouteOrders) {
        return false;
    }
    const int routeIndex = unitTradeRoute(index);
    const TradeRoute route = tradeRoute(routeIndex);
    if (route.stops.empty()) {
        u.orders = SaveUnit::kNoOrders;
        return false;
    }
    int stop = unitTradeStop(index) % static_cast<int>(route.stops.size());
    const TradeStop& here = route.stops[static_cast<std::size_t>(stop)];
    const bool ship = isShip(index);
    const bool inEurope = ship && europePlace(u) == EuropePlace::InPort;
    const bool europe = here.colony == kEuropeStop;
    const int colony = europe ? -1 : here.colony;
    if (!europe && (colony < 0 || static_cast<std::size_t>(colony) >= save_.colonies.size())) {
        u.orders = SaveUnit::kNoOrders;
        return false;
    }
    bool arrived = europe ? inEurope
                          : onMap(index) && u.x == save_.colonies[static_cast<std::size_t>(colony)].x &&
                                u.y == save_.colonies[static_cast<std::size_t>(colony)].y;
    if (!arrived) {
        if (inEurope) {
            // In port with a colony to go to: it sets sail.
            sailToNewWorld(index);
            return false;
        }
        if (!onMap(index)) {
            return false; // at sea between Europe and the New World
        }
        int x = 0;
        int y = 0;
        if (europe) {
            if (!ship || !nearestSeaLane(index, x, y)) {
                u.orders = SaveUnit::kNoOrders;
                return false;
            }
            if (save_.map.at(u.x, u.y).terrain == kSeaLane) {
                sailToEurope(index);
                return false;
            }
        } else {
            x = save_.colonies[static_cast<std::size_t>(colony)].x;
            y = save_.colonies[static_cast<std::size_t>(colony)].y;
        }
        u.raw[kDestX] = static_cast<std::uint8_t>(x);
        u.raw[kDestY] = static_cast<std::uint8_t>(y);
        const int step = routeStep(index, u.nation);
        if (step < 0 || step >= 8) {
            save_.units[static_cast<std::size_t>(index)].orders = SaveUnit::kNoOrders;
            return false;
        }
        const auto result = move(index, kSteps[static_cast<std::size_t>(step)]);
        if (result == MoveResult::Lost) {
            return false;
        }
        if (result != MoveResult::Moved && result != MoveResult::HighSeas) {
            if (result != MoveResult::NoMovesLeft) {
                save_.units[static_cast<std::size_t>(index)].orders = SaveUnit::kNoOrders;
            }
            return false;
        }
        const auto& moved = unit(index);
        if (europe && save_.map.at(moved.x, moved.y).terrain == kSeaLane) {
            sailToEurope(index);
            return false;
        }
        return true;
    }

    // At the stop: unload, then load.
    if (europe) {
        for (const int good : here.unload) {
            if (!isBoycotted(u.nation, good)) {
                sellGoods(save_, rules_, index, good);
            }
        }
        for (const int good : here.load) {
            if (!isBoycotted(u.nation, good)) {
                buyGoods(save_, rules_, index, good, freeHolds(index));
            }
        }
    } else {
        for (const int good : here.unload) {
            while (colonyCargoHold(index, good) >= 0) {
                if (unloadInColony(index, colony, good, kHoldSize) <= 0) {
                    break;
                }
            }
        }
        // The goods to load, the most valuable stock first (the price in
        // Europe times the stock), a hold at a time until the holds are
        // full or nothing is left to load.
        const auto& nation = save_.nations[static_cast<std::size_t>(u.nation & 3)];
        const int slots = rules_.units[static_cast<std::size_t>(u.type)].cargoSlots;
        for (int guard = 0; guard < kMaxHolds * kGoodsKinds && unit(index).holds < slots; ++guard) {
            const auto& stock = save_.colonies[static_cast<std::size_t>(colony)].stock;
            int best = -1;
            int bestWeight = 0;
            for (const int good : here.load) {
                const int weight = bidPrice(nation, good) * stock[static_cast<std::size_t>(good)];
                if (weight > bestWeight || (weight == bestWeight && weight > 0 && good > best)) {
                    best = good;
                    bestWeight = weight;
                }
            }
            if (best < 0 || loadInColony(index, colony, best, kHoldSize) <= 0) {
                break;
            }
        }
    }
    stop = (stop + 1) % static_cast<int>(route.stops.size());
    auto& v = save_.units[static_cast<std::size_t>(index)];
    v.raw[kUnitRouteAt] = static_cast<std::uint8_t>((v.raw[kUnitRouteAt] & 0x0F) | (stop << 4));
    bool loop = true;
    for (const auto& s : route.stops) {
        loop = loop && s.colony == route.stops.front().colony;
    }
    if (loop || europe) {
        if (loop && v.nation == human()) {
            notices_.push_back(GameNotice{"ROUTELOOP", {route.name}});
        }
        v.movesUsed = movementAllowance(index);
        return false;
    }
    return movesLeft(index) > 0;
}

bool Game::colonyOnCoast(int colony) const {
    if (colony < 0 || static_cast<std::size_t>(colony) >= save_.colonies.size()) {
        return false;
    }
    const auto& c = save_.colonies[static_cast<std::size_t>(colony)];
    for (const auto& s : kSteps) {
        const int x = c.x + s.dx;
        const int y = c.y + s.dy;
        if (save_.map.inside(x, y) && save_.map.at(x, y).isWater()) {
            return true;
        }
    }
    return false;
}

void Game::moveTradeRouteUnits() {
    constexpr int kStepLimit = 40;
    for (int i = 0; i < unitCount(); ++i) {
        for (int steps = 0; steps < kStepLimit; ++steps) {
            if (i >= unitCount()) {
                break;
            }
            const auto& u = unit(i);
            if (u.nation != human() || u.orders != kTradeRouteOrders) {
                break;
            }
            if (onMap(i) && movesLeft(i) <= 0) {
                break;
            }
            const int before = unitCount();
            if (!tradeRouteStep(i) || unitCount() != before) {
                break;
            }
        }
    }
}

} // namespace viceroy
