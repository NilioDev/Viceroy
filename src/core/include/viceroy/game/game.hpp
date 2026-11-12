// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game itself: the state of a game (as stored in a savegame) plus the
// rules that change it. Everything here is plain logic without graphics, so
// it can be tested on its own. Rules follow the original's manual; where we
// still simplify, docs/rules/movement.md says so.
#pragma once

#include "viceroy/data/savegame.hpp"
#include "viceroy/game/combat.hpp"
#include "viceroy/rules/ruleset.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace viceroy {

struct ColonyTurn;

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
    HighSeas,    // a ship moved onto the high seas: it may sail for Europe
};

enum class WorkResult {
    Started,       // the pioneer is at work until the job is done
    Finished,      // done at once (a hardy pioneer's quick job)
    NotAPioneer,   // only pioneers with tools can do this
    NotHere,       // not on land, or hills and mountains, which can't be plowed
    AlreadyPlowed, // GAME.TXT @NOPLOW
    AlreadyRoad,   // GAME.TXT @NOROAD
};

// Something the player is told in a message box: a GAME.TXT section and
// the %STRINGs that go in it.
struct GameNotice {
    std::string section;
    std::vector<std::string> strings;
    int value = -1; // what the screen needs to fill in the rest: a profession, ...
    std::vector<int> numbers{}; // the %NUMBERs
    int colony = -1;          // the colony it is about, for "Zoom to colony."
    // %STRINGs that are a nation's adjective ("French"): which string, and
    // the nation, for the screen to fill in.
    std::vector<std::pair<int, int>> adjectives{};
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
    // "Move to front." in a colony: the unit comes first among the units on
    // its square. The original keeps a list of units for each square and
    // puts the unit at its end; we keep the savegame's order instead, so
    // the unit moves to the end of the units (other units' numbers shift).
    // Returns the unit's new number.
    int moveToFront(int index);

    // --- Pioneers ----------------------------------------------------------
    // P and R: a pioneer plows the square or clears its forest
    // (SaveUnit::kPlow), or builds a road (SaveUnit::kRoad). Each job uses
    // 20 tools; a pioneer without tools is a colonist again. The timings
    // are measured: docs/rules/pioneers.md.
    WorkResult startWork(int index, int orders);
    // The effort of a job on a square: the terrain's improvement value
    // from NAMES.TXT, less 2 for a road.
    int workEffort(int x, int y, int orders) const;
    bool canPlow(int x, int y) const;

    // --- Colonies ----------------------------------------------------------
    FoundResult canFoundColony(int index) const;
    FoundResult foundColony(int index, const std::string& name);
    // A name for the next colony from COLONY.TXT, not yet in use.
    std::string suggestColonyName(const TextFile& colonyNames) const;

    // --- Turns -------------------------------------------------------------
    // Ends the human's turn: everyone gets their moves back, and the
    // calendar moves on (one turn a year until 1600, then spring and autumn).
    void endTurn();
    // What happened in the human's colonies at the last end of turn.
    const std::vector<std::string>& lastTurnNews() const { return lastTurnNews_; }
    // Message boxes for the player that came up since the last call: a
    // pioneer out of tools, ...
    std::vector<GameNotice> takeNotices() { return std::exchange(notices_, {}); }
    // A unit the colony on this square has built.
    void addBuiltUnit(int x, int y, int nation, int type);
    // A free colonist appears on the square (born in a colony).
    void addColonist(int x, int y, int nation);

    // --- Europe --------------------------------------------------------------
    // A ship on the high seas sets sail for Europe with everyone aboard. It
    // will come back to this square.
    bool sailToEurope(int ship);
    // A ship in port sets sail for the New World with its passengers.
    bool sailToNewWorld(int ship);
    // Land units in port aboard this ship.
    std::vector<int> passengersInPort(int ship) const;
    // A unit on the docks goes aboard a ship in port, or back to the docks.
    bool boardInPort(int unit, int ship);
    void leaveShipInPort(int unit);
    // Free places on a ship: holds not used by cargo or passengers.
    int freeHolds(int ship) const;
    // The Recruit, Purchase and Train buttons. Each pays from the human's
    // gold and puts the new unit on the docks (a ship in port); it returns
    // the unit, or -1 when the gold is short.
    int recruit(int slot);
    int purchase(int unitType);
    // BUY on a colony's Make panel: pays constructionPrice() and finishes
    // the construction. False when it cannot be paid.
    bool buyConstruction(int colony);
    int train(int jobRow);

    // --- Combat ----------------------------------------------------------------
    // A land unit with an attack value (soldiers, dragoons, artillery, ...).
    bool canAttack(int index) const;
    // Artillery that has lost a battle once: weaker, and the next defeat
    // destroys it.
    bool isDamagedArtillery(int index) const;
    // The unit that defends a square against `attacker`: the strongest one
    // there, or -1.
    int defenderAt(int x, int y, int attacker) const;
    // European nations at peace (the treaty bit of their relations).
    bool atPeace(int nation, int other) const;
    // Breaks the treaty both ways: the two nations are at war.
    void declareWar(int nation, int other);
    // The game option "Combat Analysis" (header byte 19, bit 1).
    bool combatAnalysisShown() const;
    CombatAnalysis analyseAttack(int attacker, int defender) const;
    // Fights it out: the loser is demoted, damaged or captured, and the
    // attacker's turn is over. Notices tell what happened.
    CombatOutcome attack(int attacker, int defender);
    // The odds the attacker wins, from the two strengths.
    static bool attackerWins(int attack, int defense, int roll);

    // --- Map -----------------------------------------------------------------
    void reveal(int x, int y, int radius, int nation);
    int sightRadius(int index) const;

private:
    void refreshUnitFlags();
    bool workDone(int index) const;
    void finishWork(int index);
    void sailOn(); // ships between Europe and the New World move on a stage
    void chainPassengers(int ship, const std::vector<int>& passengers);
    void removeUnit(int index);
    int addUnitInEurope(int type, int profession);
    int randomRecruit();
    void gatherCrosses(); // immigration at the end of the turn

    SaveGame save_;
    Ruleset rules_;
    std::vector<std::string> lastTurnNews_;
    std::vector<GameNotice> notices_;
    // The messages about a colony's warehouse after its turn.
    void storageNotices(const ColonyTurn& turn, int colony);
    void foodNotices(const ColonyTurn& turn, int colony);
    void runOutNotices(const ColonyTurn& turn, int colony);
    std::uint32_t random_ = 0;
    int nextRandom(int range); // 0 .. range - 1
    void loseBattle(int loser, int winner);
};

} // namespace viceroy
