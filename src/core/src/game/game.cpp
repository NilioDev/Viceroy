// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/game/game.hpp"
#include "viceroy/game/resource.hpp"

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
    }
    reveal(toX, toY, sightRadius(index), u.nation);
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
                notices_.push_back(GameNotice{"TUTORIAL6", {goodName(good), colony.name}, -1,
                                              {colony.stock[g] / kHoldSize * kHoldSize}});
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

// Game options: header byte 18, bit 7 (seen in the Game Options box with
// the savegame edited).
bool Game::tutorialHints() const {
    return (save_.header[18] & 0x80) != 0;
}

// The hints already given: bits of the 16-bit word at header bytes 22-23
// (measured for the cargo hint only, bit 9).
bool Game::tutorialShown(int bit) const {
    const int word = save_.header[22] | (save_.header[23] << 8);
    return (word >> bit & 1) != 0;
}

void Game::markTutorialShown(int bit) {
    const int byte = 22 + bit / 8;
    save_.header[static_cast<std::size_t>(byte)] =
        static_cast<std::uint8_t>(save_.header[static_cast<std::size_t>(byte)] | (1 << (bit % 8)));
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
    // Measured: the original goes through the colonies from the last
    // founded to the first, which sets the order of its messages.
    for (std::size_t c = save_.colonies.size(); c-- > 0;) {
        auto& colony = save_.colonies[c];
        // Only the player's colonies lose work to tories.
        const bool tories = colony.nation >= 0 && colony.nation < kEuropeanNations &&
                            save_.players[static_cast<std::size_t>(colony.nation)].control == 0;
        const int sonsBefore = sonsOfLibertyPercent(colony, nationEffects(save_, colony.nation).libertyExtra(true));
        NationEffects effects = nationEffects(save_, colony.nation);
        effects.customs = static_cast<NationEffects::Customs>(customsOf(static_cast<int>(c)));
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
    gatherCrosses();
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
    // The player's units under Go To set off before anyone is asked.
    moveGoToUnits();
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

int Game::addUnitInEurope(int type, int profession, int nation) {
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

void Game::gatherCrosses() {
    // Measured: 2 crosses a turn with no colony at all, plus what the
    // colonies make. When they reach the number needed, one of the three
    // on the docks comes over for free and the crosses start again at 0.
    auto& nation = save_.nations[static_cast<std::size_t>(human())];
    int crosses = 2;
    for (const auto& colony : save_.colonies) {
        if (colony.nation == human()) {
            crosses += colonyProduction(colony, save_.map, rules_, save_.difficulty, true,
                                        nationEffects(save_, colony.nation)).produced[cargo::kCrosses];
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
    nation.recruits[slot] = randomRecruit(human());
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
