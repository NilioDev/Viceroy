// SPDX-License-Identifier: GPL-3.0-or-later
//
// The home port in Europe: where units there are, and the market.
//
// A unit in Europe or on the high seas between has x = y = 228 + nation +
// 4 × stage. Ships sail towards lower stages: stages 4 and 3 are on the way
// to Europe ("Expected Soon"), 2 is the port and its docks, and 1 and 0 are
// on the way to the New World ("Bound For"). Land units share the stage of
// the ship that carries them. See docs/rules/europe.md.
#pragma once

#include "viceroy/data/savegame.hpp"
#include "viceroy/rules/ruleset.hpp"

#include <vector>

namespace viceroy {

enum class EuropePlace {
    None,         // on the map
    ToNewWorld,   // stages 1 and 0
    InPort,       // stage 2: ships in port, land units on the docks
    ToEurope,     // stages 4 and 3
};

inline constexpr int kEuropeBase = 228;
inline constexpr int kPortStage = 2;
inline constexpr int kLastStage = 4;

// The stage of a unit in Europe or on the high seas, or -1 on the map.
int europeStage(const SaveUnit& unit);
EuropePlace europePlace(const SaveUnit& unit);
void setEuropeStage(SaveUnit& unit, int stage);

// The units of a nation at a place, in the order of the savegame.
std::vector<int> unitsInEurope(const SaveGame& save, const Ruleset& rules, int nation, EuropePlace place,
                               bool ships);

// The ship a land unit sailing to or from Europe travels on: the first ship
// along its chain of units, or -1.
int carrierOf(const SaveGame& save, const Ruleset& rules, int unit);

bool isShipType(const Ruleset& rules, int type);

// Market prices per 100 units: what the market pays and what it asks.
int bidPrice(const SaveNation& nation, int good);
int askPrice(const Ruleset& rules, const SaveNation& nation, int good);

} // namespace viceroy
