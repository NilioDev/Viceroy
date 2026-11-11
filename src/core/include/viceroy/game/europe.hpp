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

// Land units on the docks, in the order the screen shows them (the last
// to arrive first). Units aboard a ship in port (sentried) are not there.
std::vector<int> unitsOnDocks(const SaveGame& save, const Ruleset& rules, int nation);

// The ship a land unit sailing to or from Europe travels on: the first ship
// along its chain of units, or -1.
int carrierOf(const SaveGame& save, const Ruleset& rules, int unit);

bool isShipType(const Ruleset& rules, int type);

// Market prices per unit of a good: what the market pays and what it asks.
int bidPrice(const SaveNation& nation, int good);
int askPrice(const Ruleset& rules, const SaveNation& nation, int good);

// --- Cargo in a ship's holds ----------------------------------------------
// A ship's first `holds` holds carry goods: the good of hold k is a nibble
// at byte 13 + k / 2 (the low one for even k) and its amount is byte 16 + k.
int holdGood(const SaveUnit& ship, int hold);
int holdAmount(const SaveUnit& ship, int hold);
void setHold(SaveUnit& ship, int hold, int good, int amount);
void removeHold(SaveUnit& ship, int hold);
inline constexpr int kMaxHolds = 6;
inline constexpr int kHoldSize = 100;

struct Trade {
    int amount = 0; // units moved
    int gold = 0;   // paid (buying) or received after tax (selling)
    int tax = 0;
};

// Buys up to 100 of a good into a ship in port: tops up a hold of the same
// good, or fills a free one. Limited by gold and room.
Trade buyGoods(SaveGame& save, const Ruleset& rules, int ship, int good, int freeHolds);
// Sells everything of a good aboard a ship in port.
Trade sellGoods(SaveGame& save, int ship, int good);

// --- Recruit, Purchase and Train --------------------------------------------
// What the passage of a recruit costs: 140 + 20 for every recruit so far,
// less a share for the crosses gathered towards the next migrant. Measured
// on the original; see docs/rules/europe.md.
int recruitPrice(const SaveNation& nation);
int artilleryPrice(const SaveNation& nation);

struct PurchaseItem {
    int unitType = 0; // row of @UNIT
    int price = 0;
};
// The Purchase list: artillery and the ships, in the original's order.
std::vector<PurchaseItem> purchaseList(const SaveNation& nation);

// The rows of @JOB the Royal University teaches, cheapest first, in the
// original's order where two cost the same.
std::vector<int> trainingList(const Ruleset& rules);

// The row of @JOB that names a profession (SaveUnit::profession).
int professionJob(int profession);
// The unit a person of a profession arrives as: veteran soldiers come as
// soldiers, pioneers as pioneers, and so on. The rest are colonists.
int professionUnitType(int profession);

inline constexpr int kFreeColonist = 0x1C;
inline constexpr int kArtillery = 11;

} // namespace viceroy
