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
#include "viceroy/rules/ruleset.hpp"

#include <array>
#include <optional>
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

// What a colonist of this skill makes doing this job on a square.
int squareYield(const Ruleset& rules, const MapSquare& square, int jobRow, int skill);

ColonyProduction colonyProduction(const SaveColony& colony, const SaveMap& map, const Ruleset& rules);

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
                                  const std::vector<SaveColony>& colonies, int colonist);

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
};

// How much of each good the warehouse holds (food keeps up to 200).
int storageLimit(const SaveColony& colony);

// Stores what the colony made this turn: workshops use what is there, food
// is eaten, hammers go into the building under way, and the warehouse
// throws away what it cannot hold.
ColonyTurn runColonyTurn(SaveColony& colony, const SaveMap& map, const Ruleset& rules);

// The share of the colony in the Sons of Liberty, 0-100.
int sonsOfLibertyPercent(const SaveColony& colony);

} // namespace viceroy
