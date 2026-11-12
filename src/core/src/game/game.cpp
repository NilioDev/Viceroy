// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/game/game.hpp"

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
    const auto row = static_cast<std::size_t>(terrainRow(to));
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

WorkResult Game::startWork(int index, int orders) {
    auto& u = save_.units[static_cast<std::size_t>(index)];
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
    } else if (sq.isForest()) {
        sq.terrain = static_cast<std::uint8_t>(sq.terrain - 8);
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
    colony.jobs[0] = 0; // farmer
    colony.skills[0] = static_cast<std::uint8_t>(u.profession);
    startLiberty(colony, save_.map, rules_, save_.difficulty);
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
    lastTurnNews_.clear();
    // Measured: the original goes through the colonies from the last
    // founded to the first, which sets the order of its messages.
    for (std::size_t c = save_.colonies.size(); c-- > 0;) {
        auto& colony = save_.colonies[c];
        const ColonyTurn turn = runColonyTurn(colony, save_.map, rules_, save_.difficulty);
        const bool ours = colony.nation == human();
        if (ours && turn.libertyAfter != turn.libertyBefore) {
            const bool up = turn.libertyAfter > turn.libertyBefore;
            const char* section = up ? (turn.libertyAfter == 2 ? "REBELUNANIMOUS" : "REBELMAJORITY")
                                     : (turn.libertyAfter == 1 ? "TORYMINORITY" : "TORYMAJORITY");
            GameNotice notice{section, {colony.name}, -1, {sonsOfLibertyPercent(colony)}, static_cast<int>(c)};
            notices_.push_back(std::move(notice));
        }
        if (turn.grew) {
            addColonist(colony.x, colony.y, colony.nation);
            if (ours) {
                notices_.push_back(GameNotice{"NEWCOLONIST", {colony.name}, -1, {}, static_cast<int>(c)});
            }
        }
        if (turn.starved) {
            const auto last = static_cast<std::size_t>(colony.population - 1);
            for (auto& who : colony.squares) {
                if (who == static_cast<int>(last)) {
                    who = -1;
                }
            }
            --colony.population;
            if (ours) {
                lastTurnNews_.push_back(colony.name + ": a colonist starved.");
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
            notices_.push_back(GameNotice{"BUILT", {colony.name, what}, -1, {}, static_cast<int>(c)});
        }
        if (ours && turn.toolsNeeded > 0) {
            const int have = std::max(0, colony.stock[cargo::kTools]);
            notices_.push_back(GameNotice{have > 0 ? "NEEDTOOLS" : "NEEDTOOLS0",
                                          {colony.name, constructionName(rules_, colony.construction)},
                                          -1,
                                          {turn.toolsNeeded, have},
                                          static_cast<int>(c)});
        }
    }
    gatherCrosses();
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
    const auto aboard = passengers(ship);
    if (u.nation >= 0 && u.nation < kEuropeanNations) {
        auto& raw = save_.nations[static_cast<std::size_t>(u.nation)].raw;
        raw[kReturnX] = static_cast<std::uint8_t>(u.x);
        raw[kReturnY] = static_cast<std::uint8_t>(u.y);
    }
    setEuropeStage(u, kLastStage);
    u.orders = SaveUnit::kNoOrders;
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
    if (onMap(ship)) {
        used += static_cast<int>(passengers(ship).size());
    } else {
        used += static_cast<int>(passengersInPort(ship).size());
    }
    return std::max(0, rules_.units[static_cast<std::size_t>(type)].cargoSlots - used);
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
    if (!isShip(ship) || europePlace(unit(ship)) != EuropePlace::InPort) {
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
            if (next == kPortStage && ours) {
                lastTurnNews_.push_back(name + " has arrived in Europe.");
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
        s.orders = SaveUnit::kNoOrders;
        reveal(x, y, sightRadius(ship), s.nation);
        if (ours) {
            lastTurnNews_.push_back(name + " has arrived in the New World.");
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

int Game::sightRadius(int index) const {
    const int type = unit(index).type;
    return (type == unit_type::kScout || type == unit_type::kDragoon) ? 2 : 1;
}

void Game::addColonist(int x, int y, int nation) {
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
}

void Game::addBuiltUnit(int x, int y, int nation, int type) {
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
}

int Game::addUnitInEurope(int type, int profession) {
    SaveUnit u;
    static constexpr std::array<std::uint8_t, 28> kTemplate{0, 0, 0, 0, 0, 0, 0xFF, 0x58, 0, 0, 0, 0, 0, 0,
                                                            0, 0, 0, 0, 0xFF, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF};
    u.raw = kTemplate;
    u.type = type;
    u.nation = human();
    u.profession = profession;
    setEuropeStage(u, kPortStage);
    save_.units.push_back(u);
    return unitCount() - 1;
}

int Game::randomRecruit() {
    // Who turns up on the docks next. The original's odds are not known;
    // these favour the unskilled as it seems to. See docs/rules/europe.md.
    struct Odds {
        int profession;
        int weight;
    };
    static constexpr Odds kOdds[] = {
        {kFreeColonist, 20}, {0x19, 10}, {0x1A, 8}, {0x00, 2}, {0x04, 2}, {0x05, 2}, {0x06, 2}, {0x07, 2},
        {0x08, 2},           {0x09, 2},  {0x0A, 2}, {0x0B, 2}, {0x0C, 2}, {0x0D, 2}, {0x0E, 2}, {0x0F, 2},
        {0x10, 1},           {0x11, 1},  {0x14, 2}, {0x15, 2}, {0x16, 2}, {0x18, 1},
    };
    int total = 0;
    for (const auto& o : kOdds) {
        total += o.weight;
    }
    if (random_ == 0) {
        random_ = static_cast<std::uint32_t>(save_.year * 4 + save_.season + save_.turn * 7919) | 1U;
    }
    random_ = random_ * 1103515245U + 12345U;
    int pick = static_cast<int>((random_ >> 16) % static_cast<std::uint32_t>(total));
    for (const auto& o : kOdds) {
        if (pick < o.weight) {
            return o.profession;
        }
        pick -= o.weight;
    }
    return kFreeColonist;
}

void Game::gatherCrosses() {
    // Measured: 2 crosses a turn with no colony at all, plus what the
    // colonies make. When they reach the number needed, one of the three
    // on the docks comes over for free and the crosses start again at 0.
    auto& nation = save_.nations[static_cast<std::size_t>(human())];
    int crosses = 2;
    for (const auto& colony : save_.colonies) {
        if (colony.nation == human()) {
            crosses += colonyProduction(colony, save_.map, rules_, save_.difficulty).produced[cargo::kCrosses];
        }
    }
    nation.crosses += crosses;
    if (nation.crosses < nation.crossesNeeded || nation.crossesNeeded <= 0) {
        return;
    }
    nation.crosses = 0;
    // Guess: which of the three comes is chance (the original took the
    // second in the one case measured).
    random_ = random_ == 0 ? static_cast<std::uint32_t>(save_.turn * 7919 + 17) | 1U : random_;
    random_ = random_ * 1103515245U + 12345U;
    const auto slot = static_cast<std::size_t>((random_ >> 16) % 3U);
    const int profession = nation.recruits[slot];
    addUnitInEurope(professionUnitType(profession), profession);
    nation.recruits[slot] = randomRecruit();
    notices_.push_back({"UNREST", {}, profession});
}

int Game::recruit(int slot) {
    auto& nation = save_.nations[static_cast<std::size_t>(human())];
    if (slot < 0 || slot >= static_cast<int>(nation.recruits.size())) {
        return -1;
    }
    const int price = recruitPrice(nation);
    if (nation.gold < price) {
        return -1;
    }
    nation.gold -= price;
    const int profession = nation.recruits[static_cast<std::size_t>(slot)];
    const int made = addUnitInEurope(professionUnitType(profession), profession);
    ++nation.recruitCount;
    nation.crosses = 0;
    nation.recruits[static_cast<std::size_t>(slot)] = randomRecruit();
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
        nation.gold -= item.price;
        if (unitType == kArtillery) {
            ++nation.artilleryBought;
        }
        return addUnitInEurope(unitType, 0);
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
    nation.gold -= price;
    return addUnitInEurope(professionUnitType(jobRow), jobRow);
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
