// SPDX-License-Identifier: GPL-3.0-or-later
//
// The native settlements: their mood towards a European nation, and the
// skill a village teaches. See docs/rules/natives.md.
#pragma once

#include "viceroy/data/savegame.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace viceroy {

inline constexpr int kTribes = 8;
inline constexpr int kTribeRecordSize = 78;

// Settlement flags (byte 3 of its record).
inline constexpr std::uint8_t kSettlementTaught = 0x02;  // has taught its skill
inline constexpr std::uint8_t kSettlementCapital = 0x04;

// The tribe (0-7, in @TRIBES order) a settlement belongs to.
int tribeOf(const SaveSettlement& settlement);

// The tribe's level: 0 semi-nomadic, 1 agrarian, 2 advanced, 3 civilized.
int tribeLevel(const SaveGame& save, int tribe);

// How much the tribe is alarmed by a European nation, and the four steps
// the original reads it in (below 25, 50, 75, and above).
int tribeTension(const SaveGame& save, int tribe, int nation);
int tensionLevel(int tension);

void setTribeTension(SaveGame& save, int tribe, int nation, int tension);

// The settlement's mission: the nation that runs it, or -1 (byte 5; bit
// 0x10 marks a mission of a Jesuit or with Jean de Brebeuf).
int missionOf(const SaveSettlement& settlement);
inline constexpr std::uint8_t kExpertMission = 0x10;

// The nation's colony nearest to (x, y), measured the original's way (the
// longer side plus half the shorter); a later colony wins a tie. With a
// region, only colonies on land of that region count. -1 if there is none.
int nearestColony(const SaveGame& save, int x, int y, int nation, int region = -1);

// The land region of a square (the low four bits of the path layer), or
// -1 for water and the map's border.
int landRegion(const SaveMap& map, int x, int y);

// Whether the nation has met the tribe (bit 0x40 of its attitude byte).
bool metTribe(const SaveGame& save, int nation, int tribe);

// The GAME.TXT section that greets the nation's unit at the settlement:
// VILLAGEHAPPY, VILLAGESAVAGE, VILLAGEMEDIUM, VILLAGEBAD or VILLAGEWAR.
std::string villageGreeting(const SaveGame& save, int settlement, int nation);

// The 16 goods weighed for the settlement: what its people want and what
// its land gives, worked out from the squares around it.
struct VillageEconomy {
    std::array<int, 16> wants{};
    std::array<int, 16> makes{};
};
VillageEconomy villageEconomy(const SaveGame& save, int settlement);

// The skill the settlement teaches (a row of @JOB). The same village always
// teaches the same skill in a game.
int villageSkill(const SaveGame& save, int settlement);

// The original's own sort: ascending, with each value's index in `order`.
// Equal values can change places, which decides ties.
void originalSort(std::array<int, 16>& values, std::array<int, 16>& order);

// The settlement's economy as its trade sees it: what it wants (less what
// it makes most of) and what it makes, sorted, with each good's row.
struct VillageMarket {
    std::array<int, 16> wants{};
    std::array<int, 16> makes{};      // sorted, smallest first
    std::array<int, 16> makesOrder{}; // the good of each place in `makes`
};
VillageMarket villageMarket(const SaveGame& save, int settlement);

// The three goods most wanted, leaving out those in the settlement's bytes
// 8 and 9 (rows of @CARGO, most wanted first).
std::array<int, 3> topWants(std::array<int, 16> wants, const SaveSettlement& settlement);
std::array<int, 3> villageWants(const SaveGame& save, int settlement);

// Settlement flag: a scout has spoken with the chief.
inline constexpr std::uint8_t kSettlementVisited = 0x08;
// Settlement flag: it has paid tribute.
inline constexpr std::uint8_t kSettlementTribute = 0x10;

// The random numbers of the original's C library: the same seed gives the
// same numbers.
class OriginalRandom {
public:
    explicit OriginalRandom(std::uint32_t seed) : state_(seed) {}
    int next(); // 0 to 32767
    int between(int low, int high); // inclusive
private:
    std::uint32_t state_;
};

} // namespace viceroy
