// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game itself: the state of a game (as stored in a savegame) plus the
// rules that change it. Everything here is plain logic without graphics, so
// it can be tested on its own. Rules follow the original's manual; where we
// still simplify, docs/rules/movement.md says so.
#pragma once

#include "viceroy/data/savegame.hpp"
#include "viceroy/rules/ruleset.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace viceroy {

struct Step {
    int dx = 0;
    int dy = 0;
};

// The eight directions, in the order N, NE, E, SE, S, SW, W, NW.
inline constexpr std::array<Step, 8> kSteps{
    Step{0, -1}, Step{1, -1}, Step{1, 0}, Step{1, 1}, Step{0, 1}, Step{-1, 1}, Step{-1, 0}, Step{-1, -1}};

// Unit types by their row in @UNIT.
namespace unit_type {
inline constexpr int kColonist = 0;
inline constexpr int kSoldier = 1;
inline constexpr int kPioneer = 2;
inline constexpr int kMissionary = 3;
inline constexpr int kDragoon = 4;
inline constexpr int kScout = 5;
inline constexpr int kTreasure = 10;
inline constexpr int kWagonTrain = 12;
inline constexpr int kCaravel = 13;
inline constexpr int kFirstNative = 19;
} // namespace unit_type

enum class MoveResult {
    Moved,       // the unit moved
    Boarded,     // the unit went aboard a ship
    NoMovesLeft, // not enough moves left this turn: its turn is over
    Blocked,     // the square cannot be entered (wrong terrain, no room)
    Occupied,    // a foreign unit, colony or settlement is there
};

enum class FoundResult {
    Founded,
    NotAColonist,
    NotOnLand,
    Mountains,
    TooClose, // another colony is next to this square
};

class Game {
public:
    Game(SaveGame save, Ruleset rules);

    const SaveGame& state() const { return save_; }
    SaveGame& state() { return save_; }
    const Ruleset& rules() const { return rules_; }
    int human() const { return save_.humanNation; }

    // --- Units -----------------------------------------------------------
    const SaveUnit& unit(int index) const { return save_.units[static_cast<std::size_t>(index)]; }
    int unitCount() const { return static_cast<int>(save_.units.size()); }
    bool isShip(int index) const;
    bool onMap(int index) const;
    // A land unit on a water square is aboard the ship there.
    bool isAboard(int index) const;
    int movementAllowance(int index) const; // in thirds of a move
    int movesLeft(int index) const;         // in thirds of a move
    std::string unitName(int index) const;
    // Land units aboard a ship.
    std::vector<int> passengers(int ship) const;
    // Units on a square, ships first.
    std::vector<int> unitsAt(int x, int y) const;
    // The square's colony or settlement, if any.
    std::optional<int> colonyAt(int x, int y) const;
    std::optional<int> settlementAt(int x, int y) const;

    // Cost in thirds for this unit to step from one square to the next.
    int moveCost(int index, int fromX, int fromY, int toX, int toY) const;
    MoveResult move(int index, Step step);

    // The unit waiting for orders, or -1. Picks the next one when needed.
    int activeUnit() const { return save_.activeUnit; }
    void setActiveUnit(int index);
    // A human unit on the map with moves left and no standing orders.
    bool needsOrders(int index) const;
    // The next of the human's units, after `after`, that still needs
    // orders this turn; -1 when every unit has moved.
    int nextUnitNeedingOrders(int after) const;
    // Space bar: the unit does nothing more this turn.
    void skipUnit(int index);
    void setOrders(int index, int orders);

    // --- Colonies ----------------------------------------------------------
    FoundResult canFoundColony(int index) const;
    FoundResult foundColony(int index, const std::string& name);
    // A name for the next colony from COLONY.TXT, not yet in use.
    std::string suggestColonyName(const TextFile& colonyNames) const;

    // --- Turns -------------------------------------------------------------
    // Ends the human's turn: everyone gets their moves back, and the
    // calendar moves on (one turn a year until 1600, then spring and autumn).
    void endTurn();

    // --- Map -----------------------------------------------------------------
    void reveal(int x, int y, int radius, int nation);
    int sightRadius(int index) const;

private:
    void refreshUnitFlags();
    void removeUnit(int index);

    SaveGame save_;
    Ruleset rules_;
};

} // namespace viceroy
