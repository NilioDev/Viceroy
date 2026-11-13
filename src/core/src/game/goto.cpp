// SPDX-License-Identifier: GPL-3.0-or-later
//
// Go To: the player sends a unit to one of their colonies (or a ship to
// Europe) and it walks there on its own, a route step at a time. Seen in
// the original and read from it with a debugger; see docs/rules/routes.md.
#include "viceroy/game/game.hpp"

#include <algorithm>
#include <cstdlib>

namespace viceroy {

namespace {

constexpr std::size_t kDestX = 9;
constexpr std::size_t kDestY = 10;
constexpr int kSeaLane = 26;
constexpr int kStepLimit = 40; // a safety net: no unit has more steps in a turn

int region(const MapSquare& sq) {
    return sq.region & 0x0F;
}

} // namespace

std::vector<int> Game::goToColonies(int index) const {
    // Seen: the nation's own colonies on the unit's land (the same region
    // number), in the order of the colony list; for a ship, its colonies by
    // the same water.
    std::vector<int> out;
    if (index < 0 || index >= unitCount() || !onMap(index)) {
        return out;
    }
    const auto& u = unit(index);
    const auto& map = save_.map;
    const int here = region(map.at(u.x, u.y));
    const bool ship = isShip(index);
    for (std::size_t i = 0; i < save_.colonies.size(); ++i) {
        const auto& c = save_.colonies[i];
        if (c.nation != u.nation || !map.inside(c.x, c.y)) {
            continue;
        }
        bool reached = !ship && region(map.at(c.x, c.y)) == here;
        for (const auto& s : kSteps) {
            const int x = c.x + s.dx;
            const int y = c.y + s.dy;
            reached = reached || (ship && map.inside(x, y) && map.at(x, y).isWater() && region(map.at(x, y)) == here);
        }
        if (reached) {
            out.push_back(static_cast<int>(i));
        }
    }
    return out;
}

void Game::setGoTo(int index, int x, int y) {
    auto& u = save_.units[static_cast<std::size_t>(index)];
    u.orders = kGoToOrders;
    u.raw[kDestX] = static_cast<std::uint8_t>(x);
    u.raw[kDestY] = static_cast<std::uint8_t>(y);
}

bool Game::goToEurope(int index) {
    // Seen once: the ship is sent to the nearest sea lane (the first of
    // equals, row by row) and sails for Europe when it gets there, in the
    // same turn if its moves last.
    if (!isShip(index) || !onMap(index)) {
        return false;
    }
    const auto& u = unit(index);
    const auto& map = save_.map;
    if (map.at(u.x, u.y).terrain == kSeaLane) {
        return sailToEurope(index);
    }
    int best = -1;
    int bx = 0;
    int by = 0;
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            if (map.at(x, y).terrain != kSeaLane) {
                continue;
            }
            const int dx = std::abs(x - u.x);
            const int dy = std::abs(y - u.y);
            const int d = std::max(dx, dy) + std::min(dx, dy) / 2;
            if (best < 0 || d < best) {
                best = d;
                bx = x;
                by = y;
            }
        }
    }
    if (best < 0) {
        return false;
    }
    setGoTo(index, bx, by);
    return true;
}

bool Game::goToStep(int index) {
    // One step under Go To (read from the original): the route finder,
    // kept off other nations' units and towns, gives the way. With no way
    // the orders are dropped. On arrival the orders end; a ship that
    // arrives on a sea lane sails for Europe.
    auto& u = save_.units[static_cast<std::size_t>(index)];
    const int step = routeStep(index, u.nation);
    if (step < 0 || step >= 8) {
        u.orders = SaveUnit::kNoOrders;
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
    auto& moved = save_.units[static_cast<std::size_t>(index)];
    if (moved.x == moved.raw[kDestX] && moved.y == moved.raw[kDestY]) {
        moved.orders = SaveUnit::kNoOrders;
        if (isShip(index) && save_.map.at(moved.x, moved.y).terrain == kSeaLane) {
            sailToEurope(index);
        }
        return false;
    }
    return true;
}

void Game::moveGoToUnits() {
    // The player's units under Go To take their steps while they have
    // moves, in the order of the unit list.
    for (int i = 0; i < unitCount(); ++i) {
        for (int steps = 0; steps < kStepLimit; ++steps) {
            if (i >= unitCount()) {
                break;
            }
            const auto& u = unit(i);
            if (u.nation != human() || u.orders != kGoToOrders || !onMap(i) || movesLeft(i) <= 0) {
                break;
            }
            const int before = unitCount();
            if (!goToStep(i) || unitCount() != before) {
                break;
            }
        }
    }
}

} // namespace viceroy
