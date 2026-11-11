// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/game/game.hpp"

#include <algorithm>
#include <cstdlib>

namespace viceroy {

namespace {

constexpr int kThirds = 3;
constexpr int kTerrainArctic = 24;

bool isNative(int nation) {
    return nation >= kEuropeanNations;
}

} // namespace

Game::Game(SaveGame save, Ruleset rules) : save_(std::move(save)), rules_(std::move(rules)) {
    if (save_.activeUnit >= unitCount()) {
        save_.activeUnit = -1;
    }
}

// --- Units ---------------------------------------------------------------

bool Game::isShip(int index) const {
    const int type = unit(index).type;
    return type >= 0 && type < static_cast<int>(rules_.units.size()) &&
           rules_.units[static_cast<std::size_t>(type)].hull > 0;
}

bool Game::onMap(int index) const {
    const auto& u = unit(index);
    return save_.map.inside(u.x, u.y);
}

bool Game::isAboard(int index) const {
    return onMap(index) && !isShip(index) && save_.map.at(unit(index).x, unit(index).y).isWater();
}

int Game::movementAllowance(int index) const {
    const int type = unit(index).type;
    if (type < 0 || type >= static_cast<int>(rules_.units.size())) {
        return 0;
    }
    return rules_.units[static_cast<std::size_t>(type)].movement * kThirds;
}

int Game::movesLeft(int index) const {
    return std::max(0, movementAllowance(index) - unit(index).movesUsed);
}

std::string Game::unitName(int index) const {
    const int type = unit(index).type;
    if (type < 0 || type >= static_cast<int>(rules_.units.size())) {
        return "?";
    }
    return rules_.units[static_cast<std::size_t>(type)].name;
}

std::vector<int> Game::unitsAt(int x, int y) const {
    std::vector<int> ships;
    std::vector<int> others;
    for (int i = 0; i < unitCount(); ++i) {
        if (unit(i).x == x && unit(i).y == y && onMap(i)) {
            (isShip(i) ? ships : others).push_back(i);
        }
    }
    ships.insert(ships.end(), others.begin(), others.end());
    return ships;
}

std::optional<int> Game::colonyAt(int x, int y) const {
    for (std::size_t i = 0; i < save_.colonies.size(); ++i) {
        if (save_.colonies[i].x == x && save_.colonies[i].y == y) {
            return static_cast<int>(i);
        }
    }
    return std::nullopt;
}

std::optional<int> Game::settlementAt(int x, int y) const {
    for (std::size_t i = 0; i < save_.settlements.size(); ++i) {
        if (save_.settlements[i].x == x && save_.settlements[i].y == y) {
            return static_cast<int>(i);
        }
    }
    return std::nullopt;
}

int Game::moveCost(int index, int fromX, int fromY, int toX, int toY) const {
    const auto& map = save_.map;
    const auto& to = map.at(toX, toY);
    if (isShip(index) || to.isWater()) {
        return kThirds;
    }
    const auto& from = map.at(fromX, fromY);
    const bool roadFrom = (from.flags & (MapSquare::kRoad | MapSquare::kHasColony)) != 0;
    const bool roadTo = (to.flags & (MapSquare::kRoad | MapSquare::kHasColony)) != 0;
    // Roads, and rivers followed from square to square, cost a third.
    if ((roadFrom && roadTo) || (from.river() && to.river())) {
        return 1;
    }
    // Rows of the terrain tables: 0-7 open land, 8-15 forest, then arctic,
    // ocean, sea lane, mountains, hills.
    std::size_t row = 0;
    if (to.mountains()) {
        row = 19;
    } else if (to.hills()) {
        row = 20;
    } else if (to.terrain == kTerrainArctic) {
        row = 16;
    } else if (to.isForest()) {
        row = 8 + (to.terrain & 7u);
    } else {
        row = to.terrain & 7u;
    }
    const int cost = row < rules_.terrain.size() ? rules_.terrain[row].movementCost : 1;
    return std::max(1, cost) * kThirds;
}

MoveResult Game::move(int index, Step step) {
    auto& u = save_.units[static_cast<std::size_t>(index)];
    if (!onMap(index)) {
        return MoveResult::Blocked;
    }
    const int toX = u.x + step.dx;
    const int toY = u.y + step.dy;
    const auto& map = save_.map;
    if (!map.inside(toX, toY) || (step.dx == 0 && step.dy == 0)) {
        return MoveResult::Blocked;
    }
    if (movesLeft(index) <= 0) {
        return MoveResult::NoMovesLeft;
    }
    const auto& target = map.at(toX, toY);
    const auto colony = colonyAt(toX, toY);
    if (settlementAt(toX, toY) || (colony && save_.colonies[static_cast<std::size_t>(*colony)].nation != u.nation)) {
        return MoveResult::Occupied;
    }
    for (const int other : unitsAt(toX, toY)) {
        if (unit(other).nation != u.nation) {
            return MoveResult::Occupied;
        }
    }

    const bool ship = isShip(index);
    bool boarding = false;
    if (ship) {
        // Ships sail on water, and may enter their own coastal colonies.
        if (!target.isWater() && !colony) {
            return MoveResult::Blocked;
        }
    } else if (target.isWater()) {
        // A land unit may only step onto water by boarding a ship there
        // with a free hold.
        int carrier = -1;
        for (const int other : unitsAt(toX, toY)) {
            if (isShip(other)) {
                carrier = other;
                break;
            }
        }
        if (carrier < 0) {
            return MoveResult::Blocked;
        }
        const int slots = rules_.units[static_cast<std::size_t>(unit(carrier).type)].cargoSlots;
        int used = unit(carrier).holds;
        for (const int other : unitsAt(toX, toY)) {
            if (!isShip(other)) {
                ++used;
            }
        }
        if (used >= slots) {
            return MoveResult::Blocked;
        }
        boarding = true;
    }

    const int cost = moveCost(index, u.x, u.y, toX, toY);
    if (cost > movesLeft(index) && u.movesUsed > 0) {
        // Not enough moves for this square: the unit's turn is over. A unit
        // that has not moved yet may always make one move.
        u.movesUsed = movementAllowance(index);
        return MoveResult::NoMovesLeft;
    }

    // A ship takes the units aboard with it.
    if (ship) {
        for (const int other : unitsAt(u.x, u.y)) {
            if (other != index && !isShip(other) && map.at(u.x, u.y).isWater()) {
                save_.units[static_cast<std::size_t>(other)].x = toX;
                save_.units[static_cast<std::size_t>(other)].y = toY;
            }
        }
    }
    u.x = toX;
    u.y = toY;
    u.movesUsed = std::min(movementAllowance(index), u.movesUsed + cost);
    if (boarding) {
        u.orders = SaveUnit::kSentry;
        u.movesUsed = movementAllowance(index);
    }
    if (ship && colony) {
        // Units aboard go ashore in a colony.
        for (const int other : unitsAt(toX, toY)) {
            if (!isShip(other)) {
                save_.units[static_cast<std::size_t>(other)].orders = SaveUnit::kSentry;
            }
        }
    }
    reveal(toX, toY, sightRadius(index), u.nation);
    refreshUnitFlags();
    return boarding ? MoveResult::Boarded : MoveResult::Moved;
}

void Game::setActiveUnit(int index) {
    save_.activeUnit = index;
}

bool Game::needsOrders(int index) const {
    const auto& u = unit(index);
    return u.nation == human() && onMap(index) && u.orders == SaveUnit::kNoOrders && movesLeft(index) > 0;
}

std::vector<int> Game::passengers(int ship) const {
    std::vector<int> out;
    if (!isShip(ship) || !onMap(ship) || !save_.map.at(unit(ship).x, unit(ship).y).isWater()) {
        return out;
    }
    for (const int other : unitsAt(unit(ship).x, unit(ship).y)) {
        if (!isShip(other)) {
            out.push_back(other);
        }
    }
    return out;
}

int Game::nextUnitNeedingOrders(int after) const {
    const int count = unitCount();
    for (int step = 1; step <= count; ++step) {
        const int i = ((after < 0 ? -1 : after) + step + count) % count;
        if (needsOrders(i)) {
            return i;
        }
    }
    return -1;
}

void Game::skipUnit(int index) {
    auto& u = save_.units[static_cast<std::size_t>(index)];
    u.movesUsed = movementAllowance(index);
}

void Game::setOrders(int index, int orders) {
    save_.units[static_cast<std::size_t>(index)].orders = orders;
}

// --- Colonies --------------------------------------------------------------

FoundResult Game::canFoundColony(int index) const {
    const auto& u = unit(index);
    if (isShip(index) || u.type == unit_type::kTreasure || u.type == unit_type::kWagonTrain ||
        u.type >= unit_type::kFirstNative || u.type == 11 /* artillery */) {
        return FoundResult::NotAColonist;
    }
    if (!onMap(index) || isAboard(index)) {
        return FoundResult::NotOnLand;
    }
    const auto& sq = save_.map.at(u.x, u.y);
    if (sq.terrain == kTerrainArctic || !sq.isLand()) {
        return FoundResult::NotOnLand;
    }
    if (sq.mountains()) {
        return FoundResult::Mountains;
    }
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (colonyAt(u.x + dx, u.y + dy) || settlementAt(u.x + dx, u.y + dy)) {
                return FoundResult::TooClose;
            }
        }
    }
    return FoundResult::Founded;
}

FoundResult Game::foundColony(int index, const std::string& name) {
    const FoundResult check = canFoundColony(index);
    if (check != FoundResult::Founded) {
        return check;
    }
    const auto u = unit(index);
    SaveColony colony;
    colony.x = u.x;
    colony.y = u.y;
    colony.name = name.substr(0, 23);
    colony.nation = u.nation;
    colony.population = 1;
    // The founder works as a farmer; the eight squares around are free.
    colony.raw[32] = 0;                                     // occupation: farmer
    colony.raw[64] = static_cast<std::uint8_t>(u.profession); // their skill
    for (int i = 0; i < 8; ++i) {
        colony.raw[112 + static_cast<std::size_t>(i)] = 0xFF;
    }
    save_.colonies.push_back(colony);
    auto& sq = save_.map.at(u.x, u.y);
    sq.flags = static_cast<std::uint8_t>(sq.flags | MapSquare::kHasColony | MapSquare::kRoad);
    removeUnit(index);
    reveal(colony.x, colony.y, 2, colony.nation);
    refreshUnitFlags();
    return FoundResult::Founded;
}

std::string Game::suggestColonyName(const TextFile& colonyNames) const {
    // COLONY.TXT: a section per nation, one "name,year" per line.
    static constexpr const char* kSections[] = {"ENGLISH", "FRENCH", "SPANISH", "DUTCH"};
    const auto* section = colonyNames.find(kSections[human() & 3]);
    if (section != nullptr) {
        for (const auto& line : section->lines) {
            const auto fields = splitFields(line);
            if (fields.empty() || fields[0].empty()) {
                continue;
            }
            const std::string& name = fields[0];
            const bool used = std::any_of(save_.colonies.begin(), save_.colonies.end(),
                                          [&](const SaveColony& c) { return c.name == name; });
            if (!used) {
                return name;
            }
        }
    }
    return "Colony " + std::to_string(save_.colonies.size() + 1);
}

// --- Turns -------------------------------------------------------------------

void Game::endTurn() {
    for (int i = 0; i < unitCount(); ++i) {
        save_.units[static_cast<std::size_t>(i)].movesUsed = 0;
    }
    ++save_.turn;
    if (save_.year < 1600) {
        ++save_.year;
        save_.season = 0;
    } else if (save_.season == 0) {
        save_.season = 1;
    } else {
        save_.season = 0;
        ++save_.year;
    }
    save_.activeUnit = nextUnitNeedingOrders(-1);
}

// --- Map -----------------------------------------------------------------------

void Game::reveal(int x, int y, int radius, int nation) {
    if (isNative(nation) || nation < 0) {
        return;
    }
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (save_.map.inside(x + dx, y + dy)) {
                auto& sq = save_.map.at(x + dx, y + dy);
                sq.seen = static_cast<std::uint8_t>(sq.seen | (0x10u << nation));
            }
        }
    }
}

int Game::sightRadius(int index) const {
    const int type = unit(index).type;
    return (type == unit_type::kScout || type == unit_type::kDragoon) ? 2 : 1;
}

void Game::refreshUnitFlags() {
    for (auto& sq : save_.map.squares) {
        sq.flags = static_cast<std::uint8_t>(sq.flags & ~MapSquare::kHasUnit);
    }
    for (int i = 0; i < unitCount(); ++i) {
        if (onMap(i)) {
            auto& sq = save_.map.at(unit(i).x, unit(i).y);
            sq.flags = static_cast<std::uint8_t>(sq.flags | MapSquare::kHasUnit);
        }
    }
}

void Game::removeUnit(int index) {
    save_.units.erase(save_.units.begin() + index);
    auto fix = [index](int& ref) {
        if (ref == index) {
            ref = -1;
        } else if (ref > index) {
            --ref;
        }
    };
    for (auto& u : save_.units) {
        fix(u.nextInTransport);
        fix(u.prevInTransport);
    }
    fix(save_.activeUnit);
}

} // namespace viceroy
