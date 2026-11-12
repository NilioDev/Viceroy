// SPDX-License-Identifier: GPL-3.0-or-later
//
// A land unit attacking another: the strengths and bonuses the Combat
// Analysis box lists, and what happens to the loser. See
// docs/rules/combat.md.
#pragma once

#include <vector>

namespace viceroy {

// Rows of LABELS.TXT @MISC that name a bonus.
namespace combat_label {
inline constexpr int kVeteran = 65;
inline constexpr int kTitle = 75; // "COMBAT ANALYSIS"
inline constexpr int kFatigue = 76;
inline constexpr int kAttackBonus = 77;
inline constexpr int kTerrain = 79;
inline constexpr int kFortified = 81;
inline constexpr int kArtilleryInOpen = 84;
} // namespace combat_label

struct CombatBonus {
    int label = 0;   // row of LABELS.TXT @MISC
    int percent = 0; // +50, -75, ...
    bool terrainPicture = false; // the box shows the defender's square beside it
};

struct CombatSide {
    int unit = -1;
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

enum class CombatOutcome {
    AttackerWon,
    DefenderWon,
};

} // namespace viceroy
