// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's rule tables, read from NAMES.TXT of the player's own copy (or,
// later, from a pack). See docs/rules/names.md for what each value means.
#pragma once

#include "viceroy/data/text_table.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace viceroy {

// The kinds of work a colonist can do on a land or water square, in the
// order the terrain tables list their yields.
enum class SquareWork : int {
    Farmer,
    SugarPlanter,
    TobaccoPlanter,
    CottonPlanter,
    FurTrapper,
    Lumberjack,
    OreMiner,
    SilverMiner,
    Fisherman,
};
inline constexpr std::size_t kSquareWorkCount = 9;

struct TerrainType {
    enum class Group { Unforested, Forested, Other };
    std::string name;
    Group group = Group::Unforested;
    int movementCost = 0;
    int defenseBonus = 0;    // as listed; see docs/rules/names.md
    int improvementTurns = 0; // effort to plow or clear, as listed
    int value = 0;
    std::array<int, kSquareWorkCount> yield{};
};

struct BuildingType {
    std::string name;
    int hammers = 0;
    int tools = 0;        // the file lists tens of tools; this is the full number
    int slot = 0;         // the "size" column
    int minColonySize = 0;
    int upkeep = 0;
};

struct JobType {
    std::string name;       // "Farmer"
    std::string expertName; // "Expert Farmers"
    int skillLevel = 0;     // 4 means it cannot be taught
    int europeCost = -1;    // -1: cannot be bought in Europe
};

struct CargoType {
    std::string name;
    bool tradable = false; // hammers, crosses, bells and flags are not
    int startPriceLow = 0;
    int startPriceHigh = 0;
    int driftLow = 0;
    int driftHigh = 0;
    int burden = 0;
    int rise = 0;
    int fall = 0;
    int attrition = 0;
    int volatility = 0;
};

struct UnitType {
    std::string name;
    int icon = 0;
    int movement = 0;
    int attack = 0;
    int combat = 0;
    int cargoSlots = 0;
    int size = 0;
    int cost = 0;
    int tools = 0;
    int guns = 0;
    int hull = 0;
    std::uint8_t aiRoles = 0; // bit 7 = invade ... bit 0 = naval
};

struct EuropeanClass {
    std::string name;
    int transportCost = 0;
};

struct FoundingFather {
    std::string name;
    int category = 0;
    std::array<int, 3> weight{}; // per century: 1500s, 1600s, 1700s and later
};

struct Tribe {
    std::string name;
    std::string adjective;
    std::string treasure;
    int level = 0;
    int colour = 0;
};

struct Ruleset {
    std::vector<TerrainType> terrain;
    std::vector<BuildingType> buildings;
    std::vector<JobType> jobs;
    std::vector<CargoType> cargo;
    std::vector<UnitType> units;
    std::vector<EuropeanClass> classes;
    std::vector<FoundingFather> fathers;
    std::vector<Tribe> tribes;
    // The names of the republics a nation becomes when its King lets its
    // colonies go (@INDEPENDENT, by nation); empty if the section is missing.
    std::vector<std::string> independent;
};

// Reads the rule tables. Missing sections, rows with the wrong number of
// values, and values out of range throw FormatError.
Ruleset readRuleset(const TextFile& names);

} // namespace viceroy
