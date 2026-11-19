// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/game/game.hpp"
#include "viceroy/game/resource.hpp"
#include "viceroy/game/sounds.hpp"
#include "viceroy/sound/background_music.hpp"

#include "viceroy/game/colony.hpp"
#include "viceroy/game/europe.hpp"

#include <algorithm>
#include <cstdlib>

namespace viceroy {

namespace {

constexpr std::uint8_t kSeaLane = 26; // the high seas: ships may sail for Europe


constexpr int kThirds = 3;
constexpr int kTerrainArctic = 24;

bool isNative(int nation) {
    return nation >= kEuropeanNations;
}

constexpr int kToolsPerJob = 20;
constexpr int kHardyPioneer = 0x14;

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
    int moves = rules_.units[static_cast<std::size_t>(type)].movement * kThirds;
    // Read from the original: Ferdinand Magellan gives the nation's ships
    // one move more.
    constexpr int kMagellan = 5;
    constexpr int kCaravel = 13; // the ships are rows 13 to 18 of @UNIT
    constexpr int kManOWar = 18;
    if (type >= kCaravel && type <= kManOWar && hasFather(unit(index).nation, kMagellan)) {
        moves += kThirds;
    }
    return moves;
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
    // Roads, and rivers followed straight (not corner to corner) from
    // square to square, cost a third; a colony or settlement at most a
    // move (read from the original).
    if ((roadFrom && roadTo) || (from.river() && to.river() && (fromX == toX || fromY == toY))) {
        return 1;
    }
    const auto row = static_cast<std::size_t>(terrainRow(to));
    const int cost = std::max(1, row < rules_.terrain.size() ? rules_.terrain[row].movementCost : 1) * kThirds;
    return (to.flags & MapSquare::kHasColony) != 0 ? std::min(cost, kThirds) : cost;
}

MoveResult Game::move(int index, Step step) {
    auto& u = save_.units[static_cast<std::size_t>(index)];
    if (!onMap(index) || isDamagedShip(index)) {
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
        // Read from the original: not onto an inland lake (water outside the
        // main body of water).
        if (target.isWater() && (target.region & 0x0F) != 1) {
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
        // Read from the original: a unit takes as many holds as its size.
        if (unitSize(index) > freeHolds(carrier)) {
            return MoveResult::Blocked;
        }
        boarding = true;
    }

    // Read from the original: whether a lost city rumour lies there is
    // looked at before the unit comes onto the square.
    const bool rumour = !ship && u.nation < kEuropeanNations && rumourAt(map, toX, toY);
    const int cost = moveCost(index, u.x, u.y, toX, toY);
    if (cost > movesLeft(index) && u.movesUsed > 0 && 1 + nextRandom(cost) > movesLeft(index)) {
        // Not enough moves for this square (read from the original): a unit
        // that has moved already gets there with a chance of its moves left
        // in the cost; otherwise its turn is over. A unit that has not moved
        // yet may always make one move.
        u.movesUsed = movementAllowance(index);
        return MoveResult::NoMovesLeft;
    }

    // A ship takes the units aboard with it; out of a colony, those who
    // went aboard there.
    if (ship) {
        const bool fromWater = map.at(u.x, u.y).isWater();
        const auto aboard = fromWater ? passengers(index) : passengersInColony(index);
        for (const int other : aboard) {
            save_.units[static_cast<std::size_t>(other)].x = toX;
            save_.units[static_cast<std::size_t>(other)].y = toY;
        }
        if (!fromWater) {
            u.prevInTransport = -1;
        }
    }
    u.x = toX;
    u.y = toY;
    // The original keeps the whole cost, even past the unit's allowance
    // (a colonist entering a forest has used 6 thirds).
    u.movesUsed += cost;
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
        u.prevInTransport = -1;
    }
    reveal(toX, toY, sightRadius(index), u.nation);
    // Read from the original: the first time one of the player's units
    // comes next to land, the player names the new land (once a game,
    // player flag 0x80).
    if (u.nation == human() && !landSighted()) {
        bool land = false;
        for (int y = toY - 1; y <= toY + 1; ++y) {
            for (int x = toX - 1; x <= toX + 1; ++x) {
                land = land || (map.inside(x, y) && !map.at(x, y).isWater());
            }
        }
        if (land) {
            auto& flags = save_.players[static_cast<std::size_t>(human())].raw[48];
            flags = static_cast<std::uint8_t>(flags | 0x80);
            // The woodcut of the discovery comes first.
            pushWithWoodcut(GameNotice{"LANDHO", {}}, 1);
        }
    }
    // Read from the original: the first time one of the player's units
    // comes next to the Pacific (water the new game marked with mask bit
    // 0x20), its woodcut.
    if (u.nation == human() && !woodcutShown(6)) {
        bool pacific = false;
        for (int y = toY - 1; y <= toY + 1; ++y) {
            for (int x = toX - 1; x <= toX + 1; ++x) {
                pacific = pacific || (map.inside(x, y) && map.at(x, y).isWater() &&
                                      (map.at(x, y).flags & MapSquare::kPacific) != 0);
            }
        }
        if (pacific) {
            if (auto picture = woodcut(6)) {
                notices_.push_back(std::move(*picture));
            }
        }
    }
    refreshUnitFlags();
    if (ship && target.isWater()) {
        slowByWarships(index);
    }
    if (rumour && !exploreRumour(index)) {
        return MoveResult::Lost;
    }
    meetNeighbours(index);
    if (colony && u.type == unit_type::kTreasure) {
        treasureArrives(index);
    }
    if (ship && target.terrain == kSeaLane && !isNative(u.nation)) {
        return MoveResult::HighSeas;
    }
    return boarding ? MoveResult::Boarded : MoveResult::Moved;
}

void Game::setActiveUnit(int index) {
    save_.activeUnit = index;
}

bool Game::needsOrders(int index) const {
    const auto& u = unit(index);
    return u.nation == human() && onMap(index) && u.orders == SaveUnit::kNoOrders && movesLeft(index) > 0 &&
           !isDamagedShip(index);
}

std::vector<int> Game::passengers(int ship) const {
    std::vector<int> out;
    if (!isShip(ship) || !onMap(ship) || !save_.map.at(unit(ship).x, unit(ship).y).isWater()) {
        return out;
    }
    // Only the ship's own nation's people: a ship of another nation may
    // share the square (where ships come back from Europe).
    for (const int other : unitsAt(unit(ship).x, unit(ship).y)) {
        if (!isShip(other) && unit(other).nation == unit(ship).nation) {
            out.push_back(other);
        }
    }
    return out;
}

std::vector<int> Game::passengersInColony(int ship) const {
    std::vector<int> out;
    if (!isShip(ship) || !onMap(ship) || !colonyAt(unit(ship).x, unit(ship).y)) {
        return out;
    }
    const auto& s = unit(ship);
    // Walking back from the ship gives them last first. One who has left
    // (moved, woken, gone to work) is passed over.
    for (int at = s.prevInTransport, steps = 0; at >= 0 && at < unitCount() && steps < unitCount();
         at = unit(at).prevInTransport, ++steps) {
        const auto& p = unit(at);
        if (isShip(at) || at == ship) {
            break;
        }
        if (p.x == s.x && p.y == s.y && p.nation == s.nation && p.orders == SaveUnit::kSentry &&
            std::find(out.begin(), out.end(), at) == out.end()) {
            out.insert(out.begin(), at);
        }
    }
    return out;
}

int Game::unitSize(int index) const {
    const int type = unit(index).type;
    if (type < 0 || static_cast<std::size_t>(type) >= rules_.units.size()) {
        return 1;
    }
    return std::max(1, rules_.units[static_cast<std::size_t>(type)].size);
}

bool Game::boardInColony(int index, int ship) {
    if (index == ship || isShip(index) || !isShip(ship) || !onMap(index) || !onMap(ship)) {
        return false;
    }
    const auto& u = unit(index);
    const auto& s = unit(ship);
    if (u.x != s.x || u.y != s.y || u.nation != s.nation || !colonyAt(s.x, s.y)) {
        return false;
    }
    auto aboard = passengersInColony(ship);
    if (std::find(aboard.begin(), aboard.end(), index) != aboard.end() || unitSize(index) > freeHolds(ship)) {
        return false;
    }
    aboard.push_back(index);
    save_.units[static_cast<std::size_t>(index)].orders = SaveUnit::kSentry;
    chainPassengers(ship, aboard);
    return true;
}

void Game::leaveShipInColony(int index) {
    if (index < 0 || index >= unitCount() || isShip(index)) {
        return;
    }
    const auto& u = unit(index);
    for (const int ship : unitsAt(u.x, u.y)) {
        if (!isShip(ship)) {
            continue;
        }
        auto aboard = passengersInColony(ship);
        const auto it = std::find(aboard.begin(), aboard.end(), index);
        if (it != aboard.end()) {
            aboard.erase(it);
            chainPassengers(ship, aboard);
            save_.units[static_cast<std::size_t>(index)].orders = SaveUnit::kNoOrders;
            save_.units[static_cast<std::size_t>(index)].prevInTransport = -1;
            save_.units[static_cast<std::size_t>(index)].nextInTransport = -1;
            return;
        }
    }
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

// --- Pioneers ---------------------------------------------------------------

bool Game::canPlow(int x, int y) const {
    const auto& sq = save_.map.at(x, y);
    return sq.isLand() && !sq.hills() && !sq.mountains();
}

int Game::workEffort(int x, int y, int orders) const {
    const auto row = static_cast<std::size_t>(terrainRow(save_.map.at(x, y)));
    const int value = row < rules_.terrain.size() ? rules_.terrain[row].improvementTurns : 0;
    return std::max(0, orders == SaveUnit::kRoad ? value - 2 : value);
}

WorkResult Game::canStartWork(int index, int orders) const {
    const auto& u = unit(index);
    if (u.type != unit_type::kPioneer || u.tools <= 0) {
        return WorkResult::NotAPioneer;
    }
    if (!onMap(index) || isAboard(index)) {
        return WorkResult::NotHere;
    }
    const auto& sq = save_.map.at(u.x, u.y);
    if (orders == SaveUnit::kPlow) {
        if (!canPlow(u.x, u.y)) {
            return WorkResult::NotHere;
        }
        if (!sq.isForest() && (sq.flags & MapSquare::kPlowed) != 0) {
            return WorkResult::AlreadyPlowed;
        }
    } else if ((sq.flags & (MapSquare::kRoad | MapSquare::kHasColony)) != 0) {
        return WorkResult::AlreadyRoad;
    }
    return WorkResult::Started;
}

WorkResult Game::startWork(int index, int orders) {
    if (const auto check = canStartWork(index, orders); check != WorkResult::Started) {
        return check;
    }
    auto& u = save_.units[static_cast<std::size_t>(index)];
    u.orders = orders;
    u.workTurns = 1;
    u.movesUsed = movementAllowance(index);
    if (workDone(index)) {
        finishWork(index);
        return WorkResult::Finished;
    }
    return WorkResult::Started;
}

bool Game::workDone(int index) const {
    const auto& u = unit(index);
    // Measured: a pioneer finishes after one turn more than the effort; a
    // hardy pioneer after half the effort, so at once when that is 0.
    const int turns = u.workTurns - 1;
    const int effort = workEffort(u.x, u.y, u.orders);
    return u.profession == kHardyPioneer ? turns >= effort / 2 : turns > effort;
}

void Game::finishWork(int index) {
    auto& u = save_.units[static_cast<std::size_t>(index)];
    auto& sq = save_.map.at(u.x, u.y);
    if (u.orders == SaveUnit::kRoad) {
        sq.flags = static_cast<std::uint8_t>(sq.flags | MapSquare::kRoad);
        landGrudge(index, true);
    } else if (sq.isForest()) {
        clearCutLumber(index);
        sq.terrain = static_cast<std::uint8_t>(sq.terrain - 8);
        landGrudge(index, false);
    } else {
        sq.flags = static_cast<std::uint8_t>(sq.flags | MapSquare::kPlowed);
    }
    u.orders = SaveUnit::kNoOrders;
    u.workTurns = 0;
    u.tools = std::max(0, u.tools - kToolsPerJob);
    if (u.tools == 0) {
        u.type = unit_type::kColonist;
        if (u.nation == human()) {
            notices_.push_back({"USEDUPTOOLS", {}});
        }
    }
}

void Game::clearCutLumber(int index) {
    // Read from the original: a forest cleared within 3 squares (the
    // longer side plus half the shorter) of one of the nation's colonies,
    // the nearest (the last one listed of those as near), gives it lumber:
    // 20 for each point of the forest's lumber yield plus one, or just 20
    // without a lumber mill; twice that for a hardy pioneer; as much as
    // the warehouse still holds. The player hears CLEARCUT.
    const auto& u = save_.units[static_cast<std::size_t>(index)];
    int nearest = -1;
    int best = 9999;
    for (std::size_t c = 0; c < save_.colonies.size(); ++c) {
        const auto& colony = save_.colonies[c];
        if (colony.nation != u.nation) {
            continue;
        }
        const int dx = std::abs(colony.x - u.x);
        const int dy = std::abs(colony.y - u.y);
        const int d = std::max(dx, dy) + std::min(dx, dy) / 2;
        if (d <= best) {
            best = d;
            nearest = static_cast<int>(c);
        }
    }
    if (nearest < 0 || best > 3) {
        return;
    }
    auto& colony = save_.colonies[static_cast<std::size_t>(nearest)];
    const int terrain = save_.map.at(u.x, u.y).terrain;
    constexpr int kLumberjack = 5;
    int per = terrain >= 0 && static_cast<std::size_t>(terrain) < rules_.terrain.size()
                  ? rules_.terrain[static_cast<std::size_t>(terrain)].yield[kLumberjack] + 1
                  : 1;
    if (buildingLevel(colony, BuildingKind::Carpenter) < 2) { // no lumber mill
        per = 1;
    }
    const int most = per * 20 * (u.profession == kHardyPioneer ? 2 : 1);
    auto& lumber = colony.stock[cargo::kLumber];
    const int amount = std::max(0, std::min(storageLimit(colony) - lumber, most));
    if (amount > 0 && colony.nation == human()) {
        notices_.push_back(GameNotice{"CLEARCUT", {colony.name}, -1, {amount}, nearest});
    }
    lumber += amount;
}

// --- Colonies --------------------------------------------------------------

FoundResult Game::canFoundColony(int index) const {
    // Read from the original's Build Colony, in its order.
    const auto& u = unit(index);
    if (isShip(index) || u.type == unit_type::kTreasure || u.type == unit_type::kWagonTrain ||
        u.type >= unit_type::kFirstNative || u.type == 11 /* artillery */) {
        return FoundResult::NotAColonist;
    }
    if (!save_.map.inside(u.x, u.y)) {
        return FoundResult::NotOnLand;
    }
    const auto colony = colonyAt(u.x, u.y);
    if (!colony && u.nation >= 0 && u.nation < kEuropeanNations) {
        // 48 colonies in the game, or 38 of the nation's own.
        int own = 0;
        for (const auto& c : save_.colonies) {
            own += c.nation == u.nation ? 1 : 0;
        }
        if (save_.colonies.size() >= 48 || own >= 38) {
            return FoundResult::TooMany;
        }
    }
    if (colony) {
        return FoundResult::InColony;
    }
    const auto& sq = save_.map.at(u.x, u.y);
    if (!sq.isLand()) {
        return FoundResult::AtSea;
    }
    if (!onMap(index) || isAboard(index)) {
        return FoundResult::NotOnLand;
    }
    if (colonyNextTo(index) >= 0) {
        return FoundResult::TooClose;
    }
    if (sq.mountains()) {
        return FoundResult::Mountains;
    }
    // Read from the original: a unit of any nation on this square or next
    // to it with orders to build a colony stops this one. (The unit itself
    // is left out.)
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            for (std::size_t i = 0; i < save_.units.size(); ++i) {
                const auto& o = save_.units[i];
                if (static_cast<int>(i) != index && o.x == u.x + dx && o.y == u.y + dy &&
                    o.orders == SaveUnit::kBuildColony && onMap(static_cast<int>(i))) {
                    return FoundResult::BuildNear;
                }
            }
        }
    }
    return FoundResult::Founded;
}

int Game::colonyNextTo(int index) const {
    const auto& u = unit(index);
    for (const auto& s : kSteps) {
        if (const auto c = colonyAt(u.x + s.dx, u.y + s.dy)) {
            return *c;
        }
    }
    return -1;
}

FoundAdvice Game::foundAdvice(int index) const {
    // Read from the original: the way to port is a water square next to
    // the site that belongs to the open sea. The nine squares of the site
    // count as productive unless they are water, arctic or desert or hold
    // a native settlement, and once more for a resource; forest is any of
    // the forest terrains.
    const auto& u = unit(index);
    const auto& map = save_.map;
    FoundAdvice advice;
    advice.noPort = true;
    for (const auto& s : kSteps) {
        const int x = u.x + s.dx;
        const int y = u.y + s.dy;
        if (map.inside(x, y) && map.at(x, y).isWater() && (map.at(x, y).region & 0x0F) == 1) {
            advice.noPort = false;
        }
    }
    int productive = 0;
    bool forest = false;
    for (int k = 0; k <= 8; ++k) {
        const int x = u.x + (k < 8 ? kSteps[static_cast<std::size_t>(k)].dx : 0);
        const int y = u.y + (k < 8 ? kSteps[static_cast<std::size_t>(k)].dy : 0);
        if (!map.inside(x, y)) {
            continue;
        }
        const auto& sq = map.at(x, y);
        const int kind = sq.mountains() ? 27 : sq.hills() ? 28 : sq.terrain;
        if (kind != 25 && kind != 26 && kind != 1 && kind != kTerrainArctic && !settlementAt(x, y)) {
            ++productive;
        }
        if (map.resourceSeed != 0 && resourceAt(map, x, y) != Resource::None) {
            ++productive;
        }
        forest = forest || (kind >= 8 && kind < 24);
    }
    if (save_.difficulty < 2) {
        advice.fewSquares = productive < 4;
        advice.noForest = !forest;
    }
    return advice;
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
    // As in the saves, a new colony has its town hall, carpenter's shop,
    // blacksmith's house and the four craft houses (rows of @BUILDING).
    for (const int row : {9, 21, 24, 27, 32, 35, 39}) {
        colony.buildings |= std::uint64_t{1} << row;
    }
    // The founder works as a farmer; the eight squares around are free.
    colony.jobs[0] = 0; // farmer
    colony.skills[0] = static_cast<std::uint8_t>(u.profession);
    startLiberty(colony, save_.map, rules_, save_.difficulty);
    // Byte 28, bit 0x40: the colony lies by the water (as in the saves).
    for (const auto& s : kSteps) {
        if (save_.map.inside(u.x + s.dx, u.y + s.dy) && save_.map.at(u.x + s.dx, u.y + s.dy).isWater()) {
            colony.raw[0x1C] = static_cast<std::uint8_t>(colony.raw[0x1C] | 0x40);
            break;
        }
    }
    save_.colonies.push_back(colony);
    auto& sq = save_.map.at(u.x, u.y);
    sq.flags = static_cast<std::uint8_t>(sq.flags | MapSquare::kHasColony | MapSquare::kRoad);
    removeUnit(index);
    reveal(colony.x, colony.y, 2, colony.nation);
    refreshUnitFlags();
    // Read from the original: the player's new colony is greeted with a
    // sound, and the first one with its woodcut.
    if (colony.nation == human()) {
        sound(sounds::kColonyFounded);
        if (auto picture = woodcut(2)) {
            notices_.push_back(std::move(*picture));
        }
    }
    return FoundResult::Founded;
}

void Game::runOutNotices(const ColonyTurn& turn, int c) {
    // GAME.TXT has one report for each raw good a workshop uses.
    static constexpr std::pair<int, const char*> kSections[] = {
        {cargo::kSugar, "CANESUGAR"}, {cargo::kTobacco, "TOBACCO"}, {cargo::kCotton, "COTTON"}, {cargo::kFurs, "FURS"},
        {cargo::kLumber, "LUMBER"},   {cargo::kOre, "ORE"},         {cargo::kTools, "TOOLS"},
    };
    // Read from the original: the colony report option for raw materials
    // turns all of these off.
    if (!colonyReportOn(colony_report::kRawMaterials)) {
        return;
    }
    for (const auto& [good, section] : kSections) {
        if (turn.ranOut[static_cast<std::size_t>(good)]) {
            notices_.push_back(GameNotice{section, {save_.colonies[static_cast<std::size_t>(c)].name}, -1, {}, c});
        }
    }
}

bool Game::starve(const ColonyTurn& turn, int c) {
    auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    const bool ours = colony.nation == human();
    // Seen in the original with a debugger: the other nations' colonies
    // do not starve for less than 3 food.
    const int missing = ours || turn.foodMissing >= 3 ? turn.foodMissing : 0;
    // A colonist starves when the store was already empty as the turn
    // began. On the two easiest levels nobody does before 1520, and after
    // that only one time in three (Discoverer) or two (Explorer).
    bool dies = turn.foodRanOut && missing > 0 && turn.foodBefore <= 0;
    if (dies && save_.difficulty <= 1) {
        if (save_.year < 1520 || nextRandom(3 - std::max(0, save_.difficulty)) != 0) {
            dies = false;
        }
    }
    if (!dies) {
        if (ours) {
            foodNotices(turn, c);
        }
        return false;
    }
    if (colony.population <= 1) {
        if (ours) {
            GameNotice notice{"VANISH", {colony.name}};
            notice.music = {{MusicCue::Kind::ChangeMood, 1}};
            notices_.push_back(std::move(notice));
        }
        removeColony(c);
        return true;
    }
    if (ours) {
        const bool autumnNext = save_.year >= 1600 && save_.season == 0;
        GameNotice notice{autumnNext ? "STARVE2" : "STARVE1", {colony.name}, -1, {}, c};
        notice.music = {{MusicCue::Kind::ChangeMood, 1}};
        notices_.push_back(std::move(notice));
    }
    removeColonist(colony, nextRandom(colony.population));
    return false;
}

void Game::removeColony(int c) {
    const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    auto& sq = save_.map.at(colony.x, colony.y);
    sq.flags = static_cast<std::uint8_t>(sq.flags & ~MapSquare::kHasColony);
    save_.colonies.erase(save_.colonies.begin() + c);
    // Reports about the colonies after it now name them one lower.
    for (auto& n : notices_) {
        if (n.colony == c) {
            n.colony = -1;
        } else if (n.colony > c) {
            --n.colony;
        }
    }
}

void Game::foodNotices(const ColonyTurn& turn, int c) {
    const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    const int food = colony.stock[cargo::kFood];
    // Measured: a colony 10 food short warns with 39 left but not with 49,
    // one 4 short not yet with 19; when the food is gone it says so, with
    // winter coming when the next turn is autumn.
    if (turn.foodRanOut) {
        const bool autumnNext = save_.year >= 1600 && save_.season == 0;
        GameNotice notice{autumnNext ? "FOOD2" : "FOOD1", {colony.name}, -1, {}, c};
        notice.music = {{MusicCue::Kind::MoodEvent, 1}};
        notices_.push_back(std::move(notice));
    } else if (turn.foodShort > 0 && food < 4 * turn.foodShort && colonyReportOn(colony_report::kFood)) {
        // The food report option turns off this warning only (read from
        // the original).
        GameNotice notice{"FOODLOW", {colony.name}, -1, {food}, c};
        notice.music = {{MusicCue::Kind::SetMood, 1}};
        notices_.push_back(std::move(notice));
    }
}

void Game::storageNotices(const ColonyTurn& turn, int c) {
    const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    const int limit = storageLimit(colony);
    const bool canExpand = colony.raw[149] < 2;
    const auto goodName = [&](int good) {
        return static_cast<std::size_t>(good) < rules_.cargo.size() ? rules_.cargo[static_cast<std::size_t>(good)].name
                                                                    : std::string();
    };
    // Measured: a cargo that is ready reports the full warehouse with it
    // (and then not as thrown away); the rest that was thrown away is
    // reported once, by name when it was one good.
    int spoiled = -1;
    int spoiledKinds = 0;
    for (int good = 1; good < kGoodsKinds; ++good) {
        const auto g = static_cast<std::size_t>(good);
        if (turn.cargoReady[g] && colonyReportOn(colony_report::kNewCargo)) {
            // The option for new cargos turns this report (and the hint
            // with it) off; what was thrown away is still told (read from
            // the original).
            const bool full = colony.stock[g] >= limit;
            const char* section = !full ? "CARGOREADY0" : canExpand ? "CARGOREADY1" : "CARGOREADY2";
            GameNotice notice{section, {colony.name, goodName(good)}, -1, {limit}, c};
            notice.music = {{MusicCue::Kind::SetMood, 2}};
            notices_.push_back(std::move(notice));
            // Measured: with Tutorial Hints on, the naval advisor follows the
            // first cargo report of the game with how to ship it home
            // ("there are 100 Cotton in ..."); never again after that.
            if (tutorialHints() && !tutorialShown(kTutorialCargo)) {
                markTutorialShown(kTutorialCargo);
                notices_.push_back(GameNotice{
                    "TUTORIAL6", {goodName(good), colony.name}, 0, {colony.stock[g] / kHoldSize * kHoldSize}});
            }
        } else if (turn.wasted[g] > 0) {
            spoiled = good;
            ++spoiledKinds;
        }
    }
    if (spoiledKinds == 1) {
        notices_.push_back(GameNotice{canExpand ? "SPOIL1" : "SPOIL3",
                                      {colony.name, goodName(spoiled)},
                                      -1,
                                      {turn.wasted[static_cast<std::size_t>(spoiled)]},
                                      c});
    } else if (spoiledKinds > 1) {
        notices_.push_back(GameNotice{canExpand ? "SPOIL2" : "SPOIL4", {colony.name}, -1, {}, c});
    }
}

void Game::clearSpecialty(int c, int colonist) {
    auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    if (colonist >= 0 && colonist < colony.population) {
        colony.skills[static_cast<std::size_t>(colonist)] = skill::kFreeColonist;
    }
}

void Game::setExport(int c, int good, bool on) {
    setExportTicked(save_.colonies[static_cast<std::size_t>(c)], good, on);
}

int Game::customsOf(int c) const {
    // Read from the original: a colony with a custom house sells, unless it
    // is the player's and an armed ship of another nation (privateer,
    // frigate, man-o-war) is within 5 squares of it.
    const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    if (buildingLevel(colony, BuildingKind::CustomHouse) == 0 || colony.nation < 0 ||
        colony.nation >= kEuropeanNations) {
        return static_cast<int>(NationEffects::Customs::None);
    }
    const bool player = save_.players[static_cast<std::size_t>(colony.nation)].control == 0;
    if (!player) {
        return static_cast<int>(NationEffects::Customs::Computer);
    }
    for (const auto& u : save_.units) {
        constexpr int kFirstShip = 13, kLastShip = 18;
        if (u.nation == colony.nation || u.nation >= kEuropeanNations || u.type < kFirstShip || u.type > kLastShip ||
            static_cast<std::size_t>(u.type) >= rules_.units.size() ||
            rules_.units[static_cast<std::size_t>(u.type)].attack <= 0 || !save_.map.inside(u.x, u.y)) {
            continue;
        }
        const int dx = std::abs(u.x - colony.x);
        const int dy = std::abs(u.y - colony.y);
        if (std::max(dx, dy) + std::min(dx, dy) / 2 <= 5) {
            return static_cast<int>(NationEffects::Customs::None);
        }
    }
    return static_cast<int>(NationEffects::Customs::Ticked);
}

void Game::sellExports(const ColonyTurn& turn, int c) {
    // Read from the original: what the custom house sells goes at the price
    // of Europe, with the nation's tax (none after the declaration), and
    // moves the market as a sale there does. The player reads each sale on
    // the top line: "Curacao sells 143 Ore for 715.  2% Tax: 14.  Net: 701".
    const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    if (colony.nation < 0 || colony.nation >= kEuropeanNations) {
        return;
    }
    auto& nation = save_.nations[static_cast<std::size_t>(colony.nation)];
    const bool independent = (save_.header[18] & 0x01) != 0;
    for (int good = 0; good < kGoodsKinds; ++good) {
        const auto g = static_cast<std::size_t>(good);
        const int amount = turn.exported[g];
        if (amount <= 0) {
            continue;
        }
        const int gross = amount * bidPrice(nation, good);
        const int tax = independent ? 0 : gross * nation.taxRate / 100;
        nation.gold += gross - tax;
        nation.tradeGold[g] += gross - tax;
        nation.tonsTraded[g] += amount;
        nation.tonsTraded2[g] += amount;
        moveMarket(rules_, nation, good, amount);
        if (colony.nation == human()) {
            GameNotice notice{"CUSTOMSALE",
                              {colony.name, rules_.cargo.size() > g ? rules_.cargo[g].name : std::string()},
                              independent ? -1 : nation.taxRate,
                              {amount, gross, tax, gross - tax},
                              c};
            notices_.push_back(std::move(notice));
        }
    }
}

void Game::wearMines(int c) {
    // Read from the original. Each turn every miner on a resource adds to
    // a count: an ore miner on minerals 1, a silver miner on minerals 2, a
    // silver miner on a silver deposit 1. For each of those the colony's
    // wear (byte 151) goes up by one, except one time in (difficulty + 2).
    // At 50 it starts again from 0, and every such resource the colony
    // works then is used up (mask bit 2): minerals are gone, silver is
    // left as a poor deposit.
    constexpr int kOreMiner = 6, kSilverMiner = 7;
    constexpr std::size_t kWear = 151;
    constexpr int kWornOut = 50;
    constexpr std::uint8_t kUsedUp = 0x04;
    auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    const auto worked = [&](std::size_t s, int& x, int& y) {
        const int who = colony.squares[s];
        if (who < 0 || who >= colony.population) {
            return -1;
        }
        x = colony.x + kColonySquares[s].dx;
        y = colony.y + kColonySquares[s].dy;
        return save_.map.inside(x, y) ? static_cast<int>(colony.jobs[static_cast<std::size_t>(who)]) : -1;
    };
    int count = 0;
    for (std::size_t s = 0; s < colony.squares.size(); ++s) {
        int x = 0, y = 0;
        const int job = worked(s, x, y);
        const Resource r = job < 0 ? Resource::None : resourceAt(save_.map, x, y);
        if (r == Resource::Minerals) {
            count += job == kOreMiner ? 1 : job == kSilverMiner ? 2 : 0;
        } else if (r == Resource::Silver) {
            count += job == kSilverMiner ? 1 : 0;
        }
    }
    for (int i = 0; i < count; ++i) {
        if (nextRandom(save_.difficulty + 2) == 0) {
            continue;
        }
        colony.raw[kWear] = static_cast<std::uint8_t>(colony.raw[kWear] + 1);
        if (colony.raw[kWear] < kWornOut) {
            continue;
        }
        colony.raw[kWear] = static_cast<std::uint8_t>(colony.raw[kWear] - kWornOut);
        for (std::size_t s = 0; s < colony.squares.size(); ++s) {
            int x = 0, y = 0;
            const int job = worked(s, x, y);
            if (job != kOreMiner && job != kSilverMiner) {
                continue;
            }
            const Resource r = resourceAt(save_.map, x, y);
            if (r != Resource::Minerals && r != Resource::Silver) {
                continue;
            }
            auto& sq = save_.map.at(x, y);
            sq.flags = static_cast<std::uint8_t>(sq.flags | kUsedUp);
            if (colony.nation == human()) {
                notices_.push_back(GameNotice{"DEPLETION", {colony.name}, -1, {}, c});
            }
        }
    }
}

void Game::setColonyNames(const TextFile& colonyNames) {
    static constexpr const char* kSections[] = {"ENGLISH", "FRENCH", "SPANISH", "DUTCH"};
    for (std::size_t n = 0; n < colonyNames_.size(); ++n) {
        colonyNames_[n].clear();
        const auto* section = colonyNames.find(kSections[n]);
        if (section == nullptr) {
            continue;
        }
        for (const auto& line : section->lines) {
            const auto fields = splitFields(line);
            if (!fields.empty() && !fields[0].empty()) {
                colonyNames_[n].push_back(fields[0]);
            }
        }
    }
}

std::string Game::nextColonyName(int nation) {
    // Read from the original: the nation's count of names given (word 50
    // of its player record) picks the line of its list in COLONY.TXT, and
    // goes up by one, even when the player then gives another name or none.
    // Past the end of the list the first name comes again, and the count
    // stays.
    if (nation < 0 || nation >= kEuropeanNations) {
        return {};
    }
    const auto& list = colonyNames_[static_cast<std::size_t>(nation & 3)];
    if (list.empty()) {
        return "Colony " + std::to_string(save_.colonies.size() + 1);
    }
    auto& raw = save_.players[static_cast<std::size_t>(nation)].raw;
    const int count = raw[50] | (raw[51] << 8);
    if (count >= static_cast<int>(list.size())) {
        return list.front().substr(0, 23);
    }
    raw[50] = static_cast<std::uint8_t>((count + 1) & 0xFF);
    raw[51] = static_cast<std::uint8_t>((count + 1) >> 8);
    return list[static_cast<std::size_t>(count)].substr(0, 23);
}

// --- Turns -------------------------------------------------------------------

void Game::endTurn() {
    lastTurnNews_.clear();
    visits_.clear(); // a visit the player never answered is let go
    // Seen with a debugger: the count of forces is redone when the
    // player's turn ends.
    countForces();
    // The original starts the player's part of a round with the King, in
    // the turn that comes; the colonies' turn follows (measured: the
    // King's message comes before theirs).
    {
        const int nextYear = save_.year < 1600 || save_.season != 0 ? save_.year + 1 : save_.year;
        if (!kingTax(human(), save_.turn + 1, nextYear)) {
            kingDeclaresWar(human(), save_.turn + 1);
        }
    }
    // The player's nation is counted too, as the computer's are at the
    // start of their turns (its figures in the Foreign Affairs report come
    // from this count).
    nationCensus(human());
    kingFrigate(human(), save_.turn + 1);
    // Read from the original: the turn of a nation begins with its
    // immigration, then its colonies add the crosses they make.
    immigration(human());
    // Measured: the original goes through the colonies from the last
    // founded to the first, which sets the order of its messages.
    for (std::size_t c = save_.colonies.size(); c-- > 0;) {
        // Read from the original: first the fort fires on ships nearby.
        fortFire(static_cast<int>(c));
        auto& colony = save_.colonies[c];
        // Only the player's colonies lose work to tories.
        const bool tories = colony.nation >= 0 && colony.nation < kEuropeanNations &&
                            save_.players[static_cast<std::size_t>(colony.nation)].control == 0;
        const int sonsBefore = sonsOfLibertyPercent(colony, nationEffects(save_, colony.nation).libertyExtra(true));
        NationEffects effects = nationEffects(save_, colony.nation);
        effects.customs = static_cast<NationEffects::Customs>(customsOf(static_cast<int>(c)));
        if (colony.nation >= 0 && colony.nation < kEuropeanNations) {
            const int crosses =
                colonyProduction(colony, save_.map, rules_, save_.difficulty, tories, effects).produced[cargo::kCrosses];
            if (colony.nation == human()) {
                save_.nations[static_cast<std::size_t>(colony.nation)].crosses += crosses;
            } else {
                colonyCrosses_[static_cast<std::size_t>(colony.nation)] += crosses;
            }
        }
        const ColonyTurn turn = runColonyTurn(colony, save_.map, rules_, save_.difficulty, tories, effects);
        sellExports(turn, static_cast<int>(c));
        // Read from the original: the colony's bells go to the congress
        // first, before its messages.
        addCongressBells(colony.nation, turn.bells);
        const bool ours = colony.nation == human();
        if (ours && turn.libertyAfter != turn.libertyBefore) {
            // Read from the original: the option for rebel majorities
            // keeps these quiet, and then the Sons of Liberty are not
            // told either.
            const bool up = turn.libertyAfter > turn.libertyBefore;
            const char* section = up ? (turn.libertyAfter == 2 ? "REBELUNANIMOUS" : "REBELMAJORITY")
                                     : (turn.libertyAfter == 1 ? "TORYMINORITY" : "TORYMAJORITY");
            const int percent = sonsOfLibertyPercent(colony, nationEffects(save_, colony.nation).libertyExtra(true));
            if (colonyReportOn(colony_report::kRebels)) {
                notices_.push_back(GameNotice{section, {colony.name}, -1, {percent}, static_cast<int>(c)});
            }
        } else if (ours && colonyReportOn(colony_report::kSonsOfLiberty)) {
            // Read from the original: membership passing a ten up, or
            // falling below one by more than 4, unless the colony report
            // option for the Sons of Liberty is off.
            const int sons = sonsOfLibertyPercent(colony, nationEffects(save_, colony.nation).libertyExtra(true));
            if (sonsBefore / 10 < sons / 10) {
                notices_.push_back(GameNotice{"SONSUP", {colony.name}, -1, {sons}, static_cast<int>(c)});
            } else if ((sons + 4) / 10 < sonsBefore / 10) {
                notices_.push_back(GameNotice{"SONSDOWN", {colony.name}, -1, {sons}, static_cast<int>(c)});
            }
        }
        {
            // Read from the original: when the colony's tories reach 10 less
            // the difficulty level (the level where they hold back
            // production), the player hears of inefficient government once,
            // and of efficient government again when they fall below it
            // (colony byte 28, bit 3, remembers which). The report option
            // only keeps these quiet.
            const int sons = sonsOfLibertyPercent(colony, nationEffects(save_, colony.nation).libertyExtra(true));
            const int limit = 10 - save_.difficulty;
            const int toryCount = colony.population * (100 - sons) / 100;
            constexpr std::uint8_t kInefficient = 0x08;
            const bool was = (colony.raw[28] & kInefficient) != 0;
            const bool told = ours && colonyReportOn(colony_report::kInefficient);
            if (toryCount >= limit) {
                if (!was && told) {
                    notices_.push_back(GameNotice{"INEFFICIENT", {colony.name}, -1, {limit}, static_cast<int>(c)});
                }
                colony.raw[28] = static_cast<std::uint8_t>(colony.raw[28] | kInefficient);
            } else {
                if (was && told) {
                    notices_.push_back(GameNotice{"EFFICIENT", {colony.name}, -1, {}, static_cast<int>(c)});
                }
                colony.raw[28] = static_cast<std::uint8_t>(colony.raw[28] & ~kInefficient);
            }
        }
        if (ours) {
            runOutNotices(turn, static_cast<int>(c));
            storageNotices(turn, static_cast<int>(c));
        }
        wearMines(static_cast<int>(c));
        teach(static_cast<int>(c));
        learnByDoing(static_cast<int>(c));
        if (starve(turn, static_cast<int>(c))) {
            continue;
        }
        if (turn.grew) {
            addColonist(colony.x, colony.y, colony.nation);
            if (ours) {
                notices_.push_back(GameNotice{"NEWCOLONIST", {colony.name}, -1, {}, static_cast<int>(c)});
            }
        }
        if (turn.builtUnit >= 0) {
            addBuiltUnit(colony.x, colony.y, colony.nation, turn.builtUnit);
        }
        if (ours && (turn.built >= 0 || turn.builtUnit >= 0)) {
            // Measured: "Curacao colony produces Wagon Train." from the
            // colonial advisor, with the map on the colony.
            const std::string what = turn.builtUnit >= 0 ? rules_.units[static_cast<std::size_t>(turn.builtUnit)].name
                                                         : constructionName(rules_, turn.built);
            GameNotice notice{"BUILT", {colony.name, what}, -1, {}, static_cast<int>(c)};
            // Read from the original: the armory, magazine, printing press
            // and newspaper call for the independence tunes.
            constexpr int kArmory = 3, kMagazine = 4, kPress = 19, kNewspaper = 20;
            const int b = turn.builtUnit >= 0 ? -1 : turn.built;
            const bool rousing = b == kArmory || b == kMagazine || b == kPress || b == kNewspaper;
            notice.music = {{MusicCue::Kind::MoodEvent, rousing ? 3 : 2}};
            notices_.push_back(std::move(notice));
        }
        if (ours && turn.noMoreWagons) {
            const int colonies = nationEffects(save_, colony.nation).colonies;
            notices_.push_back(GameNotice{"NOMOREWAGONS", {colony.name}, -1, {colonies}, static_cast<int>(c)});
        }
        if (ours && turn.alreadyHave) {
            // "Jamestown is set to produce a Docks, but it has already
            // built one!"
            notices_.push_back(GameNotice{"ALREADYHAVE",
                                          {colony.name, constructionName(rules_, colony.construction)},
                                          -1,
                                          {},
                                          static_cast<int>(c)});
        }
        if (ours && turn.noMoreWarehouse) {
            notices_.push_back(GameNotice{"NOMOREWAREHOUSE", {colony.name}, -1, {}, static_cast<int>(c)});
        }
        if (ours && turn.toolsNeeded > 0 && colonyReportOn(colony_report::kTools)) {
            const int have = std::max(0, colony.stock[cargo::kTools]);
            notices_.push_back(GameNotice{have > 0 ? "NEEDTOOLS" : "NEEDTOOLS0",
                                          {colony.name, constructionName(rules_, colony.construction)},
                                          -1,
                                          {turn.toolsNeeded, have},
                                          static_cast<int>(c)});
        }
    }
    // After its colonies, each nation's rebels are counted (the player
    // hears of it) and the Crown spends on its army.
    for (int n = 0; n < kEuropeanNations; ++n) {
        rebelSentiment(n);
    }
    offerMercenaries();
    repairShips();
    loseFaith();
    sailOn();
    for (int i = 0; i < unitCount(); ++i) {
        auto& u = save_.units[static_cast<std::size_t>(i)];
        u.movesUsed = 0;
        if ((u.orders == SaveUnit::kPlow || u.orders == SaveUnit::kRoad) && onMap(i)) {
            ++u.workTurns;
            if (workDone(i)) {
                // The job takes the rest of this turn too.
                finishWork(i);
                u.movesUsed = movementAllowance(i);
            }
        }
    }
    // The original's round: the natives, then the nations in order. The
    // computer's nations after the player's move before the year turns,
    // the ones before it after.
    for (int n = human() + 1; n < kEuropeanNations; ++n) {
        europeanTurn(n);
    }
    nativesTurn();
    ++save_.turn;
    if (save_.year < 1600) {
        ++save_.year;
        save_.season = 0;
    } else if (save_.season == 0) {
        if (save_.year == 1600) {
            // Read from the original: the first time the year has two
            // turns, a help box says so ("Colonization Help: Time Scale").
            notices_.push_back(GameNotice{"TIMECHANGE", {}});
        }
        save_.season = 1;
    } else {
        save_.season = 0;
        ++save_.year;
    }
    for (int n = 0; n < human(); ++n) {
        europeanTurn(n);
    }
    // The year has turned: is the game over?
    checkTheEnd();
    // The player's units under Go To and on trade routes set off before
    // anyone is asked.
    moveGoToUnits();
    moveTradeRouteUnits();
    save_.activeUnit = nextUnitNeedingOrders(-1);
}

// --- Europe --------------------------------------------------------------------

namespace {

// Where the nation's ships come back from Europe: bytes 50 and 51 of its
// record, set when a ship leaves for Europe.
constexpr std::size_t kReturnX = 50;
constexpr std::size_t kReturnY = 51;

} // namespace

void Game::chainPassengers(int ship, const std::vector<int>& passengers) {
    // The chain runs from the first passenger to the ship.
    int previous = -1;
    for (const int p : passengers) {
        auto& u = save_.units[static_cast<std::size_t>(p)];
        u.prevInTransport = previous;
        if (previous >= 0) {
            save_.units[static_cast<std::size_t>(previous)].nextInTransport = p;
        }
        previous = p;
    }
    if (previous >= 0) {
        save_.units[static_cast<std::size_t>(previous)].nextInTransport = ship;
    }
    save_.units[static_cast<std::size_t>(ship)].prevInTransport = previous;
    save_.units[static_cast<std::size_t>(ship)].nextInTransport = -1;
}

bool Game::sailToEurope(int ship) {
    if (!isShip(ship) || !onMap(ship)) {
        return false;
    }
    auto& u = save_.units[static_cast<std::size_t>(ship)];
    if (independenceDeclared() && (u.nation != crownNation() || u.type != unit_type::kManOWar)) {
        // Read from the original: during the war only the Crown's
        // men-of-war sail for Europe.
        return false;
    }
    const auto aboard = passengers(ship);
    if (u.nation >= 0 && u.nation < kEuropeanNations) {
        auto& raw = save_.nations[static_cast<std::size_t>(u.nation)].raw;
        raw[kReturnX] = static_cast<std::uint8_t>(u.x);
        raw[kReturnY] = static_cast<std::uint8_t>(u.y);
    }
    setEuropeStage(u, kLastStage);
    if (u.orders != kTradeRouteOrders) {
        u.orders = SaveUnit::kNoOrders;
    }
    for (const int p : aboard) {
        auto& passenger = save_.units[static_cast<std::size_t>(p)];
        passenger.x = u.x;
        passenger.y = u.y;
        passenger.orders = SaveUnit::kSentry;
    }
    chainPassengers(ship, aboard);
    refreshUnitFlags();
    return true;
}

std::vector<int> Game::passengersInPort(int ship) const {
    std::vector<int> out;
    if (europePlace(unit(ship)) != EuropePlace::InPort) {
        return out;
    }
    // Walking back from the ship gives the passengers last first.
    for (int at = unit(ship).prevInTransport, steps = 0; at >= 0 && at < unitCount() && steps < unitCount();
         at = unit(at).prevInTransport, ++steps) {
        if (isShip(at) || unit(at).x != unit(ship).x || unit(at).orders != SaveUnit::kSentry) {
            break;
        }
        out.insert(out.begin(), at);
    }
    return out;
}

int Game::freeHolds(int ship) const {
    const int type = unit(ship).type;
    if (!isShip(ship)) {
        return 0;
    }
    int used = unit(ship).holds;
    const auto aboard = !onMap(ship)                                         ? passengersInPort(ship)
                        : save_.map.at(unit(ship).x, unit(ship).y).isWater() ? passengers(ship)
                                                                             : passengersInColony(ship);
    for (const int p : aboard) {
        used += unitSize(p);
    }
    return std::max(0, rules_.units[static_cast<std::size_t>(type)].cargoSlots - used);
}

namespace {

// Read from the original: holds of the good are topped up first, the rest
// takes a new hold.
void addToHolds(SaveUnit& u, int good, int amount) {
    for (int k = 0; k < u.holds && amount > 0; ++k) {
        if (holdGood(u, k) == good && holdAmount(u, k) < kHoldSize) {
            const int add = std::min(amount, kHoldSize - holdAmount(u, k));
            setHold(u, k, good, holdAmount(u, k) + add);
            amount -= add;
        }
    }
    if (amount > 0 && u.holds < kMaxHolds) {
        setHold(u, u.holds, good, amount);
        ++u.holds;
    }
}

} // namespace

void Game::captureCargo(int loser, int winner) {
    // Read from the original: a ship that beats another takes its cargo,
    // hold by hold, while it has room: all of it when there is room for
    // all; otherwise the player picks (PICKACARGO), and the computer takes
    // the hold worth most at its prices. The rest is lost with the ship.
    captureShip_ = -1;
    captureHolds_.clear();
    auto& l = save_.units[static_cast<std::size_t>(loser)];
    std::vector<std::pair<int, int>> holds;
    for (int k = 0; k < l.holds; ++k) {
        holds.emplace_back(holdGood(l, k), holdAmount(l, k));
    }
    l.holds = 0;
    if (holds.empty()) {
        return;
    }
    const int winnerNation = unit(winner).nation;
    const bool shown = winnerNation == human() || unit(loser).nation == human();
    const auto take = [&](std::size_t k, bool tell) {
        const auto [good, amount] = holds[k];
        addToHolds(save_.units[static_cast<std::size_t>(winner)], good, amount);
        if (tell && shown) {
            // "Dutch cargo of 100 furs captured by English Privateer!"
            GameNotice notice{"CARGOCAPTURE",
                              {"",
                               static_cast<std::size_t>(good) < rules_.cargo.size()
                                   ? rules_.cargo[static_cast<std::size_t>(good)].name
                                   : std::string(),
                               "", rules_.units[static_cast<std::size_t>(unit(winner).type)].name},
                              -1,
                              {amount}};
            notice.adjectives = {{0, unit(loser).nation}, {2, winnerNation}};
            notices_.push_back(std::move(notice));
        }
        holds.erase(holds.begin() + static_cast<std::ptrdiff_t>(k));
    };
    if (freeHolds(winner) >= static_cast<int>(holds.size())) {
        while (!holds.empty()) {
            take(0, true);
        }
        return;
    }
    if (winnerNation == human()) {
        captureShip_ = winner;
        captureHolds_ = holds;
        notices_.push_back(GameNotice{"PICKACARGO", {}});
        return;
    }
    const auto& nation = save_.nations[static_cast<std::size_t>(winnerNation & 3)];
    while (!holds.empty() && freeHolds(winner) > 0) {
        std::size_t best = 0;
        int most = -1;
        for (std::size_t k = 0; k < holds.size(); ++k) {
            const int worth = bidPrice(nation, holds[k].first) * holds[k].second;
            if (worth >= most) {
                most = worth;
                best = k;
            }
        }
        take(best, true);
    }
}

std::vector<std::pair<int, int>> Game::cargoToCapture() const { return captureHolds_; }

void Game::answerCargoCapture(int hold) {
    const int ship = std::exchange(captureShip_, -1);
    if (ship < 0 || ship >= unitCount() || hold < 0 || hold >= static_cast<int>(captureHolds_.size())) {
        captureHolds_.clear();
        return;
    }
    const auto [good, amount] = captureHolds_[static_cast<std::size_t>(hold)];
    addToHolds(save_.units[static_cast<std::size_t>(ship)], good, amount);
    captureHolds_.erase(captureHolds_.begin() + hold);
    // Asked again while there is room and cargo left.
    if (!captureHolds_.empty() && freeHolds(ship) > 0) {
        captureShip_ = ship;
        notices_.push_back(GameNotice{"PICKACARGO", {}});
    } else {
        captureHolds_.clear();
    }
}

namespace {

int gearGood(Game::DockGear gear) {
    return gear == Game::DockGear::Muskets ? cargo::kMuskets
           : gear == Game::DockGear::Tools ? cargo::kTools
                                           : cargo::kHorses;
}

int gearAmount(Game::DockGear gear) { return gear == Game::DockGear::Tools ? 100 : 50; }

} // namespace

int Game::dockGearPrice(DockGear gear, bool buying) const {
    const auto& nation = save_.nations[static_cast<std::size_t>(human())];
    const int good = gearGood(gear);
    return gearAmount(gear) * (buying ? askPrice(rules_, nation, good) : bidPrice(nation, good));
}

int Game::equipOnDocks(int index, DockGear gear, bool buying) {
    auto& u = save_.units[static_cast<std::size_t>(index)];
    auto& nation = save_.nations[static_cast<std::size_t>(u.nation)];
    const int price = dockGearPrice(gear, buying);
    using namespace unit_type;
    switch (gear) {
    case DockGear::Muskets:
        u.type = buying ? (u.type == kScout ? kDragoon : kSoldier) : (u.type == kDragoon ? kScout : kColonist);
        break;
    case DockGear::Tools:
        u.type = buying ? kPioneer : kColonist;
        u.tools = buying ? 100 : 0;
        break;
    case DockGear::Horses:
        u.type = buying ? (u.type == kSoldier ? kDragoon : kScout) : (u.type == kDragoon ? kSoldier : kColonist);
        break;
    }
    nation.gold += buying ? -price : price;
    const int amount = gearAmount(gear);
    return moveMarket(rules_, nation, gearGood(gear), buying ? -amount : amount);
}

void Game::disband(int index) {
    if (index < 0 || index >= unitCount()) {
        return;
    }
    removeUnit(index);
    refreshUnitFlags();
}

void Game::dumpHold(int carrier, int hold) {
    if (carrier < 0 || carrier >= unitCount()) {
        return;
    }
    removeHold(save_.units[static_cast<std::size_t>(carrier)], hold);
}

int Game::colonyCargoRoom(int carrier, int good) const {
    const auto& u = unit(carrier);
    const int type = u.type;
    if (type < 0 || static_cast<std::size_t>(type) >= rules_.units.size()) {
        return 0;
    }
    int used = u.holds;
    if (isShip(carrier)) {
        used += static_cast<int>(passengers(carrier).size());
    }
    const int free = rules_.units[static_cast<std::size_t>(type)].cargoSlots - used;
    if (free > 0) {
        return 100 * free;
    }
    int room = 0;
    for (int k = 0; k < u.holds; ++k) {
        if (holdGood(u, k) == good && holdAmount(u, k) < kHoldSize) {
            room += kHoldSize - holdAmount(u, k);
        }
    }
    return room;
}

int Game::loadInColony(int carrier, int colony, int good, int amount) {
    if (colony < 0 || static_cast<std::size_t>(colony) >= save_.colonies.size() || good < 0 || good >= kGoodsKinds) {
        return 0;
    }
    auto& stock = save_.colonies[static_cast<std::size_t>(colony)].stock[static_cast<std::size_t>(good)];
    int take = std::min({amount, kHoldSize, stock, colonyCargoRoom(carrier, good)});
    if (take <= 0) {
        return 0;
    }
    stock -= take;
    auto& u = save_.units[static_cast<std::size_t>(carrier)];
    addToHolds(u, good, take);
    if (u.orders != kTradeRouteOrders) {
        u.orders = SaveUnit::kNoOrders;
    }
    return take;
}

int Game::colonyCargoHold(int carrier, int good) const {
    const auto& u = unit(carrier);
    for (int k = 0; k < u.holds; ++k) {
        if (holdGood(u, k) == good) {
            return holdAmount(u, k);
        }
    }
    return -1;
}

int Game::cargoToLoad(int carrier, int colony) const {
    if (carrier < 0 || carrier >= unitCount() || colony < 0 ||
        static_cast<std::size_t>(colony) >= save_.colonies.size()) {
        return -1;
    }
    const auto& u = unit(carrier);
    const int empty =
        isShip(carrier) ? freeHolds(carrier) : rules_.units[static_cast<std::size_t>(u.type)].cargoSlots - u.holds;
    if (empty <= 0 || u.nation < 0 || u.nation >= kEuropeanNations) {
        return -1;
    }
    const auto& c = save_.colonies[static_cast<std::size_t>(colony)];
    const auto& nation = save_.nations[static_cast<std::size_t>(u.nation)];
    int best = -1;
    int bestValue = 0;
    for (int good = 0; good < kGoodsKinds; ++good) {
        if (good == cargo::kHorses || good == cargo::kTools || good == cargo::kMuskets) {
            continue;
        }
        const int amount = std::min(100, static_cast<int>(c.stock[static_cast<std::size_t>(good)]));
        const int value = amount * std::max(1, bidPrice(nation, good));
        if (amount > 0 && value > bestValue) {
            best = good;
            bestValue = value;
        }
    }
    return best;
}

int Game::unloadInColony(int carrier, int colony, int good, int amount) {
    if (colony < 0 || static_cast<std::size_t>(colony) >= save_.colonies.size()) {
        return 0;
    }
    auto& u = save_.units[static_cast<std::size_t>(carrier)];
    int hold = -1;
    for (int k = 0; k < u.holds && hold < 0; ++k) {
        if (holdGood(u, k) == good) {
            hold = k;
        }
    }
    if (hold < 0) {
        return 0;
    }
    const int aboard = holdAmount(u, hold);
    const int moved = std::clamp(amount, 0, aboard);
    removeHold(u, hold);
    auto& stock = save_.colonies[static_cast<std::size_t>(colony)].stock[static_cast<std::size_t>(good)];
    stock += moved;
    if (aboard - moved > 0) {
        addToHolds(u, good, aboard - moved); // the rest goes back aboard
    }
    return moved;
}

bool Game::boardInPort(int index, int ship) {
    if (isShip(index) || !isShip(ship) || europePlace(unit(index)) != EuropePlace::InPort ||
        europePlace(unit(ship)) != EuropePlace::InPort || unit(index).nation != unit(ship).nation ||
        freeHolds(ship) <= 0) {
        return false;
    }
    auto aboard = passengersInPort(ship);
    if (std::find(aboard.begin(), aboard.end(), index) != aboard.end()) {
        return false;
    }
    aboard.push_back(index);
    save_.units[static_cast<std::size_t>(index)].orders = SaveUnit::kSentry;
    chainPassengers(ship, aboard);
    return true;
}

void Game::leaveShipInPort(int index) {
    for (int ship = 0; ship < unitCount(); ++ship) {
        if (!isShip(ship) || europePlace(unit(ship)) != EuropePlace::InPort) {
            continue;
        }
        auto aboard = passengersInPort(ship);
        const auto it = std::find(aboard.begin(), aboard.end(), index);
        if (it != aboard.end()) {
            aboard.erase(it);
            chainPassengers(ship, aboard);
        }
    }
    auto& u = save_.units[static_cast<std::size_t>(index)];
    u.orders = SaveUnit::kNoOrders;
    u.nextInTransport = -1;
    u.prevInTransport = -1;
}

bool Game::sailToNewWorld(int ship) {
    if (!isShip(ship) || isDamagedShip(ship) || europePlace(unit(ship)) != EuropePlace::InPort) {
        return false;
    }
    const auto aboard = passengersInPort(ship);
    setEuropeStage(save_.units[static_cast<std::size_t>(ship)], kPortStage - 1);
    for (const int p : aboard) {
        setEuropeStage(save_.units[static_cast<std::size_t>(p)], kPortStage - 1);
    }
    chainPassengers(ship, aboard);
    return true;
}

void Game::sailOn() {
    std::vector<int> treasures;
    bool cargoWoodcut = false;
    for (int ship = 0; ship < unitCount(); ++ship) {
        if (!isShip(ship)) {
            continue;
        }
        const auto& u = unit(ship);
        const int stage = europeStage(u);
        if (stage < 0 || stage == kPortStage) {
            continue;
        }
        // Its passengers, before the ship moves on.
        std::vector<int> aboard;
        for (int at = u.prevInTransport, steps = 0; at >= 0 && at < unitCount() && steps < unitCount();
             at = unit(at).prevInTransport, ++steps) {
            if (isShip(at) || unit(at).x != u.x) {
                break;
            }
            aboard.insert(aboard.begin(), at);
        }
        const bool ours = u.nation == human();
        const std::string name = unitName(ship);
        if (stage > 0) {
            const int next = stage - 1;
            setEuropeStage(save_.units[static_cast<std::size_t>(ship)], next);
            for (const int p : aboard) {
                setEuropeStage(save_.units[static_cast<std::size_t>(p)], next);
            }
            if (next == kPortStage) {
                // Read from the original: treasure aboard is sold on
                // arrival; the Crown takes the tax rate, at most 50%.
                for (const int p : aboard) {
                    if (unit(p).type == unit_type::kTreasure) {
                        treasures.push_back(p);
                    }
                }
            }
            if (next == kPortStage && ours) {
                lastTurnNews_.push_back(name + " has arrived in Europe.");
                // Read from the original: the first ship to arrive that is
                // not on a trade route opens the Europe screen, and the
                // first time it brings cargo, the woodcut of the cargo.
                if (europeArrival_ < 0 && u.orders != kTradeRouteOrders) {
                    europeArrival_ = ship;
                    cargoWoodcut = u.holds > 0;
                }
            }
            continue;
        }
        // Stage 0: the ship arrives in the New World where it left.
        const auto& raw = save_.nations[static_cast<std::size_t>(u.nation & 3)].raw;
        int x = raw[kReturnX];
        int y = raw[kReturnY];
        if (!save_.map.inside(x, y) || !save_.map.at(x, y).isWater()) {
            // Nowhere known: the first sea lane from the east, half way down.
            y = save_.map.height / 2;
            for (x = save_.map.width - 1; x > 0 && !save_.map.at(x, y).isWater(); --x) {
            }
        }
        for (const int p : aboard) {
            auto& passenger = save_.units[static_cast<std::size_t>(p)];
            passenger.x = x;
            passenger.y = y;
            passenger.orders = SaveUnit::kSentry;
        }
        auto& s = save_.units[static_cast<std::size_t>(ship)];
        s.x = x;
        s.y = y;
        if (s.orders != kTradeRouteOrders) {
            s.orders = SaveUnit::kNoOrders;
        }
        reveal(x, y, sightRadius(ship), s.nation);
        if (ours) {
            lastTurnNews_.push_back(name + " has arrived in the New World.");
        }
    }
    // The treasure is sold once every ship has moved on (it leaves the
    // list of units), the last first.
    std::sort(treasures.rbegin(), treasures.rend());
    for (const int t : treasures) {
        if (europeArrival_ > t) {
            --europeArrival_;
        }
        treasureInEurope(t);
    }
    if (cargoWoodcut) {
        if (auto picture = woodcut(9)) {
            notices_.push_back(std::move(*picture));
        }
    }
    refreshUnitFlags();
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

void Game::revealLand(int x, int y, int radius, int nation) {
    // Read from the original with a debugger: the edge of the map is left
    // out, and beyond the next squares only land of the same region (the
    // low four bits of the path layer) is shown.
    const auto& map = save_.map;
    const auto inner = [&](int px, int py) { return px >= 1 && py >= 1 && px < map.width - 1 && py < map.height - 1; };
    if (isNative(nation) || nation < 0 || !inner(x, y)) {
        return;
    }
    const int region = map.at(x, y).isLand() ? (map.at(x, y).region & 0x0F) : -1;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int px = x + dx;
            const int py = y + dy;
            if (!inner(px, py)) {
                continue;
            }
            auto& sq = save_.map.at(px, py);
            const bool near = std::abs(dx) <= 1 && std::abs(dy) <= 1;
            if (!near && (!sq.isLand() || (sq.region & 0x0F) != region)) {
                continue;
            }
            sq.seen = static_cast<std::uint8_t>(sq.seen | (0x10u << nation));
        }
    }
}

int Game::sightRadius(int index) const {
    // Read from the original and measured: one square; two for a galleon,
    // privateer or frigate; a land unit sees two with Hernando de Soto in
    // congress, and a scout one more than any other (measured: a dragoon
    // 1, with de Soto 2; a scout 2, with de Soto 3).
    constexpr int kGalleon = 15;
    constexpr int kFrigate = 17;
    constexpr int kDeSoto = 7;
    const auto& u = unit(index);
    int radius = u.type >= kGalleon && u.type <= kFrigate ? 2 : 1;
    if (!isShip(index) && hasFather(u.nation, kDeSoto)) {
        radius = 2;
    }
    if (u.type == unit_type::kScout) {
        ++radius;
    }
    return radius;
}

bool Game::roomForUnit(int nation, int type) {
    const int count = unitCount();
    const bool european = nation >= 0 && nation < kEuropeanNations;
    const bool player = european && nation == human();
    if (!player && count >= kMaxUnits - 8) {
        return false;
    }
    bool full = count >= kMaxUnits;
    if (!full && european) {
        int own = 0;
        for (const auto& u : save_.units) {
            own += u.nation == nation ? 1 : 0;
        }
        full = own > 200;
    }
    if (full && player) {
        // "A {unit} cannot be created because the maximum number of units
        // for the game has been reached."
        GameNotice notice{"TOOMANYUNITS", {}};
        if (type >= 0 && type < static_cast<int>(rules_.units.size())) {
            notice.strings = {rules_.units[static_cast<std::size_t>(type)].name};
        }
        notices_.push_back(std::move(notice));
    }
    return !full;
}

int Game::addColonist(int x, int y, int nation) {
    if (!roomForUnit(nation, unit_type::kColonist)) {
        return -1;
    }
    SaveUnit u;
    // A free colonist with no orders, as the original stores one.
    static constexpr std::array<std::uint8_t, 28> kTemplate{0, 0, 0, 0x80, 0, 0, 0xFF, 0x58, 0, 0, 0, 0, 0, 0,
                                                            0, 0, 0, 0, 0xFF, 0, 0, 0, 0, 0x1C, 0xFF, 0xFF, 0xFF, 0xFF};
    u.raw = kTemplate;
    u.x = x;
    u.y = y;
    u.type = unit_type::kColonist;
    u.nation = nation;
    u.profession = 0x1C;
    save_.units.push_back(u);
    refreshUnitFlags();
    return unitCount() - 1;
}

int Game::addBuiltUnit(int x, int y, int nation, int type) {
    if (!roomForUnit(nation, type)) {
        return -1;
    }
    SaveUnit u;
    // As the original stores a unit it has just built (measured: artillery
    // keeps the colonist's 0x1C in byte 23, wagons and ships 0).
    static constexpr std::array<std::uint8_t, 28> kTemplate{0, 0, 0, 0x80, 0, 0, 0xFF, 0x58, 0, 0, 0, 0, 0, 0,
                                                            0, 0, 0, 0, 0xFF, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF};
    u.raw = kTemplate;
    u.x = x;
    u.y = y;
    u.type = type;
    u.nation = nation;
    u.profession = type == kArtillery ? 0x1C : 0;
    save_.units.push_back(u);
    refreshUnitFlags();
    return unitCount() - 1;
}

int Game::addUnitInEurope(int type, int profession, int nation) {
    if (!roomForUnit(nation < 0 ? human() : nation, type)) {
        return -1;
    }
    SaveUnit u;
    static constexpr std::array<std::uint8_t, 28> kTemplate{0, 0, 0, 0, 0, 0, 0xFF, 0x58, 0, 0, 0, 0, 0, 0,
                                                            0, 0, 0, 0, 0xFF, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF};
    u.raw = kTemplate;
    u.type = type;
    u.nation = nation < 0 ? human() : nation;
    u.profession = profession;
    setEuropeStage(u, kPortStage);
    save_.units.push_back(u);
    return unitCount() - 1;
}

int Game::tribeMood(int tribe) {
    constexpr int kInca = 0, kAztec = 1;
    return tribe == kInca ? 7 : tribe == kAztec ? 6 : 5;
}

int Game::randomRecruit(int nation) {
    // Who turns up on the docks next, read from the original: first three
    // draws for a criminal, a servant or a free colonist (likelier at a
    // harder level, William Brewster turns the first two into free
    // colonists), otherwise an expert from the nation's own little
    // shift-register sequence that is not already waiting on the docks.
    // See docs/rules/europe.md.
    constexpr int kBrewster = 20;
    constexpr int kServant = 0x19;
    constexpr int kCriminal = 0x1A;
    auto& n = save_.nations[static_cast<std::size_t>(nation)];
    const bool brewster = hasFather(nation, kBrewster);
    const int level = (nation == human() ? save_.difficulty : 1) + 1;
    const int cut = (level + 2) >> 1;
    if (1 + nextRandom(15) <= cut) {
        return brewster ? kFreeColonist : kCriminal;
    }
    if (1 + nextRandom(10) <= cut) {
        return brewster ? kFreeColonist : kServant;
    }
    if (1 + nextRandom(8) <= cut) {
        return kFreeColonist;
    }
    const auto expert = [](int p) {
        return p != kFreeColonist && p != 0x13 && p != kServant && p != kCriminal && p != 0x1B;
    };
    int tries = 0;
    for (;;) {
        if (std::all_of(n.recruits.begin(), n.recruits.end(), [&](int p) { return expert(p); })) {
            return kFreeColonist;
        }
        int v = 0;
        do {
            int s = n.raw[68];
            s = (s & 1) != 0 ? (s >> 1) ^ 0x14 : s >> 1;
            n.raw[68] = static_cast<std::uint8_t>(s);
            v = (n.raw[68] + n.raw[69]) & 31;
        } while (v >= 25);
        switch (v) {
        case 18:
        case 23:
            v = 13;
            break;
        case 19:
            v = 22;
            break;
        case 1:
            v = 8;
            break;
        case 2:
            v = 5;
            break;
        case 3:
            v = 0;
            break;
        case 4:
            v = 6;
            break;
        default:
            break;
        }
        if (++tries > 100 || std::find(n.recruits.begin(), n.recruits.end(), v) == n.recruits.end()) {
            return v;
        }
    }
}

int Game::crossesNeeded(int nation) const {
    // Read from the original: twice the nation's people (the colonists in
    // its colonies and every unit it has, ships and those in Europe too)
    // plus 8, at most 4000. A computer nation needs (8 − level) eighths of
    // that, and the English two thirds.
    int people = 0;
    for (const auto& colony : save_.colonies) {
        if (colony.nation == nation) {
            people += colony.population;
        }
    }
    for (const auto& u : save_.units) {
        people += u.nation == nation ? 1 : 0;
    }
    if (people < 4000) {
        people *= 2;
    }
    int needed = std::min(people + 8, 4000);
    if (nation < 0 || nation >= kEuropeanNations || save_.players[static_cast<std::size_t>(nation)].control != 0) {
        needed = (8 - save_.difficulty) * needed >> 3;
    }
    if (nation == 0) {
        needed = needed * 2 / 3;
    }
    return needed;
}

int Game::dockCrosses(int nation) const {
    // Read from the original: 2 crosses a turn; but once a newcomer has
    // come (bit 0x40 of the nation record's first byte, never cleared),
    // everyone waiting in port makes the crosses go down instead, by 2
    // each.
    int crosses = 2;
    if (nation < 0 || nation >= kEuropeanNations ||
        (save_.nations[static_cast<std::size_t>(nation)].raw[0] & kNewcomerCame) == 0) {
        return crosses;
    }
    const int port = kEuropeBase + nation + 4 * kPortStage;
    for (const auto& u : save_.units) {
        // The units that are people: colonists, soldiers, pioneers,
        // missionaries, dragoons, scouts and the Continental units.
        const bool person = u.type <= unit_type::kScout || u.type == 7 || u.type == 9;
        if (u.nation == nation && u.x == port && person) {
            crosses = crosses > 0 ? -2 : crosses - 2;
        }
    }
    return crosses;
}

void Game::immigration(int nation) {
    if (nation < 0 || nation >= kEuropeanNations) {
        return;
    }
    auto& n = save_.nations[static_cast<std::size_t>(nation)];
    const int fromColonies = std::exchange(colonyCrosses_[static_cast<std::size_t>(nation)], 0);
    // Read from the original: nobody comes once independence is declared.
    if (independenceDeclared()) {
        n.crosses += fromColonies;
        return;
    }
    n.raw[0] = static_cast<std::uint8_t>(n.raw[0] & ~0x20);
    const int needed = crossesNeeded(nation);
    n.crossesNeeded = needed;
    n.crosses = std::max(0, n.crosses + dockCrosses(nation));
    const bool player = save_.players[static_cast<std::size_t>(nation)].control == 0;
    // Read from the original: more crosses than needed bring someone.
    if (n.crosses > needed) {
        immigrate(nation, player);
    }
    n.crosses += fromColonies;
}

void Game::immigrate(int nation, bool player) {
    auto& n = save_.nations[static_cast<std::size_t>(nation)];
    // Read from the original: a newcomer makes the next tune a cheerful
    // one (at once without Background Music).
    std::vector<MusicCue> music{{MusicCue::Kind::SetMood, 2}};
    if (!viceroy::sound::SoundOptions::fromSave(soundOptionBits()).background) {
        music.push_back({MusicCue::Kind::ChangeMood, 2});
    }
    // Read from the original: with William Brewster the player chooses
    // which of the three comes (RECRUITCHOOSE), without the news of unrest.
    // (What a computer nation does with him is not known: it takes one by
    // chance, like the others.)
    constexpr int kBrewster = 20;
    if (player && hasFather(nation, kBrewster)) {
        // The crosses go back to 0 when the player has chosen; the box
        // comes at once in the original.
        n.crosses = 0;
        GameNotice choose{"RECRUITCHOOSE", {}};
        choose.music = std::move(music);
        notices_.push_back(std::move(choose));
        return;
    }
    n.crosses = 0;
    // Guess: which of the three comes is chance (the original took the
    // second in the one case measured).
    random_ = random_ == 0 ? static_cast<std::uint32_t>(save_.turn * 7919 + 17) | 1U : random_;
    random_ = random_ * 1103515245U + 12345U;
    const auto slot = static_cast<std::size_t>((random_ >> 16) % 3U);
    const int profession = n.recruits[slot];
    n.recruits[slot] = randomRecruit(nation);
    if (addUnitInEurope(professionUnitType(profession), profession, nation) < 0) {
        return;
    }
    if (player) {
        GameNotice unrest{"UNREST", {}, profession};
        unrest.music = std::move(music);
        notices_.push_back(std::move(unrest));
    }
    n.raw[0] = static_cast<std::uint8_t>(n.raw[0] | kNewcomerCame);
}

int Game::recruit(int slot) {
    auto& nation = save_.nations[static_cast<std::size_t>(human())];
    if (slot < 0 || slot >= static_cast<int>(nation.recruits.size())) {
        return -1;
    }
    const int price = recruitPrice(nation, save_.difficulty);
    if (nation.gold < price) {
        return -1;
    }
    const int profession = nation.recruits[static_cast<std::size_t>(slot)];
    const int made = addUnitInEurope(professionUnitType(profession), profession);
    if (made < 0) {
        return -1; // TOOMANYUNITS: nothing is paid
    }
    nation.gold -= price;
    ++nation.recruitCount;
    nation.crosses = 0;
    nation.recruits[static_cast<std::size_t>(slot)] = randomRecruit(human());
    return made;
}

int Game::purchase(int unitType) {
    auto& nation = save_.nations[static_cast<std::size_t>(human())];
    for (const auto& item : purchaseList(nation)) {
        if (item.unitType != unitType) {
            continue;
        }
        if (nation.gold < item.price) {
            return -1;
        }
        const int made = addUnitInEurope(unitType, 0);
        if (made < 0) {
            return -1; // TOOMANYUNITS: nothing is paid
        }
        nation.gold -= item.price;
        if (unitType == kArtillery) {
            ++nation.artilleryBought;
        }
        return made;
    }
    return -1;
}

bool Game::buyConstruction(int colonyIndex) {
    if (colonyIndex < 0 || static_cast<std::size_t>(colonyIndex) >= save_.colonies.size()) {
        return false;
    }
    auto& colony = save_.colonies[static_cast<std::size_t>(colonyIndex)];
    auto& nation = save_.nations[static_cast<std::size_t>(colony.nation)];
    const int price = constructionPrice(colony, rules_, nation);
    if (price <= 0 || nation.gold < price) {
        return false;
    }
    nation.gold -= price;
    colony.hammers = std::max(colony.hammers, constructionHammers(rules_, colony.construction));
    colony.stock[cargo::kTools] = std::max(colony.stock[cargo::kTools], constructionTools(rules_, colony.construction));
    return true;
}

int Game::train(int jobRow) {
    auto& nation = save_.nations[static_cast<std::size_t>(human())];
    if (jobRow < 0 || jobRow >= static_cast<int>(rules_.jobs.size())) {
        return -1;
    }
    const int price = rules_.jobs[static_cast<std::size_t>(jobRow)].europeCost;
    if (price <= 0 || nation.gold < price) {
        return -1;
    }
    const int made = addUnitInEurope(professionUnitType(jobRow), jobRow);
    if (made >= 0) {
        nation.gold -= price; // TOOMANYUNITS: nothing is paid
    }
    return made;
}

void Game::refreshUnitFlags() {
    // Read from the original: a unit coming onto an empty square writes its
    // nation into the high four bits of the path layer, which stay when it
    // leaves (so they tell who was there last). A native unit leaves a lost
    // city rumour's square as it is. Bit 0 of the mask marks the squares
    // with units.
    auto& map = save_.map;
    std::vector<std::uint8_t> had(map.squares.size());
    for (std::size_t i = 0; i < map.squares.size(); ++i) {
        auto& sq = map.squares[i];
        had[i] = sq.flags & MapSquare::kHasUnit;
        sq.flags = static_cast<std::uint8_t>(sq.flags & ~MapSquare::kHasUnit);
    }
    for (int i = 0; i < unitCount(); ++i) {
        if (!onMap(i)) {
            continue;
        }
        const auto& u = unit(i);
        const auto at = static_cast<std::size_t>(u.y) * static_cast<std::size_t>(map.width) + static_cast<std::size_t>(u.x);
        auto& sq = map.squares[at];
        if (had[at] == 0 && (sq.flags & MapSquare::kHasUnit) == 0 && u.nation >= 0 && u.nation < 16 &&
            (u.nation < kEuropeanNations || !rumourAt(map, u.x, u.y))) {
            sq.region = static_cast<std::uint8_t>((sq.region & 0x0F) | (u.nation << 4));
        }
        sq.flags = static_cast<std::uint8_t>(sq.flags | MapSquare::kHasUnit);
    }
}

int Game::moveToFront(int index) {
    if (index < 0 || index >= unitCount()) {
        return index;
    }
    const int last = unitCount() - 1;
    const SaveUnit moved = save_.units[static_cast<std::size_t>(index)];
    save_.units.erase(save_.units.begin() + index);
    save_.units.push_back(moved);
    auto fix = [index, last](int& ref) {
        if (ref == index) {
            ref = last;
        } else if (ref > index) {
            --ref;
        }
    };
    for (auto& u : save_.units) {
        fix(u.nextInTransport);
        fix(u.prevInTransport);
    }
    fix(save_.activeUnit);
    return last;
}

void Game::removeUnit(int index) {
    // A settlement whose brave is gone raises a new one (seen in the
    // original's code: bit 0x01 of the home settlement's byte 3).
    const auto& gone = save_.units[static_cast<std::size_t>(index)];
    if (gone.nation >= kEuropeanNations && gone.raw[6] < save_.settlements.size()) {
        auto& home = save_.settlements[gone.raw[6]];
        home.raw[3] = static_cast<std::uint8_t>(home.raw[3] | 0x01);
    }
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
