// SPDX-License-Identifier: GPL-3.0-or-later
//
// The turn of a European nation the computer plays: its count of forces and
// people, the tallies it plans with, and what it buys in Europe. Read from
// the original with a debugger and a disassembler (observed only); the
// rules are in our own words in docs/rules/european-ai.md.
#include "viceroy/game/colony.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"

#include <algorithm>

namespace viceroy {

namespace {

constexpr int kRegions = 16;
constexpr int kTypesCounted = 19; // the European unit types
constexpr int kFirstShip = 13;
constexpr int kLastShip = 18;
constexpr int kPrivateer = 16;
constexpr int kFrigate = 17;
constexpr int kManOWar = 18;
constexpr int kVeteranSoldier = 0x15;
constexpr int kMuskets = 15;
constexpr int kTools = 14;
constexpr int kHorses = 8;
constexpr int kCollegeBit = 13; // building row 13: a college (or a university)
constexpr int kMaxColonies = 48;

// The block after the tribe records, kept in the save as the original
// writes it. Per nation unless marked: bytes, or 16-bit words.
constexpr std::size_t kExtra = static_cast<std::size_t>(kTribes) * kTribeRecordSize;
constexpr std::size_t kTraits = kExtra + 0;        // 3 signed bytes per nation
constexpr std::size_t kUnitsAll = kExtra + 12;     // units of every kind
constexpr std::size_t kColonyCount = kExtra + 16;  // colonies
constexpr std::size_t kColonists = kExtra + 20;    // free colonist units
constexpr std::size_t kColonyPeople = kExtra + 24; // people in colonies
constexpr std::size_t kPeople = kExtra + 28;       // people in colonies and out
constexpr std::size_t kCombat = kExtra + 32;       // defence of the units
constexpr std::size_t kHoldsAll = kExtra + 36;     // holds of the ships
constexpr std::size_t kShips = kExtra + 40;        // ships
constexpr std::size_t kAttack = kExtra + 44;       // attack of the units (words)
constexpr std::size_t kArmedShips = kExtra + 52;   // ships that can attack
constexpr std::size_t kVeteranOdds = kExtra + 56;  // set elsewhere; read by the purchases
constexpr std::size_t kAttackOut = kExtra + 60;    // attack outside towns
constexpr std::size_t kByType = kExtra + 64;       // 19 per nation
constexpr std::size_t kRegionFlags = kExtra + 156; // 16, for the nation last counted
constexpr std::size_t kRegionUnits = kExtra + 172; // 16 per nation from here on
constexpr std::size_t kRegionColonies = kExtra + 236;
constexpr std::size_t kRegionAttackOut = kExtra + 300;
constexpr std::size_t kRegionPeople = kExtra + 364;
constexpr std::size_t kRegionCombat = kExtra + 428;
constexpr std::size_t kRegionAttack = kExtra + 492;
constexpr std::size_t kAveragePeople = kExtra + 556; // words
constexpr std::size_t kBlockEnd = kExtra + 727;

// What the region flags say about the nation counted.
constexpr std::uint8_t kFlagSettlement = 0x01;
constexpr std::uint8_t kFlagRival = 0x02;     // another European nation's unit
constexpr std::uint8_t kFlagRivalTown = 0x04; // another European nation's colony
constexpr std::uint8_t kFlagFortified = 0x08; // an armed unit of its own, fortified outside colonies

// In the block after the map: how many squares each region has (words).
constexpr std::size_t kRegionSize = 540;

// The nation record.
constexpr std::size_t kRelations = 52;
constexpr std::size_t kRelationTimers = 64;
constexpr std::size_t kTaxRate = 1;
constexpr std::size_t kReturnX = 50;
constexpr std::size_t kReturnY = 51;
constexpr std::size_t kFreeArtillery = 72; // artillery the Crown still owes
constexpr std::size_t kMusketLots = 73;    // lots of 50 muskets kept in Europe
constexpr std::size_t kHorseStock = 74;    // horses kept in Europe (word)

// Colony bytes the plans use.
constexpr std::size_t kColonyThreat = 0x1B; // bit 1: privateers near, bit 2: a frigate
constexpr std::size_t kColonyMarks = 0x1C;  // bit 0x20: a wagon train calls
constexpr std::size_t kColonyWant = 0x8D;   // the good it asks for, or -1
constexpr std::uint8_t kMarkedForRecruits = 0x10;

// The six things the computer buys in Europe, in the original's order.
constexpr int kItemArtillery = 0;
constexpr int kItemCaravel = 1;
constexpr int kItemMerchantman = 2;
constexpr int kItemGalleon = 3;
constexpr int kItemPrivateer = 4;
constexpr int kItemFrigate = 5;

bool shipType(int type) {
    return type >= kFirstShip && type <= kLastShip;
}

// Unit types that count as people: colonists and the land units made from
// them (not the Crown's regulars, artillery, wagons or treasure).
bool personType(int type) {
    return (type >= 0 && type <= 5) || type == 7 || type == 9;
}

// The military land units: soldiers, dragoons and the regulars of both sides.
bool militaryType(int type) {
    return type == 1 || type == 4 || (type >= 6 && type <= 9);
}

void addCapped(std::uint8_t& byte, int value) {
    byte = static_cast<std::uint8_t>(std::clamp(byte + value, 0, 255));
}

void bump(std::uint8_t& byte) {
    byte = static_cast<std::uint8_t>(byte + 1);
}

int signedByte(std::uint8_t b) {
    return b < 0x80 ? b : b - 256;
}

int word(const Bytes& block, std::size_t at) {
    return static_cast<std::int16_t>(block[at] | (block[at + 1] << 8));
}

void setWord(Bytes& block, std::size_t at, int value) {
    block[at] = static_cast<std::uint8_t>(value & 0xFF);
    block[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

int europeSquare(int nation) {
    return kEuropeBase + nation + 4 * kPortStage;
}

} // namespace

int Game::aiRandom(int low, int high) {
    // The original's random(low, high), both ends included.
    return low + nextRandom(high - low + 1);
}

void Game::nationCensus(int n) {
    auto& block = save_.tribesAndMore;
    if (n < 0 || n >= kEuropeanNations || block.size() < kBlockEnd) {
        return;
    }
    const auto un = static_cast<std::size_t>(n);
    setWord(block, kAttack + 2 * un, 0);
    for (const auto at : {kUnitsAll, kColonyCount, kColonists, kColonyPeople, kPeople, kCombat, kHoldsAll, kShips,
                          kArmedShips, kAttackOut}) {
        block[at + un] = 0;
    }
    std::fill_n(block.begin() + static_cast<std::ptrdiff_t>(kByType + kTypesCounted * un), kTypesCounted,
                std::uint8_t{0});
    ai_.inPort[un] = 0;
    ai_.homeward[un] = 0;
    setWord(block, kAveragePeople + 2 * un, 0);
    for (std::size_t r = 0; r < kRegions; ++r) {
        block[kRegionFlags + r] = 0;
        for (const auto at :
             {kRegionUnits, kRegionColonies, kRegionPeople, kRegionCombat, kRegionAttack, kRegionAttackOut}) {
            block[at + kRegions * un + r] = 0;
        }
    }
    const auto& map = save_.map;
    const auto& units = rules_.units;
    for (auto& u : save_.units) {
        const int region = landRegion(map, u.x, u.y);
        const auto ur = static_cast<std::size_t>(std::max(region, 0));
        if (u.nation != n) {
            if (region >= 0 && u.nation < kEuropeanNations) {
                block[kRegionFlags + ur] |= kFlagRival;
            }
            continue;
        }
        const int type = u.type;
        if (type < 0 || static_cast<std::size_t>(type) >= units.size()) {
            continue;
        }
        const auto& t = units[static_cast<std::size_t>(type)];
        if (type < kTypesCounted) {
            addCapped(block[kByType + kTypesCounted * un + static_cast<std::size_t>(type)], 1);
        }
        const int lane = (u.x - n) & 0xFF;
        if (shipType(type)) {
            // Sailing for Europe; then the ship's room and guns.
            if (lane == kEuropeBase + 4 * 4 || lane == kEuropeBase + 4 * 3) {
                ++ai_.homeward[un];
            }
            addCapped(block[kHoldsAll + un], t.cargoSlots);
            addCapped(block[kShips + un], 1);
            if (t.attack > 0) {
                bump(block[kArmedShips + un]);
            }
        } else {
            if (lane == kEuropeBase + 4 * kPortStage) {
                ++ai_.inPort[un];
            }
            if (t.combat > 1 && (u.orders == SaveUnit::kFortify || u.orders == SaveUnit::kFortified) &&
                !colonyAt(u.x, u.y) && region >= 0) {
                block[kRegionFlags + ur] |= kFlagFortified;
            }
        }
        // A unit without a home colony takes the one it stands in.
        if (signedByte(u.raw[6]) < 0) {
            const auto c = colonyAt(u.x, u.y);
            u.raw[6] = c ? static_cast<std::uint8_t>(*c) : std::uint8_t{0xFF};
        }
        bump(block[kUnitsAll + un]);
        if (region >= 0) {
            bump(block[kRegionUnits + kRegions * un + ur]);
        }
        if (type == unit_type::kColonist) {
            bump(block[kColonists + un]);
        }
        if (personType(type)) {
            addCapped(block[kPeople + un], 1);
            if (region >= 0) {
                addCapped(block[kRegionPeople + kRegions * un + ur], 1);
            }
        }
        if (shipType(type)) {
            continue;
        }
        const int defence = unitForce(u, false);
        addCapped(block[kCombat + un], defence);
        if (region >= 0) {
            addCapped(block[kRegionCombat + kRegions * un + ur], defence);
        }
        const int attack = unitForce(u, true);
        setWord(block, kAttack + 2 * un, word(block, kAttack + 2 * un) + attack);
        if (region >= 0) {
            addCapped(block[kRegionAttack + kRegions * un + ur], attack);
        }
        // In a town a human's units do not count here, nor a computer's
        // that guard it (goal 'A' or 'G').
        if (colonyAt(u.x, u.y) && (save_.players[un].control == 0 || u.raw[7] == 'A' || u.raw[7] == 'G')) {
            continue;
        }
        addCapped(block[kAttackOut + un], attack);
        if (region >= 0) {
            addCapped(block[kRegionAttackOut + kRegions * un + ur], attack);
        }
    }

    ai_.privateerThreat = 0;
    ai_.frigateThreat = 0;
    ai_.privateerPeople = 0;
    ai_.frigatePeople = 0;
    for (auto& colony : save_.colonies) {
        const int region = landRegion(map, colony.x, colony.y);
        const auto ur = static_cast<std::size_t>(std::max(region, 0));
        if (colony.nation != n) {
            if (region >= 0) {
                block[kRegionFlags + ur] |= kFlagRivalTown;
            }
            continue;
        }
        const int people = colony.population;
        bump(block[kColonyCount + un]);
        setWord(block, kAveragePeople + 2 * un, word(block, kAveragePeople + 2 * un) + people);
        addCapped(block[kPeople + un], people);
        addCapped(block[kColonyPeople + un], people);
        if (region >= 0) {
            bump(block[kRegionColonies + kRegions * un + ur]);
            addCapped(block[kRegionPeople + kRegions * un + ur], people);
        }
        // Armed ships of others within five squares by sea.
        auto& threat = colony.raw[kColonyThreat];
        threat &= 0xFC;
        for (int dy = -5; dy <= 5; ++dy) {
            for (int dx = -5; dx <= 5; ++dx) {
                const int x = colony.x + dx;
                const int y = colony.y + dy;
                if (!map.inside(x, y)) {
                    continue;
                }
                const auto here = unitsAt(x, y);
                if (here.empty() || unit(here.front()).nation == n) {
                    continue;
                }
                for (const int s : here) {
                    const int type = unit(s).type;
                    if (!shipType(type) || units[static_cast<std::size_t>(type)].attack <= 0) {
                        continue;
                    }
                    const int reach = routeReach(x, y, colony.x, colony.y, true, 8);
                    if (reach >= 0 && reach <= 5) {
                        threat |= type == kFrigate ? 0x02 : 0x01;
                    }
                }
            }
        }
        if ((threat & 0x02) != 0) {
            ++ai_.frigateThreat;
            ai_.frigatePeople += people;
        }
        if ((threat & 0x01) != 0) {
            ++ai_.privateerThreat;
            ai_.privateerPeople += people;
        }
    }
    for (const auto& s : save_.settlements) {
        const int region = landRegion(map, s.x, s.y);
        if (region >= 0) {
            block[kRegionFlags + static_cast<std::size_t>(region)] |= kFlagSettlement;
        }
    }
    ai_.openRegions = 0;
    for (std::size_t r = 0; r < kRegions; ++r) {
        const std::size_t at = kRegionSize + 2 * r;
        const int size = at + 1 < save_.tail.size() ? word(save_.tail, at) : 0;
        if (size >= 8 && block[kRegionColonies + kRegions * un + r] == 0) {
            ++ai_.openRegions;
        }
    }
    const int colonies = block[kColonyCount + un];
    if (colonies > 0) {
        setWord(block, kAveragePeople + 2 * un, word(block, kAveragePeople + 2 * un) / colonies);
    }
}

void Game::aiTallies(int n) {
    // What the nation's colonies ask for, less what its ships carry.
    auto& want = ai_.wants;
    want.fill(0);
    const auto un = static_cast<std::size_t>(n);
    ai_.marked[un] = 0;
    ai_.fortifiedRegions = 0;
    const auto& block = save_.tribesAndMore;
    if (block.size() >= kBlockEnd) {
        for (std::size_t r = 0; r < kRegions; ++r) {
            if ((block[kRegionFlags + r] & kFlagFortified) != 0) {
                ++ai_.fortifiedRegions;
            }
        }
    }
    for (std::size_t c = 0; c < save_.colonies.size(); ++c) {
        if (save_.colonies[c].nation != n) {
            continue;
        }
        // The colony's own plans come first; see docs/rules/european-ai.md.
        aiColonyPlan(static_cast<int>(c));
        auto& colony = save_.colonies[c];
        const int asks = signedByte(colony.raw[kColonyWant]);
        if (asks >= 0 && asks < kGoodsKinds) {
            ++want[static_cast<std::size_t>(asks)];
        }
        if (asks == kMuskets) {
            ++want[kMuskets];
        }
        if (colony.stock[kMuskets] == 0) {
            ++want[kMuskets];
        }
        if (colony.stock[kHorses] == 0) {
            ++want[kHorses];
        }
        if (colony.stock[kTools] == 0) {
            ++want[kTools];
        }
        if ((colony.raw[kColonyThreat] & kMarkedForRecruits) != 0) {
            ++ai_.marked[un];
        }
        colony.raw[kColonyMarks] &= static_cast<std::uint8_t>(~0x20);
    }
    ai_.wantsBefore = want;
    for (const auto& u : save_.units) {
        if (u.nation != n) {
            continue;
        }
        if (u.type == unit_type::kWagonTrain && signedByte(u.raw[6]) >= 0 &&
            static_cast<std::size_t>(u.raw[6]) < save_.colonies.size()) {
            auto& home = save_.colonies[u.raw[6]];
            if (home.nation == n) {
                home.raw[kColonyMarks] |= 0x20;
            }
        }
        if (shipType(u.type)) {
            for (int k = 0; k < u.holds; ++k) {
                const int g = holdGood(u, k);
                if (g >= 0 && g < kGoodsKinds) {
                    --want[static_cast<std::size_t>(g)];
                }
            }
        }
        if (u.type == unit_type::kPioneer) {
            --want[kTools];
        }
    }
}

void Game::aiRelations(int n) {
    // A grudge with a timer run out may cool off.
    auto& raw = save_.nations[static_cast<std::size_t>(n)].raw;
    for (std::size_t k = 0; k < kEuropeanNations; ++k) {
        auto& relation = raw[kRelations + k];
        auto& timer = raw[kRelationTimers + k];
        if ((relation & 0x08) != 0 && timer == 0 && aiRandom(0, 3) == 0) {
            relation = static_cast<std::uint8_t>((relation & 0xB7) | 0x01);
        }
        if (timer > 0) {
            --timer;
        }
    }
}

int Game::aiColonyNeed(int n) const {
    // How many more colonies the nation wants, as the original weighs it.
    const auto& block = save_.tribesAndMore;
    if (save_.colonies.size() >= kMaxColonies || block.size() < kBlockEnd) {
        return 0;
    }
    const auto un = static_cast<std::size_t>(n);
    const int colonies = block[kColonyCount + un];
    if (colonies < 1 || ai_.marked[un] == 0) {
        return 8;
    }
    const int trait = signedByte(block[kTraits + 3 * un + 1]);
    int need = (block[kPeople + un] - colonies) / (4 - trait);
    const int room = block[kHoldsAll + un] >> 1;
    if (need > room) {
        need -= (need - room + 1) >> 1;
    } else if (room > need) {
        need += (room - need + 1) >> 1;
    }
    const int crowd = 7 - 3 * trait;
    const int average = word(block, kAveragePeople + 2 * un);
    if (crowd > average) {
        need -= (crowd - average + 1) * colonies;
    }
    return std::max(0, need);
}

int Game::aiCreateInEurope(int n, int type, int profession) {
    if (unitCount() >= kMaxUnits) {
        return -1;
    }
    return addUnitInEurope(type, profession, n);
}

bool Game::aiBuyItem(int n, int item) {
    auto& nation = save_.nations[static_cast<std::size_t>(n)];
    const auto list = purchaseList(nation);
    const auto& [type, price] = list[static_cast<std::size_t>(item)];
    if (nation.gold < price) {
        return false;
    }
    const int made = aiCreateInEurope(n, type, 0);
    if (made < 0) {
        return false;
    }
    auto& u = save_.units[static_cast<std::size_t>(made)];
    u.raw[9] = nation.raw[kReturnX];
    u.raw[10] = nation.raw[kReturnY];
    u.orders = shipType(type) ? SaveUnit::kNoOrders : SaveUnit::kSentry;
    nation.gold -= price;
    return true;
}

int Game::aiRecruitSlot(int n, int slot) {
    // A recruit from the docks: experts come with what they need, and a
    // veteran soldier is sometimes mounted.
    const int profession = save_.nations[static_cast<std::size_t>(n)].recruits[static_cast<std::size_t>(slot)];
    int type = unit_type::kColonist;
    switch (profession) {
    case 0x14:
        type = unit_type::kPioneer;
        break;
    case 0x18:
        type = unit_type::kMissionary;
        break;
    case 0x16:
        type = unit_type::kScout;
        break;
    case kVeteranSoldier: {
        const int level = n == human() ? save_.difficulty : 1;
        type = aiRandom(0, level + 4) == 0 ? unit_type::kDragoon : unit_type::kSoldier;
        break;
    }
    default:
        break;
    }
    const int made = aiCreateInEurope(n, type, profession);
    if (made >= 0) {
        auto& u = save_.units[static_cast<std::size_t>(made)];
        u.orders = SaveUnit::kSentry;
        if (type == unit_type::kPioneer) {
            u.tools = 100;
        }
    }
    return made;
}

void Game::aiMarket(int n, int good, int amount) {
    // Goods the computer buys in Europe for its units: the market notices
    // as it does for goods bought into a hold.
    auto& nation = save_.nations[static_cast<std::size_t>(n)];
    const auto g = static_cast<std::size_t>(good);
    nation.tonsTraded[g] -= amount;
    nation.tonsTraded2[g] -= amount;
    nation.tradeGold[g] -= askPrice(rules_, nation, good) * amount;
    moveMarket(rules_, nation, good, -amount);
}

void Game::aiMarketBack(int n, int good, int amount) {
    // The same goods handed back, as if sold.
    auto& nation = save_.nations[static_cast<std::size_t>(n)];
    const auto g = static_cast<std::size_t>(good);
    nation.tonsTraded[g] += amount;
    nation.tonsTraded2[g] += amount;
    nation.tradeGold[g] += bidPrice(nation, good) * amount * (100 - nation.raw[kTaxRate]) / 100;
    moveMarket(rules_, nation, good, amount);
}

bool Game::aiHasCollege(int n) const {
    for (const auto& c : save_.colonies) {
        if (c.nation == n && ((c.buildings >> kCollegeBit) & 1) != 0) {
            return true;
        }
    }
    return false;
}

void Game::aiSwapExpert(int n, int u) {
    // An expert taken as a soldier leaves the skill on the docks: they swap
    // with the first recruit waiting who is no expert.
    auto& nation = save_.nations[static_cast<std::size_t>(n)];
    auto& unitRef = save_.units[static_cast<std::size_t>(u)];
    if (!isExpert(unitRef.profession)) {
        return;
    }
    int becomes = kFreeColonist;
    for (auto& r : nation.recruits) {
        if (!isExpert(r)) {
            becomes = r;
            r = unitRef.profession;
            break;
        }
    }
    unitRef.profession = becomes;
}

bool Game::isExpert(int profession) {
    // Everyone but free colonists, servants, criminals, converts and the
    // one row between the experts that no colonist holds.
    return profession != kFreeColonist && profession != 0x13 && profession != 0x19 && profession != 0x1A &&
           profession != 0x1B;
}

void Game::aiSailFromEurope(int n, int ship) {
    // The ship takes the units on the docks it has room for and sails.
    // (How the original picks them is not read yet; it takes them in the
    // order of the units.)
    for (const int u : unitsInEurope(save_, rules_, n, EuropePlace::InPort, false)) {
        if (freeHolds(ship) <= 0) {
            break;
        }
        if (carrierOf(save_, rules_, u) < 0) {
            boardInPort(u, ship);
        }
    }
    auto& waiting = ai_.inPort[static_cast<std::size_t>(n)];
    waiting = std::max(0, waiting - static_cast<int>(passengersInPort(ship).size()));
    sailToNewWorld(ship);
}

void Game::aiEurope(int n) {
    auto& nation = save_.nations[static_cast<std::size_t>(n)];
    auto& raw = nation.raw;
    const auto un = static_cast<std::size_t>(n);
    auto& block = save_.tribesAndMore;
    if (block.size() < kBlockEnd) {
        return;
    }
    const int level = save_.difficulty;
    const int colonies = block[kColonyCount + un];
    const bool independence = (save_.header[18] & 0x01) != 0;
    const auto byType = [&](int who, int type) {
        return int{block[kByType + kTypesCounted * static_cast<std::size_t>(who) + static_cast<std::size_t>(type)]};
    };

    // The Crown's help: gold every turn, more with the years and colonies,
    // and more at the harder levels.
    {
        int k = (save_.year - 1500) / 50 + colonies;
        if (save_.turn < 20) {
            k = 0;
        }
        if (save_.year >= 1700) {
            k *= 2;
        }
        int gift = level * k;
        if (level == 3) {
            gift += gift >> 1;
        }
        if (level == 4) {
            gift *= 2;
        }
        nation.gold += gift * 4;
    }
    const bool college = aiHasCollege(n);
    if (independence) {
        for (int i = unitCount(); i-- > 0;) {
            const auto& u = unit(i);
            if (u.nation == n && u.type == kManOWar && europePlace(u) == EuropePlace::InPort) {
                removeUnit(i);
            }
        }
    }
    const bool noShips = block[kShips + un] < 1;
    int rivalNavy = 0;
    for (int m = 0; m < kEuropeanNations; ++m) {
        if (m != n) {
            rivalNavy += byType(m, kPrivateer) + 4 * byType(m, kFrigate);
        }
    }
    rivalNavy >>= 2;
    if (independence) {
        rivalNavy = 0;
    }
    const int people = block[kPeople + un];
    const int colonyPeople = block[kColonyPeople + un];
    const int holds = block[kHoldsAll + un];
    const int armed = block[kArmedShips + un];
    const int h = human();
    bool wantFrigate = false;
    if ((ai_.frigateThreat != 0 || ai_.privateerThreat != 0) && rivalNavy != 0) {
        const bool pressed = (colonies >> 1) <= ai_.frigateThreat || (colonyPeople >> 1) <= ai_.frigatePeople ||
                             (save_.turn > 200 && nation.gold >= 2000);
        wantFrigate = pressed && byType(n, kFrigate) == 0 && byType(h, kFrigate) != 0;
    }
    bool wantPrivateer = false;
    if ((ai_.frigateThreat != 0 || ai_.privateerThreat != 0) && rivalNavy != 0 && !wantFrigate) {
        const bool pressed = (colonies >> 1) <= ai_.privateerThreat || (colonyPeople >> 1) <= ai_.privateerPeople ||
                             (save_.turn > 100 && nation.gold >= 1000);
        wantPrivateer = pressed && byType(n, kPrivateer) < 2 && byType(h, kPrivateer) != 0;
    }
    // Gold enough for what it must have.
    const auto list = purchaseList(nation);
    if (noShips) {
        nation.gold = std::max(nation.gold, list[kItemCaravel].price);
    }
    if (wantPrivateer) {
        nation.gold = std::max(nation.gold, list[kItemPrivateer].price);
    }
    if (wantFrigate) {
        nation.gold = std::max(nation.gold, list[kItemFrigate].price);
    }
    int most = 0;
    for (std::size_t m = 0; m < kEuropeanNations; ++m) {
        most = std::max(most, int{block[kArmedShips + m]});
    }
    int tied = 0;
    for (std::size_t m = 0; m < kEuropeanNations; ++m) {
        tied += block[kArmedShips + m] == most ? 1 : 0;
    }
    const bool behind = wantFrigate || armed < most || tied > 1;
    // Short of ships: more people than holds to carry them.
    const bool shortOfShips = !independence && (((people / 2 + 2 * colonies) / 2) & 0xFF) >= holds;

    // Ships, while there are more people than holds.
    if (!independence && people / 2 + colonies >= holds) {
        bool bought = false;
        if (wantFrigate) {
            bought = aiBuyItem(n, kItemFrigate);
            if (!bought) {
                return;
            }
        }
        if (!bought && wantPrivateer) {
            bought = aiBuyItem(n, kItemPrivateer);
            if (!bought) {
                return;
            }
        }
        if (!bought && armed < 8 && aiRandom(0, 1) != 0 && behind) {
            bought = aiBuyItem(n, kItemFrigate);
        }
        if (!bought && aiRandom(0, 3) != 0) {
            bought = aiBuyItem(n, kItemGalleon);
        }
        if (!bought && aiRandom(0, 1) == 0 && holds < 12) {
            bought = aiBuyItem(n, kItemMerchantman);
        }
        if (!bought && holds <= 2) {
            bought = aiBuyItem(n, kItemCaravel);
        }
        if (!bought && armed < 4 && aiRandom(0, 3) == 0 && behind && !shortOfShips) {
            bought = aiBuyItem(n, kItemPrivateer);
        }
        if (!bought && noShips) {
            return;
        }
    } else if (noShips) {
        return;
    }

    const auto inPort = [&] {
        std::vector<int> all;
        for (int i = 0; i < unitCount(); ++i) {
            if (unit(i).nation == n && unit(i).x == europeSquare(n) && unit(i).y == europeSquare(n)) {
                all.push_back(i);
            }
        }
        return all;
    };
    const auto countIn = [&](auto pred) {
        int c = 0;
        for (const int i : inPort()) {
            c += pred(unit(i)) ? 1 : 0;
        }
        return c;
    };
    const auto artilleryInPort = [&] { return countIn([](const SaveUnit& u) { return u.type == kArtillery; }); };
    if (artilleryInPort() == 0 && !independence && ai_.wants[kMuskets] > 0 && aiRandom(0, 3) == 0 && !shortOfShips &&
        holds > 4) {
        aiBuyItem(n, kItemArtillery);
    }
    int need = aiColonyNeed(n);
    const bool landOnDocks = countIn([](const SaveUnit& u) { return !shipType(u.type); }) > 0;
    // Every third turn it holds back. (The original also reads a byte just
    // before its table of units here, which is 0 in every game seen.)
    const bool hold = colonies > 0 && save_.turn % 3 == 0;

    // A recruit from the docks.
    if (!independence && !landOnDocks && !shortOfShips && !hold &&
        ai_.marked[un] - block[kColonists + un] >= colonies / 2) {
        const int base = (nation.recruitCount - level + 7) * 20;
        const int price = base - base * nation.crosses / (nation.crossesNeeded + 1);
        const int reserve = std::max(0, 2 * (30 * people - save_.turn));
        if (nation.gold >= price + reserve) {
            nation.gold -= price;
            const int slot = aiRandom(0, 2);
            if (aiRecruitSlot(n, slot) >= 0) {
                nation.recruits[static_cast<std::size_t>(slot)] = randomRecruit(n);
            }
        }
    }

    // The units on the docks take up arms or tools: the others first, then
    // the experts.
    int pioneerAsked = 0;
    bool geared = false;
    int musketLots = raw[kMusketLots];
    int horses = raw[kHorseStock] | (raw[kHorseStock + 1] << 8);
    const auto keepStocks = [&] {
        raw[kMusketLots] = static_cast<std::uint8_t>(std::clamp(musketLots, 0, 255));
        raw[kHorseStock] = static_cast<std::uint8_t>(horses & 0xFF);
        raw[kHorseStock + 1] = static_cast<std::uint8_t>((horses >> 8) & 0xFF);
    };
    // With a college a soldier may come out a veteran.
    const auto veteranChance = [&](int u) {
        if (!college) {
            return;
        }
        const int roll = aiRandom(0, byType(n, unit_type::kDragoon) + byType(n, unit_type::kSoldier));
        if (block[kVeteranOdds + un] >= roll) {
            save_.units[static_cast<std::size_t>(u)].profession = kVeteranSoldier;
        }
    };
    for (int pass = 0; pass < 2; ++pass) {
        for (const int u : inPort()) {
            if (independence) {
                break;
            }
            if (!personType(unit(u).type) || isExpert(unit(u).profession) != (pass == 1)) {
                continue;
            }
            if (unit(u).type == unit_type::kPioneer) {
                pioneerAsked |= need;
            }
            if (unit(u).type == unit_type::kColonist && !hold) {
                const int e = isExpert(unit(u).profession) ? 1 : 0;
                bool arm = ai_.wants[kMuskets] > 0 && aiRandom(0, e + 1) == 0;
                if (!arm && need != 0 && aiRandom(0, e + 2) == 0 && save_.turn >= 100) {
                    arm = true;
                }
                if (arm) {
                    const int price = musketLots > 0 ? 0 : askPrice(rules_, nation, kMuskets) * 50;
                    if (nation.gold >= price && !shortOfShips) {
                        if (musketLots > 0) {
                            --musketLots;
                        } else {
                            aiMarket(n, kMuskets, 50);
                        }
                        nation.gold -= price;
                        save_.units[static_cast<std::size_t>(u)].type = unit_type::kSoldier;
                        aiSwapExpert(n, u);
                        need = 0;
                        geared = true;
                        --ai_.wants[kMuskets];
                        veteranChance(u);
                        const int mount = horses >= 50 ? 0 : askPrice(rules_, nation, kHorses) * 50;
                        if (nation.gold >= mount) {
                            nation.gold -= mount;
                            save_.units[static_cast<std::size_t>(u)].type = unit_type::kDragoon;
                            if (horses >= 50) {
                                horses -= 50;
                            } else {
                                aiMarket(n, kHorses, 50);
                            }
                        }
                        continue;
                    }
                }
            }
            if (unit(u).type == unit_type::kColonist && !hold && need - byType(n, unit_type::kPioneer) > 0 &&
                aiRandom(0, 2) == 0 && pioneerAsked == 0) {
                if (save_.turn >= 100 && aiRandom(0, 2) <= byType(n, unit_type::kPioneer)) {
                    continue;
                }
                if (isExpert(unit(u).profession) && aiRandom(0, 4) != 0) {
                    continue;
                }
                const int price = askPrice(rules_, nation, kTools) * 100;
                if (nation.gold >= price) {
                    nation.gold -= price;
                    aiMarket(n, kTools, 100);
                    auto& p = save_.units[static_cast<std::size_t>(u)];
                    p.type = unit_type::kPioneer;
                    p.tools = 100;
                    pioneerAsked |= need;
                    need = 0;
                    geared = true;
                    --ai_.wants[kTools];
                    continue;
                }
            }
            if (unit(u).type == unit_type::kColonist && byType(n, unit_type::kMissionary) == 0 && save_.turn > 50 &&
                (save_.turn < 200 || aiRandom(0, 3) == 0) && save_.turn % 7 == 0 &&
                (!isExpert(unit(u).profession) || aiRandom(0, 7) == 0)) {
                save_.units[static_cast<std::size_t>(u)].type = unit_type::kMissionary;
                addCapped(block[kByType + kTypesCounted * un + unit_type::kMissionary], 1);
            }
        }
    }

    // Soldiers and artillery, when there are ships to carry them.
    const auto military = [&] {
        int m = countIn([](const SaveUnit& u) { return militaryType(u.type); });
        if (independence) {
            m += artilleryInPort();
        }
        return m;
    };
    int roomest = 0;
    for (const int i : inPort()) {
        const auto& u = unit(i);
        if (shipType(u.type) && (u.raw[4] & SaveUnit::kDamaged) == 0) {
            roomest = std::max(roomest, rules_.units[static_cast<std::size_t>(u.type)].cargoSlots);
        }
    }
    bool sentSoldiers = false;
    if (military() != 0 && !shortOfShips && !hold && !independence) {
        int space = roomest - military();
        if (save_.turn > 80) {
            while (horses / 50 > musketLots + 1) {
                horses -= 50;
                ++musketLots;
            }
            while (horses / 50 + 1 < musketLots) {
                --musketLots;
                horses += 50;
            }
        }
        if (artilleryInPort() == 0 && roomest >= 6) {
            int price = save_.turn >= 40 ? (10 - level) * 100 : 0;
            if (raw[kFreeArtillery] > 0) {
                --raw[kFreeArtillery];
                price = 0;
            }
            if (nation.gold >= price) {
                const int gun = aiCreateInEurope(n, kArtillery, 0);
                if (gun >= 0) {
                    save_.units[static_cast<std::size_t>(gun)].orders = SaveUnit::kSentry;
                    --space;
                    nation.gold -= price;
                }
            }
        }
        bool more = true;
        while (more) {
            more = false;
            const int base = 10 * (2 * (nation.recruitCount + 7) - (level & ~1));
            int price = base - base * nation.crosses / (nation.crossesNeeded + 1);
            if (musketLots == 0) {
                price += askPrice(rules_, nation, kMuskets) * 50;
            }
            if (save_.turn >= 100) {
                price -= level * price * 10 / 100;
            }
            if (nation.gold < price) {
                break;
            }
            const int slot = aiRandom(0, 2);
            const int u = aiRecruitSlot(n, slot);
            if (u < 0) {
                break;
            }
            auto& s = save_.units[static_cast<std::size_t>(u)];
            if (s.type == unit_type::kSoldier || s.type == unit_type::kDragoon) {
                // A veteran brings his own muskets.
                if (musketLots == 0) {
                    price -= askPrice(rules_, nation, kMuskets) * 50;
                    aiMarketBack(n, kMuskets, 50);
                } else {
                    ++musketLots;
                }
            } else if (s.type == unit_type::kPioneer) {
                price -= bidPrice(nation, kTools) * 100;
                aiMarketBack(n, kTools, 100);
            } else if (s.type == unit_type::kScout) {
                s.type = unit_type::kDragoon;
            }
            if (s.type != unit_type::kDragoon) {
                s.type = unit_type::kSoldier;
                aiSwapExpert(n, u);
            }
            nation.gold -= price;
            if (musketLots == 0) {
                aiMarket(n, kMuskets, 50);
            } else {
                --musketLots;
            }
            if (college && s.profession != kVeteranSoldier) {
                veteranChance(u);
            }
            int mount = askPrice(rules_, nation, kHorses) * 50;
            if (save_.turn >= 100) {
                mount -= level * mount * 10 / 100;
            }
            if (horses >= 50) {
                mount = 0;
            }
            if (nation.gold >= mount) {
                nation.gold -= mount;
            }
            save_.units[static_cast<std::size_t>(u)].type = unit_type::kDragoon;
            if (horses >= 50) {
                horses -= 50;
            } else {
                aiMarket(n, kHorses, 50);
            }
            nation.recruits[static_cast<std::size_t>(slot)] = randomRecruit(n);
            sentSoldiers = true;
            more = true;
            space -= rules_.units[unit_type::kDragoon].size;
            if (space <= 0) {
                break;
            }
        }
    }
    keepStocks();

    // The ships in port: unload, load what the colonies ask for, and sail.
    const int askLimit = ai_.inPort[un] + (save_.turn & 1);
    bool sailed = true;
    while (sailed) {
        sailed = false;
        for (const int s : inPort()) {
            const auto& ship = unit(s);
            if (!shipType(ship.type) || (ship.raw[4] & SaveUnit::kDamaged) != 0) {
                continue;
            }
            save_.units[static_cast<std::size_t>(s)].raw[6] = 0xFF;
            while (unit(s).holds > 0) {
                const int good = holdGood(unit(s), 0);
                const int amount = holdAmount(unit(s), 0);
                if (good == kMuskets) {
                    musketLots += (amount + 49) / 50;
                    removeHold(save_.units[static_cast<std::size_t>(s)], 0);
                } else if (good == kHorses) {
                    horses += amount;
                    removeHold(save_.units[static_cast<std::size_t>(s)], 0);
                } else {
                    sellGoods(save_, rules_, s, good);
                    if (unit(s).holds > 0 && holdGood(unit(s), 0) == good) {
                        removeHold(save_.units[static_cast<std::size_t>(s)], 0);
                    }
                }
            }
            keepStocks();
            const int capacity = rules_.units[static_cast<std::size_t>(unit(s).type)].cargoSlots;
            if (sentSoldiers && roomest == capacity) {
                sentSoldiers = false;
            } else {
                for (int g = kMuskets; g >= 0; --g) {
                    if (unit(s).holds >= capacity || shortOfShips || colonies == 0) {
                        break;
                    }
                    const auto ug = static_cast<std::size_t>(g);
                    if (ai_.wants[ug] < askLimit && !hold) {
                        continue;
                    }
                    if ((geared || pioneerAsked != 0) && capacity - unit(s).holds <= 2 && !hold) {
                        continue;
                    }
                    if (nation.gold >= askPrice(rules_, nation, g) * 100) {
                        buyGoods(save_, rules_, s, g, freeHolds(s));
                        --ai_.wants[ug];
                    }
                }
            }
            aiSailFromEurope(n, s);
            sailed = true;
            break;
        }
    }
}

void Game::europeanTurn(int n) {
    if (n < 0 || n >= kEuropeanNations || save_.players[static_cast<std::size_t>(n)].control != 1) {
        return;
    }
    nationCensus(n);
    aiTallies(n);
    aiRelations(n);
    aiEurope(n);
    aiPlan(n);
    aiMoveUnits(n);
}

} // namespace viceroy
