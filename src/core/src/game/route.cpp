// SPDX-License-Identifier: GPL-3.0-or-later
//
// The route finder: which way a unit with a destination steps next. Read
// from the original with a debugger; see docs/rules/routes.md. It works in
// three layers: a cost flood over the 15 x 15 squares around a nearby goal,
// a breadth-first search over the 4 x 4 blocks of the map (whose links the
// save keeps after the map), and, when both fail, a greedy step for the
// Europeans.
#include "viceroy/game/colony.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/resource.hpp"

#include <algorithm>
#include <cstdlib>

namespace viceroy {

namespace {

// The original's directions: north, then clockwise; the ninth is staying.
constexpr std::array<int, 9> kDx{0, 1, 1, 1, 0, -1, -1, -1, 0};
constexpr std::array<int, 9> kDy{-1, -1, 0, 1, 1, 1, 0, -1, 0};

constexpr int kFirstShip = 13;
constexpr int kLastShip = 18;
constexpr int kFirstNative = 19; // braves and the like skip rumours
constexpr int kSoldier = 1;
constexpr int kCaravel = 13;

// In each unit's record.
constexpr std::size_t kDestX = 9;
constexpr std::size_t kDestY = 10;
constexpr std::size_t kLastStep = 11;
constexpr std::size_t kSeventh = 7; // a byte the route finder tests for 0x39
constexpr int kAiGoTo = 0x0B;       // the computer players' Go To

// The block links after the map: one byte per 4 x 4 block, bit d set
// when the block is joined to its neighbour in direction d.
constexpr std::size_t kSeaBlocks = 0;
constexpr std::size_t kLandBlocks = 270;
constexpr int kBlocksAcross = 15;
constexpr int kBlocksDown = 18;

constexpr std::uint8_t kRoadOrTown = MapSquare::kRoad | MapSquare::kHasColony;
constexpr std::uint8_t kWorked = MapSquare::kRoad | MapSquare::kPlowed;

bool shipType(int type) {
    return type >= kFirstShip && type <= kLastShip;
}

// The original's distance: the longer side plus half the shorter.
int distance(int dx, int dy) {
    dx = std::abs(dx);
    dy = std::abs(dy);
    return std::max(dx, dy) + std::min(dx, dy) / 2;
}

// The direction of a step of at most one square each way, 8 for none.
int directionOf(int dx, int dy) {
    const int sx = dx > 0 ? 1 : dx < 0 ? -1 : 0;
    const int sy = dy > 0 ? 1 : dy < 0 ? -1 : 0;
    int found = 8;
    for (int d = 0; d < 8; ++d) {
        if (kDx[static_cast<std::size_t>(d)] == sx && kDy[static_cast<std::size_t>(d)] == sy) {
            found = d;
        }
    }
    return found;
}

// The route finder's own idea of the map's inside: one square in from
// every edge.
bool within(const SaveMap& map, int x, int y) {
    return x >= 1 && y >= 1 && x < map.width - 1 && y < map.height - 1;
}

int visitor(const SaveMap& map, int x, int y) {
    const int who = map.at(x, y).region >> 4;
    return who == 0x0F ? -1 : who;
}

// Whose colony or settlement stands on the square, else whose units.
int occupant(const SaveMap& map, int x, int y) {
    if (!map.inside(x, y)) {
        return -1;
    }
    const auto flags = map.at(x, y).flags;
    if ((flags & MapSquare::kHasColony) != 0 && visitor(map, x, y) >= 0) {
        return visitor(map, x, y);
    }
    return (flags & MapSquare::kHasUnit) != 0 ? visitor(map, x, y) : -1;
}

// The European nation whose colony stands on the square.
int colonyOwner(const SaveMap& map, int x, int y) {
    if (!map.inside(x, y) || (map.at(x, y).flags & MapSquare::kHasColony) == 0) {
        return -1;
    }
    const int who = visitor(map, x, y);
    return who < kEuropeanNations ? who : -1;
}

bool river(const MapSquare& sq) {
    return (sq.relief & 2) != 0;
}

bool mainBody(const MapSquare& sq) {
    return (sq.region & 0x0F) == 1;
}

} // namespace

std::uint8_t Game::routeBlockLinks(bool ship, int bx, int by) const {
    if (bx < 0 || by < 0 || bx >= kBlocksAcross || by >= kBlocksDown) {
        return 0;
    }
    const std::size_t at = (ship ? kSeaBlocks : kLandBlocks) + static_cast<std::size_t>(bx * kBlocksDown + by);
    return at < save_.tail.size() ? save_.tail[at] : 0;
}

int Game::routeEnemyLand(int x, int y, int nation) const {
    // Roads or fields of a European nation the mover has flagged (bit
    // 0x40 of its relation to them).
    const auto& map = save_.map;
    if (!within(map, x, y) || (map.at(x, y).flags & kWorked) == 0) {
        return -1;
    }
    const int who = visitor(map, x, y);
    if (who < 0 || who >= kEuropeanNations || who == nation || nation < 0 || nation >= kEuropeanNations) {
        return -1;
    }
    const auto& raw = save_.nations[static_cast<std::size_t>(nation)].raw;
    return (raw[52 + static_cast<std::size_t>(who)] & 0x40) != 0 ? who : -1;
}

int Game::routeTerrainCost(int x, int y) const {
    const auto row = static_cast<std::size_t>(terrainRow(save_.map.at(x, y)));
    return row < rules_.terrain.size() ? rules_.terrain[row].movementCost : 1;
}

bool Game::routeOneMove() const {
    const auto t = static_cast<std::size_t>(route_.type);
    return t >= rules_.units.size() || rules_.units[t].movement * 3 <= 3;
}

int Game::routeFlood(int sx, int sy, int limit) {
    auto& r = route_;
    const auto& map = save_.map;
    const int tx = r.targetX;
    const int ty = r.targetY;
    const int wx = tx - 8;
    const int wy = ty - 8;
    const bool ship = shipType(r.type);
    const bool oneMove = routeOneMove();
    const auto inWindow = [&](int x, int y) { return std::abs(x - tx) < 8 && std::abs(y - ty) < 8; };
    const auto cell = [&](int x, int y) -> std::uint8_t& {
        return r.grid[static_cast<std::size_t>((x - wx) * 16 + (y - wy))];
    };
    // A square of the other kind (land for a ship, water for the rest, a
    // lake for a ship) is entered only where it is a colony and the end of
    // the route.
    const auto passable = [&](int x, int y) {
        const auto& sq = map.at(x, y);
        if (sq.isWater() == ship && (!ship || mainBody(sq))) {
            return true;
        }
        return colonyOwner(map, x, y) >= 0 && ((x == sx && y == sy) || (x == r.keyX && y == r.keyY));
    };
    r.best = 0;
    const bool reuse = tx == r.keyX && ty == r.keyY && inWindow(sx, sy) && cell(sx, sy) != 0;
    if (!reuse) {
        // Costs from the goal outwards, in thirds of a move, 1 at the goal.
        r.keyX = tx;
        r.keyY = ty;
        r.grid.fill(0);
        std::array<std::uint8_t, 256> qx{};
        std::array<std::uint8_t, 256> qy{};
        int head = 0;
        std::uint8_t tail = 1;
        qx[0] = static_cast<std::uint8_t>(tx);
        qy[0] = static_cast<std::uint8_t>(ty);
        cell(tx, ty) = 1;
        r.best = limit;
        do {
            const int cx = qx[static_cast<std::size_t>(head)];
            const int cy = qy[static_cast<std::size_t>(head)];
            ++head;
            const int c = cell(cx, cy);
            if (c > r.best) {
                continue;
            }
            if (cx == sx && cy == sy) {
                r.best = c;
                continue;
            }
            const bool roadHere = (map.at(cx, cy).flags & kRoadOrTown) != 0;
            const bool riverHere = river(map.at(cx, cy));
            for (int d = 0; d < 8; ++d) {
                const int nx = cx + kDx[static_cast<std::size_t>(d)];
                const int ny = cy + kDy[static_cast<std::size_t>(d)];
                if (!inWindow(nx, ny) || !within(map, nx, ny) || !passable(nx, ny)) {
                    continue;
                }
                if (r.type >= kFirstNative && rumourAt(map, nx, ny)) {
                    continue;
                }
                int cost = c;
                if (r.nation >= 0) {
                    const int who = occupant(map, nx, ny);
                    if (who >= 0 && who != r.nation) {
                        continue;
                    }
                    if (routeEnemyLand(nx, ny, r.nation) >= 0) {
                        if (r.nation >= kEuropeanNations ||
                            save_.players[static_cast<std::size_t>(r.nation)].control != 0) {
                            continue;
                        }
                        cost += 8;
                    }
                }
                const auto& to = map.at(nx, ny);
                if ((roadHere && (to.flags & kRoadOrTown) != 0) || r.roads) {
                    cost += 1;
                } else if (riverHere && river(to) && (nx == cx || ny == cy)) {
                    cost += 1;
                } else if (oneMove) {
                    cost += 3;
                } else {
                    cost += 3 * routeTerrainCost(nx, ny);
                }
                auto& g = cell(nx, ny);
                if (g != 0 && g <= cost) {
                    continue;
                }
                g = static_cast<std::uint8_t>(cost);
                qx[tail] = static_cast<std::uint8_t>(nx);
                qy[tail] = static_cast<std::uint8_t>(ny);
                ++tail;
            }
        } while (head != tail && head < 225);
    }
    if (r.best >= limit) {
        return -1;
    }
    // Down the costs: the cheapest neighbour, the one nearer the goal of
    // equals.
    int bestCost = 99;
    int bestDistance = 99;
    int found = -1;
    const bool roadHere = (map.at(sx, sy).flags & kRoadOrTown) != 0;
    const bool riverHere = river(map.at(sx, sy));
    const int here = inWindow(sx, sy) ? cell(sx, sy) : 0;
    for (int d = 0; d < 8; ++d) {
        const int nx = sx + kDx[static_cast<std::size_t>(d)];
        const int ny = sy + kDy[static_cast<std::size_t>(d)];
        if (!inWindow(nx, ny) || !within(map, nx, ny) || !passable(nx, ny)) {
            continue;
        }
        if (r.type >= kFirstNative && rumourAt(map, nx, ny)) {
            continue;
        }
        int g = cell(nx, ny);
        if (r.nation >= 0) {
            const int who = occupant(map, nx, ny);
            if (who >= 0 && who != r.nation) {
                continue;
            }
            if (routeEnemyLand(nx, ny, r.nation) >= 0) {
                if (r.nation >= kEuropeanNations || save_.players[static_cast<std::size_t>(r.nation)].control != 0) {
                    continue;
                }
                g += 8;
            }
        }
        if (g == 0 || g >= here) {
            continue;
        }
        const auto& to = map.at(nx, ny);
        int step = 0;
        if (roadHere && (to.flags & kRoadOrTown) != 0) {
            step = 1;
        } else if (riverHere && river(to) && (nx == sx || ny == sy)) {
            step = 1;
        } else if (oneMove) {
            step = 3;
        } else {
            step = 3 * routeTerrainCost(nx, ny);
        }
        const int total = g + step;
        if (total > bestCost) {
            continue;
        }
        const int dist = distance(tx - nx, ty - ny);
        if (total < bestCost || dist < bestDistance) {
            bestCost = std::min(bestCost, total);
            found = d;
            bestDistance = dist;
        }
    }
    return found;
}

bool Game::routeBlockSquare(int x0, int y0, bool ship, int& x, int& y) const {
    // The first square of the two by two at (x0, y0) of the mover's kind.
    const auto& map = save_.map;
    for (int sx = x0; sx <= x0 + 1; ++sx) {
        for (int sy = y0; sy <= y0 + 1; ++sy) {
            if (!map.inside(sx, sy)) {
                continue;
            }
            const auto& sq = map.at(sx, sy);
            if (sq.isWater() == ship && (!ship || mainBody(sq))) {
                x = sx;
                y = sy;
                return true;
            }
        }
    }
    return false;
}

int Game::routeReach(int fromX, int fromY, int toX, int toY, bool ship, int limit) {
    // The cost of going from one square to another nearby, -1 when it is
    // more than the limit. Like the original it leaves the mover's type
    // as a caravel or a soldier afterwards.
    if (fromX == toX && fromY == toY) {
        return 0;
    }
    if (std::abs(fromX - toX) >= 8 || std::abs(fromY - toY) >= 8) {
        return -1;
    }
    route_.type = ship ? kCaravel : kSoldier;
    route_.targetX = toX;
    route_.targetY = toY;
    const int nation = route_.nation;
    route_.nation = -1;
    const int step = routeFlood(fromX, fromY, limit);
    route_.nation = nation;
    return step >= 0 ? route_.best : -1;
}

bool Game::routeBlock(int x, int y, bool ship) {
    // The block of (x, y), or the nearest joined neighbour, whose corner
    // square can be reached from it cheaply; into the target.
    constexpr int kNear = 18;
    const int bx = x >> 2;
    const int by = y >> 2;
    int found = -1;
    if (routeBlockLinks(ship, bx, by) != 0) {
        int cx = 0;
        int cy = 0;
        if (routeBlockSquare(bx * 4 + 1, by * 4 + 1, ship, cx, cy) && routeReach(cx, cy, x, y, ship, kNear) >= 0) {
            found = 8;
        }
    }
    if (found < 0) {
        int best = 99;
        for (int d = 0; d < 8; ++d) {
            const int nbx = bx + kDx[static_cast<std::size_t>(d)];
            const int nby = by + kDy[static_cast<std::size_t>(d)];
            if (nbx < 0 || nbx >= kBlocksAcross || nby < 0 || nby >= kBlocksDown ||
                routeBlockLinks(ship, nbx, nby) == 0) {
                continue;
            }
            int cx = nbx * 4 + 1;
            int cy = nby * 4 + 1;
            const int dist = distance(x - cx, y - cy);
            if (dist >= best || !routeBlockSquare(cx, cy, ship, cx, cy) || routeReach(cx, cy, x, y, ship, kNear) < 0) {
                continue;
            }
            best = dist;
            found = d;
        }
    }
    if (found >= 0) {
        route_.targetX = bx + kDx[static_cast<std::size_t>(found)];
        route_.targetY = by + kDy[static_cast<std::size_t>(found)];
    }
    return found >= 0;
}

bool Game::routeCoarse(int x0, int y0, int x1, int y1) {
    // Over the blocks: the neighbour block of the mover's own that is
    // fewest blocks from the goal's, nearer the goal of equals; its corner
    // square becomes the target. Returns whether the goal's block was found.
    auto& r = route_;
    const bool ship = shipType(r.type);
    r.targetX = x1;
    r.targetY = y1;
    if (!routeBlock(x0, y0, ship)) {
        r.targetX = x1;
        r.targetY = y1;
        return false;
    }
    const int sbx = r.targetX;
    const int sby = r.targetY;
    r.startBlockX = sbx;
    r.startBlockY = sby;
    routeBlock(x1, y1, ship); // when it fails the start's block stays
    const int tbx = r.targetX;
    const int tby = r.targetY;
    std::array<std::uint8_t, kBlocksAcross * kBlocksDown> steps{};
    const auto at = [](int bx, int by) { return static_cast<std::size_t>(bx * kBlocksDown + by); };
    std::array<std::uint8_t, 256> qx{};
    std::array<std::uint8_t, 256> qy{};
    std::uint8_t head = 0;
    std::uint8_t tail = 1;
    qx[0] = static_cast<std::uint8_t>(tbx);
    qy[0] = static_cast<std::uint8_t>(tby);
    steps[at(tbx, tby)] = 1;
    bool found = false;
    do {
        const int bx = qx[head];
        const int by = qy[head];
        ++head;
        if (bx == sbx && by == sby) {
            found = true;
            break;
        }
        const int v = steps[at(bx, by)];
        const int links = routeBlockLinks(ship, bx, by);
        for (int d = 0; d < 8; ++d) {
            if ((links & (1 << d)) == 0) {
                continue;
            }
            const int nbx = bx + kDx[static_cast<std::size_t>(d)];
            const int nby = by + kDy[static_cast<std::size_t>(d)];
            if (nbx < 0 || nbx >= kBlocksAcross || nby < 0 || nby >= kBlocksDown || steps[at(nbx, nby)] != 0) {
                continue;
            }
            steps[at(nbx, nby)] = static_cast<std::uint8_t>(v + 1);
            qx[tail] = static_cast<std::uint8_t>(nbx);
            qy[tail] = static_cast<std::uint8_t>(nby);
            ++tail;
        }
    } while (head != tail);
    bool waypoint = false;
    int wx = 0;
    int wy = 0;
    if (found) {
        int best = 99;
        int bestDistance = 0;
        int pick = -1;
        const int links = routeBlockLinks(ship, sbx, sby);
        for (int d = 0; d < 8; ++d) {
            if ((links & (1 << d)) == 0) {
                continue;
            }
            const int nbx = sbx + kDx[static_cast<std::size_t>(d)];
            const int nby = sby + kDy[static_cast<std::size_t>(d)];
            if (nbx < 0 || nbx >= kBlocksAcross || nby < 0 || nby >= kBlocksDown) {
                continue;
            }
            const int v = steps[at(nbx, nby)];
            if (v == 0 || v > best) {
                continue;
            }
            const int dist = distance(x1 - (nbx * 4 + 1), y1 - (nby * 4 + 1));
            if (v < best || dist < bestDistance) {
                best = v;
                pick = d;
                bestDistance = dist;
            }
        }
        if (pick >= 0) {
            wx = (sbx + kDx[static_cast<std::size_t>(pick)]) * 4 + 1;
            wy = (sby + kDy[static_cast<std::size_t>(pick)]) * 4 + 1;
            waypoint = true;
            routeBlockSquare(wx, wy, ship, wx, wy);
        }
    }
    r.targetX = waypoint ? wx : x1;
    r.targetY = waypoint ? wy : y1;
    return found;
}

int Game::routeStep(int index, int nation) {
    auto& r = route_;
    const auto& map = save_.map;
    const int x = unit(index).x;
    const int y = unit(index).y;
    const int tx = unit(index).raw[kDestX];
    const int ty = unit(index).raw[kDestY];
    const int own = unit(index).nation;
    const bool ship = shipType(unit(index).type);
    r.type = unit(index).type;
    r.nation = nation;
    const auto finish = [&](int step) {
        save_.units[static_cast<std::size_t>(index)].raw[kLastStep] = static_cast<std::uint8_t>(step);
        r.nation = -1;
        return step;
    };
    const auto endMoves = [&] {
        auto& u = save_.units[static_cast<std::size_t>(index)];
        u.movesUsed = movementAllowance(index);
    };
    if (!within(map, tx, ty) || (x == tx && y == ty)) {
        return finish(-1);
    }
    if (std::abs(tx - x) <= 1 && std::abs(ty - y) <= 1) {
        return finish(directionOf(tx - x, ty - y));
    }
    int step = -1;
    bool near = false;
    bool greedy = false;
    if (std::abs(tx - x) < 7 && std::abs(ty - y) < 7) {
        r.targetX = tx;
        r.targetY = ty;
        step = routeFlood(x, y, 999);
        if (step >= 0) {
            return finish(step);
        }
        near = true;
    }
    if (!routeCoarse(x, y, tx, ty) && near) {
        greedy = true;
    } else {
        step = routeFlood(x, y, 998);
        if (step < 0) {
            int cx = r.startBlockX * 4 + 1;
            int cy = r.startBlockY * 4 + 1;
            r.targetX = cx;
            r.targetY = cy;
            if (routeBlockSquare(cx, cy, ship, cx, cy)) {
                r.targetX = cx;
                r.targetY = cy;
            }
            step = routeFlood(x, y, 998);
        }
        greedy = step < 0;
        if (!greedy) {
            const auto& u = unit(index);
            const int last = static_cast<std::int8_t>(u.raw[kLastStep]);
            if (u.movesUsed != 0 && u.orders == kAiGoTo && u.raw[kSeventh] != 0x39 && last >= 0 && (last ^ 4) == step) {
                r.targetX = tx;
                r.targetY = ty;
                greedy = true;
            }
        }
    }
    if (!greedy || own >= kEuropeanNations) {
        return finish(step);
    }
    // The greedy step: closer to the target by the original's measure,
    // and cheap to enter. The player's units never step away from it.
    const int gdx = r.targetX - x;
    const int gdy = r.targetY - y;
    const int far = std::max(std::abs(gdx), std::abs(gdy)) + std::abs(gdx) + std::abs(gdy);
    if (gdx == 0 && gdy == 0) {
        save_.units[static_cast<std::size_t>(index)].orders = SaveUnit::kNoOrders;
        endMoves();
        return finish(step);
    }
    const bool human = own < kEuropeanNations && save_.players[static_cast<std::size_t>(own)].control == 0;
    const bool roadHere = (map.at(x, y).flags & kRoadOrTown) != 0;
    const bool riverHere = river(map.at(x, y));
    int best = 9999;
    step = 8;
    for (int d = 0; d < 8; ++d) {
        const int nx = x + kDx[static_cast<std::size_t>(d)];
        const int ny = y + kDy[static_cast<std::size_t>(d)];
        const int ex = std::abs(gdx - kDx[static_cast<std::size_t>(d)]);
        const int ey = std::abs(gdy - kDy[static_cast<std::size_t>(d)]);
        const int dn = std::max(ex, ey) + ex + ey;
        if ((human && dn > far) || !within(map, nx, ny)) {
            continue;
        }
        const auto& to = map.at(nx, ny);
        const int who = occupant(map, nx, ny);
        const bool open = !(who >= 0 && who != own) && to.isWater() == ship && (!ship || mainBody(to));
        if (!open && (colonyOwner(map, nx, ny) != own || nx != tx || ny != ty)) {
            continue;
        }
        int cost = 0;
        if (roadHere && (to.flags & kRoadOrTown) != 0) {
            cost = 1;
        } else if (riverHere && river(to) && (nx == x || ny == y)) {
            cost = 1;
        } else if (movementAllowance(index) <= 1) {
            cost = 3;
        } else {
            cost = 3 * routeTerrainCost(nx, ny);
        }
        const int total = cost + 4 * dn + ey + ex;
        if (total < best) {
            best = total;
            step = d;
        }
    }
    const auto& u = unit(index);
    const int last = static_cast<std::int8_t>(u.raw[kLastStep]);
    if (last >= 0 && (last ^ 4) == step) {
        // Not straight back: a computer player's Go To tries a few random
        // ways instead, the rest wait.
        step = -1;
        if (u.orders == kAiGoTo && u.raw[kSeventh] != 0x39) {
            for (int tries = 0; step < 0 && tries < 8; ++tries) {
                const int d = nextRandom(8);
                const int nx = x + kDx[static_cast<std::size_t>(d)];
                const int ny = y + kDy[static_cast<std::size_t>(d)];
                if (!within(map, nx, ny)) {
                    continue;
                }
                const int who = occupant(map, nx, ny);
                const auto& to = map.at(nx, ny);
                if ((who < 0 || who == own) && to.isWater() == ship && (!ship || mainBody(to))) {
                    step = d;
                }
            }
        }
        if (step < 0) {
            endMoves();
        }
    }
    if (step == 8) {
        endMoves();
        step = -1;
    }
    return finish(step);
}

} // namespace viceroy
