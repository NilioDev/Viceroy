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
    Lost,        // the unit vanished exploring a lost city rumour
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
    int unit = -1;            // the unit it is about, for the map to show
    // %STRINGs that are a nation's adjective ("French"): which string, and
    // the nation, for the screen to fill in.
    std::vector<std::pair<int, int>> adjectives{};
    // %STRINGs that are a word of the MISC section of LABELS.TXT
    // ("Wilderness"): which string, and the row.
    std::vector<std::pair<int, int>> labels{};
    // %STRINGs that are a nation's country ("England", "the Netherlands").
    std::vector<std::pair<int, int>> countries{};
    // %STRINGs that are a nation's home port ("London").
    std::vector<std::pair<int, int>> ports{};
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
    // The original's route finder (docs/rules/routes.md): the direction
    // the unit steps next towards its destination (bytes 9 and 10 of its
    // record), -1 for none. `nation` keeps the route off others' units and
    // towns (-1: anyone's). Its working state lives on between calls, as in
    // the original.
    int routeStep(int index, int nation);
    // Go To (orders 3): the colonies the unit can be sent to, sending it
    // there or a ship to Europe, and moving the player's units that have
    // it. See docs/rules/routes.md.
    static constexpr int kGoToOrders = 3;
    std::vector<int> goToColonies(int index) const;
    void setGoTo(int index, int x, int y);
    bool goToEurope(int index);
    bool goToStep(int index); // true when the unit can go on
    void moveGoToUnits();
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
    // COLONY.TXT, for the names the computer's nations give their colonies.
    void setColonyNames(const TextFile& colonyNames);

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

    // --- Tutorial hints ------------------------------------------------------
    // The "Tutorial Hints" game option.
    bool tutorialHints() const;
    // Each hint is given once a game; the savegame remembers which.
    static constexpr int kTutorialCargo = 9; // @TUTORIAL6: ship a cargo home
    bool tutorialShown(int bit) const;
    void markTutorialShown(int bit);

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
    // The Sound Options as the save keeps them (header byte 22).
    std::uint8_t soundOptionBits() const { return save_.header[22]; }
    void setSoundOptionBits(std::uint8_t bits) { save_.header[22] = bits; }
    // The game option "Combat Analysis" (header byte 19, bit 1).
    bool combatAnalysisShown() const;
    CombatAnalysis analyseAttack(int attacker, int defender) const;
    // Fights it out: the loser is demoted, damaged or captured, and the
    // attacker's turn is over. Notices tell what happened.
    CombatOutcome attack(int attacker, int defender);
    // The numbers the fight is rolled on (see docs/rules/combat.md). With
    // beforeLevels, only the steps a computer player weighs an attack by
    // (the original leaves out the steps for the easy levels and colonies).
    CombatStrengths combatStrengths(int attacker, int defender, bool beforeLevels = false) const;
    // A foreign colony with nobody on its square: one of its colonists,
    // picked at random, defends it.
    bool colonyUndefended(int colony) const;
    int pickColonyDefender(int colony);
    CombatAnalysis analyseColonyAttack(int attacker, int colony, int colonist) const;
    // With 50 muskets and Paul Revere a colony's colonist defends armed.
    bool colonistTakesUpArms(int colony) const;
    CombatStrengths colonyStrengths(int attacker, int colony, int colonist, bool beforeLevels = false) const;
    // Fights the colonist; when the attacker wins it marches in and takes
    // the colony with its share of the owner's gold.
    CombatOutcome attackColony(int attacker, int colony, int colonist);
    // Native settlements. A colonist or pioneer of a nation that has met
    // the tribe may live among the natives to learn their skill.
    bool canLiveAmongNatives(int unit, int settlement) const;
    struct NativeLesson {
        std::string section;              // LEARNSTAY (an offer), LEARNMAD, ...
        std::vector<std::string> strings; // the tribe, the skill
        int skill = -1;                   // the village's skill (row of @JOB)
    };
    // The unit goes in; its turn is over. LEARNSTAY asks whether to learn.
    NativeLesson liveAmongNatives(int unit, int settlement);
    // The answer "Then I shall become a master ...": the unit learns.
    void learnFromNatives(int unit, int settlement);
    // A missionary may found a mission at a settlement without one.
    bool canEstablishMission(int unit, int settlement) const;
    struct MissionNews {
        int reaction = 0; // 0 curious, 1 cautious, 2 offended, 3 hostile (MISSION0..3)
        int colony = -1;  // the nation's nearest colony, which names the mission
    };
    // The missionary stays in the settlement (the unit is gone) and the
    // tribe's tension moves with how it takes the mission.
    MissionNews establishMission(int unit, int settlement);
    // Trade with a settlement: a wagon train or a ship of a nation that has
    // met the tribe. Read from the original; see docs/rules/natives.md.
    bool canTradeWithVillage(int unit, int settlement) const;
    struct VillageSale {
        std::string refusal;        // BADCARGO or BADHAGGLE1; empty: an offer
        std::array<int, 3> wants{}; // what the village asks for instead
        int hold = -1;
        int good = -1;
        int amount = 0;
        int price = 0;   // the offer
        int ask = 0;     // the "fairer price" the player may ask
        int quality = 0; // row of @VALUES: low quality ... excellent
        int calm = 0;    // how far a sale calms the tribe
        int haggles = 0; // 0: TRADE0, then TRADE1
        int temper = 0;  // the tribe's tension step as the price weighs it
    };
    // The village's offer for the goods in one hold. The unit's turn is over.
    VillageSale offerToVillage(int unit, int settlement, int hold);
    void sellToVillage(int unit, int settlement, const VillageSale& sale);
    // "A fairer price would be ...": true when the village raises its offer,
    // false when it has had enough (it will not take the good again).
    bool haggleSale(int unit, int settlement, VillageSale& sale);
    void giveToVillage(int unit, int settlement, const VillageSale& sale);
    // After a sale: room for goods, what the village still asks for, and
    // whether it refuses to sell after haggling.
    bool hasRoomToBuy(int unit) const;
    bool refusesToSell(int settlement) const;
    // The goods the village sells (up to three, rows of @CARGO).
    std::vector<int> villageGoodsForSale(int settlement) const;
    struct VillagePurchase {
        int good = -1;
        int amount = 0;
        int price = 0;
        int haggles = 0; // 0: BUY0, then BUY1
    };
    // `sold` is the amount of the last sale: a wagon train is filled with as
    // much, a ship with a quarter.
    VillagePurchase offerFromVillage(int unit, int settlement, int good, int sold);
    // False when the treasury is too small (NOTENOUGH).
    bool buyFromVillage(int unit, int settlement, const VillagePurchase& purchase);
    // True when the price comes down; false when the village refuses to
    // sell (BADHAGGLE2).
    bool hagglePurchase(int unit, int settlement, VillagePurchase& purchase);
    // A scout may ask to speak with the chief of a tribe its nation has met.
    bool canSpeakWithChief(int unit, int settlement) const;
    enum class ChiefAnswer { Kill, Bored, Guides, Tales, Gift };
    struct ChiefTalk {
        bool welcome = false;      // CHIEFHOWDY comes first
        int skill = -1;            // what the village teaches (row of @JOB)
        std::array<int, 3> wants{}; // the goods asked for (rows of @CARGO)
        ChiefAnswer answer = ChiefAnswer::Bored;
        int gold = 0; // the gift
    };
    // The scout goes in; its turn is over. A killed scout is gone, a guided
    // one is a seasoned scout, tales show the land around, a gift is paid.
    ChiefTalk speakWithChief(int unit, int settlement);
    // Any land unit that can attack, except a missionary, may demand
    // tribute from a tribe its nation has met.
    bool canDemandTribute(int unit, int settlement) const;
    struct Tribute {
        // EXTORTLAUGH, EXTORTNO, EXTORTPOOR or EXTORTSTUFF.
        enum class Reply { Laugh, Refuse, Poor, Goods };
        Reply reply = Reply::Poor;
        int colony = -1; // the nation's nearest colony in the region
        int good = -1;   // what the village hands over (row of @CARGO)
        int amount = 0;
    };
    // The unit's turn is over. Goods go to the colony's warehouse.
    Tribute demandTribute(int unit, int settlement);
    // Counts each nation's forces by region, as the original does at the
    // end of the player's turn; the tribute's answer weighs them.
    void countForces();
    // A European nation the computer plays takes its turn: it counts its
    // forces and people (kept in the save as the original keeps them), and
    // buys in Europe. See docs/rules/european-ai.md.
    void europeanTurn(int nation);
    void nationCensus(int nation);
    // How many more colonies the nation wants, from its last count.
    int aiColonyNeed(int nation) const;
    // Every profession but free colonists, servants, criminals and converts.
    static bool isExpert(int profession);
    // A unit's attack (or defence) in eighths, as the count weighs it.
    int unitForce(const SaveUnit& u, bool attacking) const;
    // Any land unit with an attack value may attack a village, met or not.
    bool canAttackVillage(int unit) const;
    // Attacking makes war on the tribe: the war bit on both sides, the
    // tribe 10 and twice the difficulty level more tense, and the
    // village's alarm up.
    void attackVillageWar(int unit, int settlement);
    // Who defends the village: a native unit on its square, or else -1 and
    // one of its people stands up as a brave (armed when the tribe has
    // muskets, mounted when it has 25 horses).
    int villageDefenderUnit(int settlement, int attacker) const;
    SaveUnit villageBrave(int settlement) const;
    CombatAnalysis analyseVillageAttack(int attacker, int settlement) const;
    CombatStrengths villageStrengths(int attacker, int settlement, bool beforeLevels = false) const;
    // Fights it out. A won fight against the village's own people costs it
    // a person, and the last one burns it: treasure, the mission's
    // missionary back, converts for the mission's nation. The attacker's
    // turn is over; notices tell what happened.
    CombatOutcome attackVillage(int attacker, int settlement);

    // First contact: a unit standing on land next to land with a unit or
    // settlement of a tribe its nation has not met yet meets that tribe
    // (and a native unit meets a European nation the same way). The tribe
    // offers peace: a computer nation takes it at once; the player is asked
    // with an INDIANWELCOME notice. Returns whether the player is asked.
    bool meetNeighbours(int unit);
    // The step a native unit chooses in its tribe's turn: a direction from
    // north clockwise (0-7), 8 to stay (its moves are then used up), or -1
    // when it had no home and is gone. Sets its orders, last step and visit
    // marks as the original does. See docs/rules/natives.md.
    int nativeUnitAI(int index);
    // A native unit attacks the square (x, y): the unit or colony there.
    // The outcome as in the original: an ambush, a raid on the colony, a
    // colonist killed or the colony burned. See docs/rules/natives.md.
    CombatOutcome nativeAttack(int index, int x, int y);
    // The tribe's (or nation's) first meeting: both sides have met, and the
    // tribe's tension is at most 20.
    void meetTribe(int nation, int tribe);
    // The player's answer to a tribe's welcome. Yes makes peace (notices
    // INDIANPEACE and INDIANCOME); no leaves the tribe 100 tense (notice
    // INDIANSHUN).
    void answerWelcome(int tribe, bool accept);
    // The original's one way to make a tribe more or less tense toward a
    // nation: rises are halved for the French and again with Pocahontas;
    // the result stays within 0 to 100. A fall ends a war and calms the
    // tribe's settlements. At 100 a tribe with a treaty may burn the
    // nation's missions.
    void changeTension(int tribe, int nation, int amount);
    // Takes the nation's missions out of the tribe's settlements (notice
    // INDIANBURN to the human when there were any).
    void burnMissions(int tribe, int nation);

    // A brave comes to stand next to a nation's colony, wagon train or unit
    // (the square in `direction` from it) and visits: the tribe may beg for
    // food, bring food or goods as gifts, send converts, demand goods or
    // only speak (notices INDIANBEGFOOD, INDIANGIVEFOOD, INDIANGIVESTUFF,
    // INDIANSCONVERT, INDIANCITY, INDIANWAGONS, INDIANCOMMENT). True when
    // something happened; the brave's turn is then over. Read from the
    // original; see docs/rules/natives.md ("Braves visit").
    bool braveVisit(int brave, int direction, int nation);
    // The player's answer to the oldest visit that waits on one (begging,
    // or a demand for goods): true gives what was asked.
    void answerVisit(bool give);
    bool visitWaiting() const { return !visits_.empty(); }

    // A European unit that has come onto a lost city rumour explores it
    // (notices LOSTCITY1 to LOSTCITY9; read from the original, see
    // docs/rules/rumours.md). False when the unit vanished.
    bool exploreRumour(int index);
    // The player's answer to the burial mounds (LOSTCITY4): search them or
    // stay clear (notices BURIAL1 to BURIAL3, SCREWED).
    void answerBurial(bool search);
    bool burialWaiting() const { return burial_.nation >= 0; }
    // A Fountain of Youth's free recruit from the docks (LOSTCITY0, one of
    // eight); the new unit, or -1.
    int recruitFree(int slot);

    // A missionary incites a tribe against another European nation. The
    // price weighs the tribe's tension toward the missionary's nation, its
    // settlements, defence, muskets and horses; the French pay a third
    // less, and missions, a Jesuit and a capital make it cheaper (at least
    // 500).
    int incitePrice(int unit, int settlement) const;
    // Read from the original: a missionary at a tribe with a treaty is
    // always offered Incite Indians.
    bool canIncite(int unit, int settlement) const;
    // The nations a tribe can be incited against: the other Europeans.
    std::vector<int> inciteTargets(int unit) const;
    enum class Incite { NoContact, TooPoor, AlreadyAtWar, Done };
    // Checks and, when the price is paid, does it: the tribe 100 more tense
    // toward the target (halved as changeTension does) and the price off
    // the treasury. Notices tell the answer.
    Incite incite(int unit, int settlement, int target, bool pay);

    // The European pressure on a native settlement, as the original weighs
    // it: each colony within 6 squares by its buildings, people and
    // distance (the player's by difficulty), plus that nation's armed
    // units within two squares, halved for the French and with Pocahontas;
    // a mission moves it. Returns the nation pressing hardest (or -1) and
    // sets its pressure.
    int settlementPressure(int settlement, int* pressure = nullptr) const;
    // A missionary at a tribe with a treaty may denounce another nation's
    // mission there.
    bool canDenounce(int unit, int settlement) const;
    // The missionary is gone either way. On success (notice HERESY0) the
    // mission becomes the nation's; otherwise (HERESY1) the missionary is
    // burned. The tribe's tension toward both nations moves.
    bool denounceHeresy(int unit, int settlement);

    // A ship that lost a fight and is being repaired (bit 0x80 of byte 4).
    bool isDamagedShip(int index) const;
    // A ship's odds in getting away from a stronger one: its share of the
    // two ships' values decides.
    int evasion(int ship) const;
    // The attacker wins with a roll of 1 to attack + defense at or below
    // attack.
    static bool attackerWins(int attack, int defense, int roll);
    // The winner of a fight may harden: a free colonist soldier or dragoon
    // becomes a veteran, a servant or criminal moves up one rank. `own`
    // and `opponent` are the two numbers the fight was rolled on.
    void promote(int unit, int own, int opponent);
    // A founding father in the nation's congress (row of @FATHERS).
    bool hasFather(int nation, int father) const;

    // --- Continental Congress (docs/rules/congress.md) -----------------------
    static constexpr int kFoundingFathers = 25;
    static constexpr int kFatherCategories = 5; // trade, exploration, military, political, religious
    // Each colony's bells go to its nation's congress at the start of its
    // turn: the next father is put up for election when there is none,
    // and joins when the bells are enough.
    void addCongressBells(int nation, int bells);
    int congressBells(int nation) const;
    int bellsForNextFather(int nation) const;
    int fatherInElection(int nation) const; // -1 for none
    int fatherCount(int nation) const;
    // The nation's people as the original counts them: everyone in its
    // colonies and every unit that is a colonist under arms, tools or a
    // habit, at most 255.
    int nationPeople(int nation) const;
    bool independenceDeclared() const;
    // The player picks the next father (notice WHICHFREEDOM): one
    // candidate per category, -1 where there is none.
    bool fatherElectionWaiting() const { return electionNation_ >= 0; }
    const std::array<int, kFatherCategories>& fatherCandidates() const { return fatherCandidates_; }
    void chooseFather(int category);

    // --- Map -----------------------------------------------------------------
    void reveal(int x, int y, int radius, int nation);
    // What a chief's tales show: the land of the same landmass within the
    // radius, and every square next to (x, y).
    void revealLand(int x, int y, int radius, int nation);
    int sightRadius(int index) const;

private:
    void refreshUnitFlags();
    bool workDone(int index) const;
    void finishWork(int index);
    void sailOn(); // ships between Europe and the New World move on a stage
    void chainPassengers(int ship, const std::vector<int>& passengers);
    void removeUnit(int index);
    int addUnitInEurope(int type, int profession, int nation = -1);
    // The original holds at most 300 units and makes no more past that.
    static constexpr int kMaxUnits = 300;
    // A new unit of the tribe on (x, y); its index, or -1 if there is no
    // room for it.
    int addNativeUnit(int type, int tribe, int x, int y);
    // The natives' part of the turn: each tribe that is left looks after
    // its settlements, uses up goods, breeds horses and counts its forces.
    void nativesTurn();
    void tribeTurn(int tribe);
    void settlementTurn(int settlement);
    void countTribe(int tribe);
    // The tribe's units take their steps, one unit at a time.
    void moveTribeUnits(int tribe);
    void nativeUnitTurn(int index);
    int stepToward(int index, int toX, int toY);
    struct RouteState {
        std::array<std::uint8_t, 256> grid{}; // costs over the 16 x 16 squares around the key
        int keyX = -1;                        // the goal the grid was made for
        int keyY = -1;
        int best = 0; // the start's cost after a flood
        int targetX = 0;
        int targetY = 0;
        int startBlockX = 0; // the mover's block from the last block search
        int startBlockY = 0;
        int type = 0;    // the mover's type as the finder sees it
        int nation = -1; // whose units and towns are not in the way
        bool roads = false; // every step counts as a road
    };
    RouteState route_;
    int routeFlood(int sx, int sy, int limit);
    bool routeCoarse(int x0, int y0, int x1, int y1);
    bool routeBlock(int x, int y, bool ship);
    bool routeBlockSquare(int x0, int y0, bool ship, int& x, int& y) const;
    int routeReach(int fromX, int fromY, int toX, int toY, bool ship, int limit);
    std::uint8_t routeBlockLinks(bool ship, int bx, int by) const;
    int routeEnemyLand(int x, int y, int nation) const;
    int routeTerrainCost(int x, int y) const;
    bool routeOneMove() const;
    int randomRecruit(int nation);
    void gatherCrosses(); // immigration at the end of the turn
    // What the computer's nations plan with, kept in memory only, as the
    // original keeps it.
    struct EuroAiState {
        std::array<int, 16> wants{};       // colonies asking for each good, less what is on the way
        std::array<int, 16> wantsBefore{}; // the same before the ships' cargo
        std::array<int, 4> marked{};       // colonies marked for recruits
        std::array<int, 4> inPort{};       // land units on the docks
        std::array<int, 4> homeward{};     // ships on the way to Europe
        int fortifiedRegions = 0;
        int privateerThreat = 0; // colonies with armed ships of others near
        int frigateThreat = 0;   // the same with a frigate
        int privateerPeople = 0;
        int frigatePeople = 0;
        int openRegions = 0; // regions of 8 squares or more without a colony of its own
        // The plan for the units: places worth going to, each with a kind
        // (what may go there) and a weight.
        struct Target {
            int x = 0;
            int y = 0;
            int kind = -1; // -1: an empty entry
            int prio = 0;
        };
        std::array<std::array<Target, 64>, 4> targets{};
        std::array<int, 64> load{}; // units already sent to each target, from loadBase
        int loadBase = 3;
        // Colonies with goods to fetch, best first (for all nations at once,
        // as in the original).
        struct Pickup {
            int colony = -1;
            int value = 0;
            int loads = 0;
            int military = 0;
        };
        std::array<Pickup, 16> pickups{};
        std::array<std::uint8_t, 270> blocks{};       // squares of 4 x 4 with own units or colonies
        std::array<int, 16> founders{};               // units looking for a site, by region
        std::array<int, 16> foreignPeople{};          // the largest foreign colony, by region
        std::array<std::array<int, 16>, 8> tribePeople{};
        std::array<std::array<int, 16>, 4> status{};  // what each region is to the nation (kept between turns)
        std::array<int, 4> pickupCalls{};
        int biggerRegions = 0; // regions where another nation is ahead (bits)
        int emptyRegions = 0;  // regions without a colony of the nation (bits)
        bool shipSent = false; // a ship was sent to Europe for recruits this turn
    };
    EuroAiState ai_;
    int aiRandom(int low, int high);
    void aiTallies(int nation);
    void aiRelations(int nation);
    void aiEurope(int nation);
    int aiCreateInEurope(int nation, int type, int profession);
    bool aiBuyItem(int nation, int item);
    int aiRecruitSlot(int nation, int slot);
    void aiMarket(int nation, int good, int amount);
    void aiMarketBack(int nation, int good, int amount);
    bool aiHasCollege(int nation) const;
    void aiSwapExpert(int nation, int unit);
    void aiSailFromEurope(int nation, int ship);
    // The units on the map (euro_ai_units.cpp).
    void aiPlan(int nation);
    void aiMoveUnits(int nation);
    bool aiUnitStep(int nation, int unit);
    int aiDecide(int nation, int unit);
    void aiExecute(int nation, int unit);
    void aiGoToStep(int nation, int unit);
    void aiAddTarget(int nation, int x, int y, int kind, int prio);
    int aiTargetPrio(int nation, int x, int y, int kind) const;
    void aiAddPickup(int colony, int value, int loads, int military);
    int aiStack(int unit, int category) const;
    int aiHostile(int nation, int other, bool tribes, int unit) const;
    int aiThreatNear(int x, int y, int nation, bool tribes) const;
    bool aiForeignNear(int x, int y, int nation) const;
    int aiRegionScore(int nation, int region) const;
    int aiFoundBias(int nation, int unit, int region) const;
    int aiFoundScore(int nation, int unit) const;
    int aiStepAround(int nation, int x, int y, bool score, bool artillery) const;
    int aiRelation(int nation, int other) const;
    int aiAttackOdds(int unit, int x, int y);
    void aiAttack(int unit, int x, int y);
    void aiColonyPlan(int colony);
    void aiTakeIntoColony(int unit, int colony);
    void aiSendOut(int colony, int colonist, int role);
    bool aiRoadToColony(int colony);
    bool aiImproveLand(int colony, int region);
    // The tribe that claims square k around a colony (-1 for none).
    int aiTribeClaim(const SaveColony& colony, int k, int region) const;
    // What a colony's later plans weigh from the earlier ones.
    struct AiColonyView {
        int region = -1;
        int tribe = -1;    // the nearest tribe in its land
        int guards = 0;    // guards it still wants
        int adjacent = 0;  // enemy units right next to it
        int squares = 8;   // squares it can work
        int water = 0;     // of those, water or off the map
        int ocean = 0;     // of those, ocean (docks or not)
        int tories = 0;    // colonists not in the Sons of Liberty
        // Its colonists by skill (those who are no experts as free
        // colonists, 19), counted before anyone comes or goes.
        std::array<int, 32> skills{};
    };
    void aiAssignJobs(int colony, AiColonyView& view);
    void aiTeach(int colony, const AiColonyView& view);
    void aiTrain(int colony);
    // The nations from weakest (0) to strongest (3), as the plans rank
    // them; for a nation outside 0-3, the player's rank.
    int aiRank(int nation) const;
    void aiChooseConstruction(int colony, const AiColonyView& view);
    bool aiCanBuild(int colony, int row) const;
    bool aiTryBuild(int colony, int row);
    void aiGoTo(int unit, char goal, int x, int y);
    void aiToEurope(int unit);
    void aiEndMoves(int unit);
    void aiJoinColony(int unit, int colony);
    void aiBoard(int unit, int ship);
    std::string aiColonyName(int nation) const;
    std::array<std::vector<std::string>, 4> colonyNames_{};

    SaveGame save_;
    Ruleset rules_;
    std::vector<std::string> lastTurnNews_;
    std::vector<GameNotice> notices_;
    // A brave's visit, while it waits on the player's answer.
    struct BraveVisitState {
        int nation = -1;
        int tribe = -1;
        int home = -1;   // the brave's settlement
        int colony = -1; // the colony it visits, if any
        int wagon = -1;  // the wagon train it visits, if any
        int brave = -1;
        int kind = 0; // what waits on the answer: 0 begging, 1 a demand on the colony, 2 on the wagon train
        int good = -1;
        int amount = 0;
        int score = 0;
        bool friendly = false;
        bool calm = false; // the roll for a friendly visit came out so
        bool begged = false;
        std::array<int, 16> wants{};
        std::array<int, 16> makes{};
    };
    std::vector<BraveVisitState> visits_;
    // Burial mounds waiting on the player's answer.
    struct Burial {
        int nation = -1;
        int x = 0;
        int y = 0;
        int scout = 0;   // 1 for a scout, 2 seasoned, one more with de Soto
        int quality = 0; // the roll of the exploration
    };
    Burial burial_;
    void searchBurial(const Burial& b);
    // The Continental Congress.
    int fatherEra() const;
    int fathersAvailable(int nation, int category) const;
    int aiFatherCategory(int nation) const;
    void electFathers(int nation);
    void joinCongress(int nation, int father);
    std::array<int, kFatherCategories> fatherCandidates_{-1, -1, -1, -1, -1};
    int electionNation_ = -1;
    // The original's counts since the game was started (not saved): the
    // rumours explored and the cities of gold found.
    int rumoursExplored_ = 0;
    int citiesFound_ = 0;
    int addFreeRecruit(int nation, int slot);
    // The parts of a visit after the begging, and the demands.
    void visitAfterBegging(BraveVisitState v, bool give);
    bool visitOn(BraveVisitState v);
    bool visitDemand(BraveVisitState v);
    bool visitWagons(BraveVisitState v);
    void answerDemand(const BraveVisitState& v, bool give);
    // The messages about a colony's warehouse after its turn.
    void storageNotices(const ColonyTurn& turn, int colony);
    void foodNotices(const ColonyTurn& turn, int colony);
    // A colony out of food may lose a colonist; returns true when it had
    // only one and is gone.
    bool starve(const ColonyTurn& turn, int colony);
    void removeColony(int colony);
    void runOutNotices(const ColonyTurn& turn, int colony);
    std::uint32_t random_ = 0;
    int nextRandom(int range); // 0 .. range - 1
    // True when the loser is gone (unit numbers after it move down).
    bool loseBattle(int loser, int winner);
    CombatStrengths strengthsAgainst(int attacker, const SaveUnit& defender, bool defenderShip, bool defenderDamaged,
                                     int defenderExtra, bool beforeLevels = false) const;
    // The defender's part of it alone: its strength with the bonus of where
    // it stands, before the human's bonus and the rest.
    int defenceStrength(int attacker, const SaveUnit& defender, bool defenderShip, bool defenderDamaged,
                        int defenderExtra) const;
    SaveUnit colonistAsUnit(int colony, int colonist) const;
    void captureColony(int colony, int attacker);
    void loseShip(int loser, int winner);
    void repairShips();
    int addColonyBonus(CombatSide& side, int colony) const; // its quarters
    int settlementQuarters(int settlement) const;
    void loseNative(int loser);
    void winAgainstVillage(int attacker, int settlement);
    void burnSettlement(int settlement);
    // What a tribe's raiding party does to a colony after its fight there.
    void raidColony(int nation, int colony, int home, bool always);
    int villageTreasure(int nation, int tribe, bool capital);
};

} // namespace viceroy
