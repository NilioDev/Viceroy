// SPDX-License-Identifier: GPL-3.0-or-later
//
// A land unit attacking another: the strengths and bonuses the Combat
// Analysis box lists, and what happens to the loser. See
// docs/rules/combat.md.
#pragma once

#include <string>
#include <vector>

namespace viceroy {

// Rows of LABELS.TXT @MISC that name a bonus.
namespace combat_label {
inline constexpr int kVeteran = 65;
inline constexpr int kTitle = 75; // "COMBAT ANALYSIS"
inline constexpr int kFatigue = 76;
inline constexpr int kAttackBonus = 77;
inline constexpr int kTerrain = 79;
inline constexpr int kColony = 80;
inline constexpr int kFortified = 81;
inline constexpr int kSpainBonus = 82;
inline constexpr int kArtilleryInOpen = 84;
} // namespace combat_label

struct CombatBonus {
    CombatBonus() = default;
    CombatBonus(int l, int p, bool picture = false) : label(l), percent(p), terrainPicture(picture) {}
    int label = 0;   // row of LABELS.TXT @MISC
    int percent = 0; // +50, -75, ...
    bool terrainPicture = false; // the box shows the defender's square beside it
    bool colonyPicture = false;  // the box shows the defender's colony beside it
    int settlementPicture = -1;  // the box shows this native settlement beside it
    // Named by this text instead of the label (a stockade's name, "Muskets").
    std::string text;
    // Added to the base ("+1") instead of a percentage.
    int points = 0;
};

struct CombatSide {
    int unit = -1;
    // A colony's colonist defending it (no unit stands there): the colony,
    // the colonist's place in it, and whether 50 muskets arm them.
    int colony = -1;
    int colonist = -1;
    bool armed = false;
    // A village's own brave (no unit stands there): the settlement, and
    // the brave's unit type.
    int settlement = -1;
    int braveType = -1;
    int base = 0; // the unit type's attack or combat value
    std::vector<CombatBonus> bonuses;
    // The base with the bonuses added up (+50% and +50% make twice the
    // base), in hundredths.
    int strength() const;
};

struct CombatAnalysis {
    CombatSide attacker;
    CombatSide defender;
};

// The two numbers the original rolls against: the attacker wins when a
// roll of 1 to attack + defense comes out at attack or less. They are the
// strengths in eighths of a point, after every adjustment.
struct CombatStrengths {
    int attack = 0;
    int defense = 0;
};

enum class CombatOutcome {
    AttackerWon,
    DefenderWon,
    Evaded, // a ship got away
};

} // namespace viceroy
