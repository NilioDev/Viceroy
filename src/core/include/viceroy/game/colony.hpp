// SPDX-License-Identifier: GPL-3.0-or-later
//
// A colony's buildings and what its people produce.
//
// The savegame keeps the buildings as 48 bits: each kind of building has a
// field whose bits fill up from the bottom as it is improved (0, 1, 11,
// 111), so its level is the number of bits set.
//
// Production follows docs/rules/production.md. Parts of it are still
// measured guesses; that file says which.
#pragma once

#include "viceroy/data/savegame.hpp"
#include "viceroy/game/resource.hpp"
#include "viceroy/rules/ruleset.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace viceroy {

enum class BuildingKind {
    Fortification, // stockade, fort, fortress
    Armory,
    Docks,
    TownHall,
    Schoolhouse,
    Warehouse,
    Unused1,
    Stables,
    CustomHouse,
    PrintingPress,
    Weaver,
    Tobacconist,
    Distiller,
    Capitol,
    FurTrader,
    Carpenter,
    Church,
    Blacksmith,
    Count,
};

inline constexpr int kBuildingKinds = static_cast<int>(BuildingKind::Count);

struct BuildingField {
    int shift;
    int bits;
    int firstRow; // the first level's row in @BUILDING
};

// In the order of BuildingKind.
inline constexpr std::array<BuildingField, kBuildingKinds> kBuildingFields{{
    {0, 3, 0},   // stockade, fort, fortress
    {3, 3, 3},   // armory, magazine, arsenal
    {6, 3, 6},   // docks, drydock, shipyard
    {9, 3, 9},   // town hall ×3
    {12, 3, 12}, // schoolhouse, college, university
    {15, 1, 15}, // warehouse (the expansion is kept elsewhere)
    {16, 1, -1},
    {17, 1, 17}, // stable
    {18, 1, 18}, // custom house
    {19, 2, 19}, // printing press, newspaper
    {21, 3, 21}, // weaver's house, shop, textile mill
    {24, 3, 24}, // tobacconist's house, shop, cigar factory
    {27, 3, 27}, // rum distiller's house, distillery, factory
    {30, 2, 30}, // capitol (never built in the original)
    {32, 3, 32}, // fur trader's house, post, factory
    {35, 2, 35}, // carpenter's shop, lumber mill
    {37, 2, 37}, // church, cathedral
    {39, 3, 39}, // blacksmith's house, shop, iron works
}};

// 0 when not built, else 1 to 3.
int buildingLevel(const SaveColony& colony, BuildingKind kind);
void setBuildingLevel(SaveColony& colony, BuildingKind kind, int level);

// Goods by their row in @CARGO, plus the three that are not stored.
namespace cargo {
inline constexpr int kFood = 0;
inline constexpr int kSugar = 1;
inline constexpr int kTobacco = 2;
inline constexpr int kCotton = 3;
inline constexpr int kFurs = 4;
inline constexpr int kLumber = 5;
inline constexpr int kOre = 6;
inline constexpr int kSilver = 7;
inline constexpr int kHorses = 8;
inline constexpr int kRum = 9;
inline constexpr int kCigars = 10;
inline constexpr int kCloth = 11;
inline constexpr int kCoats = 12;
inline constexpr int kTradeGoods = 13;
inline constexpr int kTools = 14;
inline constexpr int kMuskets = 15;
inline constexpr int kHammers = 16;
inline constexpr int kCrosses = 17;
inline constexpr int kBells = 18;
inline constexpr int kCount = 19;
} // namespace cargo

// Jobs by their row in @JOB.
namespace job {
inline constexpr int kFarmer = 0;
inline constexpr int kFisherman = 8;
inline constexpr int kDistiller = 9;
inline constexpr int kCarpenter = 13;
inline constexpr int kGunsmith = 15;
inline constexpr int kPreacher = 16;
inline constexpr int kStatesman = 17;
inline constexpr int kTeacher = 18;
} // namespace job

// Skills of colonists in a colony that are not experts.
namespace skill {
inline constexpr int kFreeColonistUnit = 19; // as units store it
inline constexpr int kServant = 25;
inline constexpr int kCriminal = 26;
inline constexpr int kConvert = 27;
inline constexpr int kFreeColonist = 28;
} // namespace skill

// The eight squares around a colony in the order the savegame lists who
// works them: N, E, S, W, NW, NE, SE, SW.
struct SquareOffset {
    int dx;
    int dy;
};
inline constexpr std::array<SquareOffset, 8> kColonySquares{
    SquareOffset{0, -1}, SquareOffset{1, 0},  SquareOffset{0, 1},  SquareOffset{-1, 0},
    SquareOffset{-1, -1}, SquareOffset{1, -1}, SquareOffset{1, 1}, SquareOffset{-1, 1}};

// Where a colonist works and what they make there.
struct ColonistWork {
    int square = -1;                    // 0-7 into kColonySquares, or -1
    std::optional<BuildingKind> building; // when working inside
    int good = -1;                      // what they make (cargo::), -1 nothing
    int amount = 0;                     // per turn, before any shortage
    int uses = 0;                       // raw goods a workshop needs for it
};

struct ColonyProduction {
    // The colony's own square: food and one other good.
    int centreFood = 0;
    int centreGood = -1;
    int centreAmount = 0;
    std::vector<ColonistWork> colonists; // one per colonist, in order
    // Totals per turn, by cargo:: index.
    std::array<int, cargo::kCount> produced{}; // made from the land or by people
    std::array<int, cargo::kCount> needed{};   // raw goods the workshops want
    int foodEaten = 0;
};

// The row of the terrain tables (@UNFORESTED, @FORESTED, @OTHER) for a square.
int terrainRow(const MapSquare& square);

// How the Sons of Liberty change what each colonist makes: a bonus of 1
// or 2 once membership has reached 50% or 100%, and a penalty of 1 for
// every (10 - difficulty) tories.
struct WorkModifier {
    int bonus = 0;
    int penalty = 0;
};
WorkModifier workModifier(const SaveColony& colony, int difficulty);

// What a colonist of this skill makes doing this job on a square. Fishing
// gives 1 more than the yield where the water has land on at least 3 of its
// 8 sides, and 1 less elsewhere.
// A resource on the square (resource.hpp) adds to the job it suits.
int squareYield(const Ruleset& rules, const MapSquare& square, int jobRow, int skill, WorkModifier modifier = {},
                bool nearLand = true, Resource resource = Resource::None);
// What a worker in a building makes (level 1 house, 2 shop, 3 factory);
// without the factory's extra half when withFactory is false, which is
// what the worker uses of its raw good.
int buildingYield(int level, int skill, int jobRow, WorkModifier modifier = {}, bool withFactory = true);
// How many of the 8 squares around are land.
int landAround(const SaveMap& map, int x, int y);

ColonyProduction colonyProduction(const SaveColony& colony, const SaveMap& map, const Ruleset& rules,
                                  int difficulty = 0);

// The building a job is done in, if any.
std::optional<BuildingKind> buildingForJob(int jobRow);
// What a job makes, and what it uses up (-1 for nothing).
int jobOutput(int jobRow);
int jobInput(int jobRow);

// --- Putting people to work ----------------------------------------------

// A line of the jobs menu: what this colonist would make doing the job at
// the best free place for it, or 0 when there is none.
struct JobChoice {
    int job = 0;
    int amount = 0;
    int square = -1; // where (0-7), for work on the land
};

// Jobs a colonist can do in the colony, in the order of @JOB: all work on
// the land, and work in the buildings the colony has (teachers excepted),
// with what they would make.
std::vector<JobChoice> jobChoices(const SaveColony& colony, const SaveMap& map, const Ruleset& rules,
                                  const std::vector<SaveColony>& colonies, int colonist, int difficulty = 0);

// Moves a colonist to a job at its best free place. Returns false (and
// changes nothing) when there is no place for it.
bool assignJob(SaveColony& colony, const SaveMap& map, const Ruleset& rules,
               const std::vector<SaveColony>& colonies, int colonist, int jobRow);

// A square around the colony that nobody else works and that the colony may
// use (water needs docks).
bool squareFree(const SaveColony& colony, const SaveMap& map, const std::vector<SaveColony>& colonies, int square);

// --- The end of a turn ---------------------------------------------------

struct ColonyTurn {
    bool grew = false;    // 200 food: a new colonist is born
    bool starved = false; // no food left: a colonist died
    int built = -1;       // row of @BUILDING finished this turn
    std::array<int, kGoodsKinds> wasted{}; // goods the warehouse could not hold
    int libertyBefore = 0; // the production bonus before and after the turn
    int libertyAfter = 0;
    int horsesBred = 0;    // new horses, each fed with one food of the surplus
    int builtUnit = -1;    // row of @UNIT finished this turn (wagon, artillery, ships)
    int toolsNeeded = 0;   // the hammers are there but the tools are not: how many
};

// What the colony builds is a building (row of @BUILDING, 0-41) or, from
// 42 on, a unit: 42 + n is @UNIT row 11 + n (artillery, wagon train,
// caravel, merchantman, galleon, privateer, frigate). Measured: the frigate
// is offered with a shipyard but never finishes, and nothing higher exists.
inline constexpr int kFirstUnitConstruction = 42;
inline constexpr int kUnitConstructionRows = 7;
inline constexpr int kFrigateConstruction = 48;
// The unit a construction value stands for, or -1.
int constructionUnit(int construction);
// Hammers and tools a construction needs (measured for units: the @UNIT
// cost column times 32, a wagon train 40; tools ten times the column).
int constructionHammers(const Ruleset& rules, int construction);
int constructionTools(const Ruleset& rules, int construction);
// Its name, as the original's messages give it.
std::string constructionName(const Ruleset& rules, int construction);

// A line of the "Select An Item To Build" list.
struct BuildOption {
    int construction = -1; // byte 148 value
    bool complete = false; // the colony is building it but already has it
};
// What the colony may build, in the original's order (buildings by row,
// then units), measured on its colony screen; see docs/rules/colony-screen.md.
std::vector<BuildOption> buildOptions(const SaveColony& colony, const SaveMap& map, const Ruleset& rules);

// What BUY on the Make panel asks to finish the construction now, or 0 when
// there is nothing to buy. See docs/rules/production.md.
int constructionPrice(const SaveColony& colony, const Ruleset& rules, const SaveNation& nation);

// The food the colony's own square makes without the liberty bonus.
int centreFoodYield(const Ruleset& rules, const MapSquare& square, int difficulty);

// How much of each good the warehouse holds (food keeps up to 200).
int storageLimit(const SaveColony& colony);

// How many horses the colony breeds from the food left over after the
// colonists have eaten (see docs/rules/production.md).
int horsesBred(const SaveColony& colony, int foodSurplus);

// Stores what the colony made this turn: workshops use what is there, food
// is eaten, hammers go into the building under way, and the warehouse
// throws away what it cannot hold.
ColonyTurn runColonyTurn(SaveColony& colony, const SaveMap& map, const Ruleset& rules, int difficulty = 0);

// --- Sons of Liberty -----------------------------------------------------

// The share of the colony in the Sons of Liberty, 0-100.
int sonsOfLibertyPercent(const SaveColony& colony);
// How many of the colonists are rebels (the rest are tories).
int rebelCount(const SaveColony& colony);
// One turn's change of membership: the bells come in, both sides of the
// fraction wear away by a 64th, and the rebels by a fifth of their share.
void gatherBells(SaveColony& colony, int bells);
// The production bonus moves one step a turn towards what membership
// allows (1 at 50%, 2 at 100%).
void updateLibertyBonus(SaveColony& colony);
// A new colony starts at 0 of 200 and gathers its first bells at once.
void startLiberty(SaveColony& colony, const SaveMap& map, const Ruleset& rules, int difficulty);
// A colonist joining adds 100 to the total and the colony gathers again.
void joinLiberty(SaveColony& colony, const SaveMap& map, const Ruleset& rules, int difficulty);

} // namespace viceroy
