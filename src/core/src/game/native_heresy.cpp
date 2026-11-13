// SPDX-License-Identifier: GPL-3.0-or-later
//
// The European pressure on a native settlement, and a missionary
// denouncing another nation's mission. Read from the original with a
// debugger and checked against its answers; see docs/rules/natives.md.
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>

namespace viceroy {

namespace {

constexpr int kFrench = 1;
constexpr int kPocahontas = 16; // row of @FATHERS
constexpr int kFirstShip = 13;
constexpr int kLastShip = 18;
constexpr int kReach = 6; // colonies farther than this press on no one
// The squares within two of the settlement, as the original walks them.
constexpr std::array<std::array<int, 2>, 20> kRing{{{0, -1}, {1, 0},  {0, 1},   {-1, 0}, {-1, -1}, {1, -1},  {1, 1},
                                                    {-1, 1}, {0, -2}, {2, 0},   {0, 2},  {-2, 0},  {-1, -2}, {1, -2},
                                                    {-1, 2}, {1, 2},  {-2, -1}, {-2, 1}, {2, -1},  {2, 1}}};
// What a building weighs, and what the sum is divided by, for the player
// on each difficulty level; computer nations weigh 1 and 1.
constexpr std::array<std::array<int, 2>, 5> kBuildingWeight{{{1, 2}, {3, 4}, {1, 1}, {3, 2}, {2, 1}}};
constexpr int kExpertProfession = 3; // what the original compares (see the docs)

int distance(int x0, int y0, int x1, int y1) {
    const int dx = std::abs(x0 - x1);
    const int dy = std::abs(y0 - y1);
    return std::max(dx, dy) + std::min(dx, dy) / 2;
}

} // namespace

int Game::settlementPressure(int settlement, int* pressure) const {
    const auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    const auto& map = save_.map;
    const int region = landRegion(map, s.x, s.y);
    const int level = tribeLevel(save_, tribeOf(s));
    // Armed units around the settlement, by nation: their attack values,
    // halved in a colony and halved again two squares out.
    std::array<int, kEuropeanNations> armed{};
    for (const auto& [dx, dy] : kRing) {
        const int x = s.x + dx;
        const int y = s.y + dy;
        if (!map.inside(x, y) || map.at(x, y).isWater()) {
            continue;
        }
        const auto here = unitsAt(x, y);
        if (here.empty()) {
            continue;
        }
        const int owner = unit(here.front()).nation;
        if (owner < 0 || owner >= kEuropeanNations) {
            continue;
        }
        int sum = 0;
        for (const int i : here) {
            const int type = unit(i).type;
            if (type >= kFirstShip && type <= kLastShip) {
                continue;
            }
            const auto& types = rules().units;
            const int attack = type >= 0 && static_cast<std::size_t>(type) < types.size()
                                   ? types[static_cast<std::size_t>(type)].attack
                                   : 0;
            if (attack > 1) {
                sum += attack;
            }
        }
        if (colonyAt(x, y)) {
            sum >>= 1;
        }
        if (std::abs(dx) > 1 || std::abs(dy) > 1) {
            sum >>= 1;
        }
        armed[static_cast<std::size_t>(owner)] += sum;
    }
    int best = 0;
    int bestNation = -1;
    for (const auto& c : save_.colonies) {
        const int d = distance(s.x, s.y, c.x, c.y);
        if (d > kReach) {
            continue;
        }
        const int owner = c.nation;
        int term = 0;
        int weight = 1;
        int divisor = 1;
        if (owner == human()) {
            term = save_.difficulty;
            const auto& w = kBuildingWeight[static_cast<std::size_t>(std::clamp(save_.difficulty, 0, 4))];
            weight = w[0];
            divisor = w[1];
        }
        int buildings = 0;
        for (std::size_t b = 132; b < 138; ++b) {
            buildings += std::popcount(c.raw[b]) * weight;
        }
        buildings /= divisor;
        term += (buildings - 8) >> 2;
        const int people = c.raw[31];
        const int first = std::min(people, 6);
        const int beyond = people - first;
        int score = (beyond * 2 + std::min(people >> 1, level) + first + term) * 2 - d - 1;
        score /= d + 4;
        if (landRegion(map, c.x, c.y) != region) {
            score >>= 1;
        }
        if (owner >= 0 && owner < kEuropeanNations) {
            score += armed[static_cast<std::size_t>(owner)];
        }
        if (owner == kFrench) {
            score >>= 1;
        }
        if (hasFather(owner, kPocahontas)) {
            score >>= 1;
        }
        if (best < score) {
            best = score;
            bestNation = owner;
        }
    }
    if (best > 0) {
        const auto mission = static_cast<std::int8_t>(s.raw[5]);
        if (mission >= 0) {
            const bool expert = (mission & kExpertMission) != 0;
            if ((mission & 0x0F) != bestNation) {
                best = expert ? best * 2 : best + best / 2;
            } else {
                best = expert ? best >> 1 : best - (best >> 2);
            }
        }
    }
    if (pressure != nullptr) {
        *pressure = best;
    }
    return bestNation;
}

bool Game::canDenounce(int index, int settlement) const {
    const auto& u = unit(index);
    const auto& s = save_.settlements[static_cast<std::size_t>(settlement)];
    const int mission = missionOf(s);
    return u.type == unit_type::kMissionary && u.nation < kEuropeanNations && metTribe(save_, u.nation, tribeOf(s)) &&
           mission >= 0 && mission != u.nation;
}

bool Game::denounceHeresy(int index, int settlement) {
    auto& here = save_.settlements[static_cast<std::size_t>(settlement)];
    const int us = unit(index).nation;
    const int them = missionOf(here);
    const int tribe = tribeOf(here);
    // Each side's weight: the pressure on each of the tribe's settlements
    // (counted for the nation pressing hardest), and its missions.
    int ours = 0;
    int theirs = 0;
    for (int i = 0; i < static_cast<int>(save_.settlements.size()); ++i) {
        const auto& s = save_.settlements[static_cast<std::size_t>(i)];
        if (s.nation != here.nation) {
            continue;
        }
        int pressure = 0;
        const int nation = settlementPressure(i, &pressure);
        int other = 0;
        if (nation == them) {
            ours += pressure;
        } else if (nation == us) {
            theirs += pressure;
        } else {
            other += pressure;
        }
        if (static_cast<std::int8_t>(s.raw[5]) >= 0) {
            other += s.population;
            if ((s.raw[5] & kExpertMission) != 0) {
                other <<= 1;
            }
            if ((s.raw[3] & kSettlementCapital) != 0) {
                other <<= 1;
            }
            // Read from the original: it weighs every mission for the
            // denouncer (its test compares the two nations, not the
            // mission's).
            ours += other;
        }
    }
    const bool expert = unit(index).profession == kExpertProfession;
    const bool expertThere = (here.raw[5] & kExpertMission) != 0;
    const bool capital = (here.raw[3] & kSettlementCapital) != 0;
    ours += tribeTension(save_, tribe, them) << (capital ? 4 : 0);
    // A capital shifts the other way by 29 (the original's 1 - 4 as a
    // shift count): nothing is left.
    theirs += capital ? 0 : tribeTension(save_, tribe, us) >> 1;
    int angerUs = tensionLevel(theirs) + 1;
    int angerThem = tensionLevel(ours) + 1;
    if (capital) {
        ours += 1 + nextRandom(20);
        theirs += 1 + nextRandom(20);
        angerThem <<= 1;
        angerUs <<= 1;
    }
    if (expert) {
        ours <<= 1;
        angerThem <<= 1;
    }
    if (expertThere) {
        theirs <<= 1;
        angerUs <<= 1;
    }
    const auto& tribes = rules().tribes;
    const auto t = static_cast<std::size_t>(tribe);
    const bool won = 1 + nextRandom(ours + theirs) <= ours;
    GameNotice notice{won ? "HERESY0" : "HERESY1", {"", "", t < tribes.size() ? tribes[t].adjective : ""}};
    notice.adjectives = {{0, us}, {1, them}};
    notices_.push_back(std::move(notice));
    if (won) {
        here.raw[5] = static_cast<std::uint8_t>(us | (expert ? kExpertMission : 0));
        angerUs = -angerUs;
    } else {
        angerThem = -angerThem;
    }
    removeUnit(index);
    changeTension(tribe, them, angerThem);
    changeTension(tribe, us, angerUs);
    return won;
}

} // namespace viceroy
