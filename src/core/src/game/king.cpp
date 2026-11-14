// SPDX-License-Identifier: GPL-3.0-or-later
//
// The King: taxes and the parties against them, boycotts and back taxes,
// the rebels counted over the colonies and the Crown's army. Read from the
// original; see docs/rules/king.md.
#include "viceroy/game/colony.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/sounds.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace viceroy {

namespace {

// Nation record.
constexpr std::size_t kBellsToo = 0x0E;   // 16 bits, see congress.cpp
constexpr std::size_t kLastChange = 0x10; // the last change of the tax (16 bits)
constexpr std::size_t kRebelShare = 0x19; // the nation's rebels in %
constexpr std::size_t kBoycotts = 0x20;   // one bit per good (16 bits)
constexpr std::size_t kCrownFund = 0x22;  // what the Crown has put by for its army (32 bits)

// Header.
constexpr std::size_t kIndependence = 18; // bit 0: the war of independence
constexpr std::size_t kKingWives = 55;    // the King's weddings so far
constexpr std::size_t kKingWar = 56;      // the country the King last fought, 1-8
constexpr std::size_t kHumanRebels = 96;  // the human's rebels in % (16 bits)
constexpr std::size_t kFirstHalf = 98;    // the nation that first had half its people rebels, -1 (16 bits)
constexpr std::size_t kRebelsTold = 104;  // the human's rebels in tens of % when last told (16 bits)
constexpr std::size_t kRoyalForce = 106;  // regulars, dragoons, men-of-war, artillery (16 bits each)

constexpr int kMaxTax = 75;
constexpr int kNoMoreRises = 85; // the King leaves a tax above this alone
constexpr int kPartyMax = 100;
constexpr int kArmyPrice = 1800;
constexpr std::uint8_t kCoastal = 0x40; // colony byte 28: by the water

template <std::size_t N>
int word(const std::array<std::uint8_t, N>& raw, std::size_t at) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(raw[at] | (raw[at + 1] << 8)));
}

template <std::size_t N>
void setWord(std::array<std::uint8_t, N>& raw, std::size_t at, int value) {
    const auto v = static_cast<std::uint16_t>(value);
    raw[at] = static_cast<std::uint8_t>(v & 0xFF);
    raw[at + 1] = static_cast<std::uint8_t>(v >> 8);
}

template <std::size_t N>
int long32(const std::array<std::uint8_t, N>& raw, std::size_t at) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(raw[at]) |
                                     (static_cast<std::uint32_t>(raw[at + 1]) << 8) |
                                     (static_cast<std::uint32_t>(raw[at + 2]) << 16) |
                                     (static_cast<std::uint32_t>(raw[at + 3]) << 24));
}

template <std::size_t N>
void setLong32(std::array<std::uint8_t, N>& raw, std::size_t at, int value) {
    const auto v = static_cast<std::uint32_t>(value);
    for (std::size_t i = 0; i < 4; ++i) {
        raw[at + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
    }
}

constexpr int kFood = 0;
constexpr int kHorses = 8;
constexpr int kTools = 14;
constexpr int kMuskets = 15;

} // namespace

bool Game::isBoycotted(int nation, int good) const {
    if (nation < 0 || nation >= kEuropeanNations || good < 0 || good >= kGoodsKinds) {
        return false;
    }
    const auto& raw = save_.nations[static_cast<std::size_t>(nation)].raw;
    return ((raw[kBoycotts] | (raw[kBoycotts + 1] << 8)) & (1 << good)) != 0;
}

int Game::backTaxes(int good) const {
    if (good < 0 || good >= kGoodsKinds) {
        return 0;
    }
    return 500 * askPrice(rules_, save_.nations[static_cast<std::size_t>(human())], good);
}

bool Game::payBackTaxes(int good) {
    auto& nation = save_.nations[static_cast<std::size_t>(human())];
    const int amount = backTaxes(good);
    if (nation.gold < amount) {
        notices_.push_back(GameNotice{"KISSSORRY", {}, -1, {nation.gold}});
        return false;
    }
    nation.gold -= amount;
    // Read from the original: what is paid goes to the Crown's army fund.
    setLong32(nation.raw, kCrownFund, long32(nation.raw, kCrownFund) + amount);
    const int boycotts = (nation.raw[kBoycotts] | (nation.raw[kBoycotts + 1] << 8)) & ~(1 << good);
    setWord(nation.raw, kBoycotts, boycotts);
    return true;
}

void Game::changeTax(int nation, int amount, GameNotice notice) {
    if (nation < 0 || nation >= kEuropeanNations) {
        return;
    }
    auto& n = save_.nations[static_cast<std::size_t>(nation)];
    // A fall takes the tax to 0 at most; a rise stops at 75.
    if (amount < 0 && -amount > n.taxRate) {
        amount = -n.taxRate;
    }
    n.taxRate += amount;
    if (n.taxRate > kMaxTax) {
        amount -= n.taxRate - kMaxTax;
        n.taxRate = kMaxTax;
    }
    if (amount == 0 || save_.players[static_cast<std::size_t>(nation)].control != 0) {
        return;
    }
    // The good a party would throw into the sea: drawn by how much of each
    // the nation has traded in Europe (food and tools count half, horses
    // and muskets a quarter), among the goods not boycotted that one of
    // its colonies by the water has; the colony with the most of it.
    std::array<int, kGoodsKinds> weight{};
    std::array<int, kGoodsKinds> most{};
    std::array<int, kGoodsKinds> where{};
    where.fill(-1);
    for (int g = 0; g < kGoodsKinds; ++g) {
        weight[static_cast<std::size_t>(g)] = std::abs(n.tonsTraded[static_cast<std::size_t>(g)]) / 100;
    }
    weight[kHorses] >>= 2;
    weight[kMuskets] >>= 2;
    weight[kTools] >>= 1;
    weight[kFood] >>= 1;
    for (int c = 0; c < static_cast<int>(save_.colonies.size()); ++c) {
        const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
        if (colony.nation != nation || (colony.raw[28] & kCoastal) == 0) {
            continue;
        }
        for (int g = 0; g < kGoodsKinds; ++g) {
            const auto ug = static_cast<std::size_t>(g);
            if (!isBoycotted(nation, g) && colony.stock[ug] > most[ug]) {
                most[ug] = colony.stock[ug];
                where[ug] = c;
            }
        }
    }
    int total = 0;
    for (int g = 0; g < kGoodsKinds; ++g) {
        if (!isBoycotted(nation, g) && most[static_cast<std::size_t>(g)] != 0) {
            total += weight[static_cast<std::size_t>(g)];
        }
    }
    int roll = aiRandom(1, total);
    int good = -1;
    for (int g = 0; g < kGoodsKinds; ++g) {
        if (most[static_cast<std::size_t>(g)] == 0) {
            continue;
        }
        roll -= weight[static_cast<std::size_t>(g)];
        if (roll <= 0) {
            good = g;
            break;
        }
    }
    notice.numbers = {std::abs(amount), n.taxRate};
    if (good < 0 || amount < 0 || isBoycotted(nation, good)) {
        // A fall comes with a happier tune, a rise with the King's fanfare.
        notice.music = {amount < 0 ? MusicCue{MusicCue::Kind::MoodEvent, 2}
                                   : MusicCue{MusicCue::Kind::Sound, sounds::kKing}};
        notices_.push_back(std::move(notice));
        return;
    }
    const int colony = where[static_cast<std::size_t>(good)];
    if (notice.strings.size() < 4) {
        notice.strings.resize(4);
    }
    // "Boston Tea": the colony and the good, for "Hold 'Boston Tea Party.'"
    notice.strings[3] = save_.colonies[static_cast<std::size_t>(colony)].name + " " +
                        rules_.cargo[static_cast<std::size_t>(good)].name;
    notice.value = kTaxQuestion;
    notice.music = {MusicCue{MusicCue::Kind::Sound, sounds::kKing}};
    taxRaise_ = TaxRaise{amount, good, colony, nation};
    notices_.push_back(std::move(notice));
}

void Game::answerTaxRaise(bool party) {
    const TaxRaise raise = std::exchange(taxRaise_, TaxRaise{});
    if (!party || raise.amount <= 0 || raise.colony < 0 || raise.colony >= static_cast<int>(save_.colonies.size())) {
        return;
    }
    auto& n = save_.nations[static_cast<std::size_t>(raise.nation)];
    n.taxRate -= raise.amount;
    auto& colony = save_.colonies[static_cast<std::size_t>(raise.colony)];
    const auto g = static_cast<std::size_t>(raise.good);
    const int thrown = std::min(colony.stock[g], kPartyMax);
    colony.stock[g] -= thrown;
    // The tea goes into the colony's count of rebels.
    colony.rebelDividend += thrown;
    const std::string label = colony.name + " " + rules_.cargo[g].name;
    GameNotice notice{"TEAPARTY", {rules_.cargo[g].name, colony.name, "", label}, -1, {thrown}};
    notice.ports = {{2, raise.nation}};
    notice.music = {MusicCue{MusicCue::Kind::Sound, sounds::kParty}};
    notices_.push_back(std::move(notice));
    const int boycotts = (n.raw[kBoycotts] | (n.raw[kBoycotts + 1] << 8)) | (1 << raise.good);
    setWord(n.raw, kBoycotts, boycotts);
}

bool Game::kingTax(int nation, int turn, int year) {
    if (nation < 0 || nation >= kEuropeanNations) {
        return false;
    }
    const bool hasColony = std::any_of(save_.colonies.begin(), save_.colonies.end(),
                                       [&](const SaveColony& c) { return c.nation == nation; });
    if (!hasColony || turn < 30) {
        return false;
    }
    // The King speaks every 18 turns, 15 after 1600 and 3 fewer after 1700
    // and again after 1750; the player hears him more often at the harder
    // levels.
    int period = year > 1600 ? 15 : 18;
    period -= year > 1700 ? 3 : 0;
    period -= year > 1750 ? 3 : 0;
    const bool human = save_.players[static_cast<std::size_t>(nation)].control == 0;
    period -= human ? 2 * (save_.difficulty - 2) : 0;
    auto& n = save_.nations[static_cast<std::size_t>(nation)];
    if (period <= 0 || turn % period != 0 || n.taxRate > kNoMoreRises) {
        return false;
    }
    // Read from the original: the human's rebels weigh in for every nation.
    const int rebels = word(save_.header, kHumanRebels);
    int roll = aiRandom(1, 1000);
    roll += 5 * (2 * rebels - n.taxRate) + n.gold / 100 + nationPeople(nation) + turn / 30;

    GameNotice notice;
    notice.strings = {"", save_.players[static_cast<std::size_t>(nation)].name, ""};
    notice.lines = {{0, true, "DIFFICULTY", save_.difficulty}};
    int amount = 0;
    if (roll < 100) {
        amount = std::min(aiRandom(2, 5), n.taxRate);
        if (amount <= 0) {
            return false;
        }
        amount = -amount;
        notice.section = "KINGVICTORY";
        notice.lines.push_back({2, false, "COUNTRIES", static_cast<std::int8_t>(save_.header[kKingWar]) - 1});
    } else if (roll < 650 && static_cast<std::int8_t>(save_.header[kKingWives]) < 30) {
        amount = 1;
        notice.section = "KINGWIFE";
        const int wives = std::max(static_cast<std::int8_t>(save_.header[kKingWives]) + 1, 1);
        save_.header[kKingWives] = static_cast<std::uint8_t>(wives);
        notice.lines.push_back({2, false, "ORDINAL", wives - 1});
    } else if (roll < 950) {
        amount = 2;
        notice.section = "KINGWAR";
        int country = 0;
        do {
            country = aiRandom(1, 8);
        } while (country == static_cast<std::int8_t>(save_.header[kKingWar]));
        save_.header[kKingWar] = static_cast<std::uint8_t>(country);
        notice.lines.push_back({2, false, "COUNTRIES", country - 1});
    } else if (roll < 1100) {
        amount = aiRandom(3, 4);
        notice.section = "KINGNAVACT";
    } else {
        amount = aiRandom(5, 8);
        notice.section = "KINGSTAMPACT";
        notice.strings[2] = save_.players[static_cast<std::size_t>(nation)].country;
    }
    setWord(n.raw, kLastChange, amount);
    changeTax(nation, amount, std::move(notice));
    return true;
}

int Game::nationRebelPercent(int nation) const {
    long people = 0;
    long rebels = 0;
    const int extra = nationEffects(save_, nation).libertyExtra(true);
    for (const auto& c : save_.colonies) {
        if (c.nation == nation) {
            people += c.population;
            rebels += static_cast<long>(c.population) * sonsOfLibertyPercent(c, extra);
        }
    }
    return people == 0 ? 0 : static_cast<int>(rebels / people);
}

int Game::royalUnitType(int kind, int nation) const {
    // The unit a kind of the Crown's army is: regulars, cavalry, men-of-war
    // and artillery. For a nation the human plays (the side that first had
    // half its people rebels) the first two are its own.
    const bool rebels = (save_.header[kIndependence] & 0x01) != 0;
    const bool humanSide = nation >= 0 && nation < kEuropeanNations &&
                           save_.players[static_cast<std::size_t>(nation)].control == 0;
    switch (kind) {
    case 1: return humanSide ? (rebels ? 7 : 4) : 8;
    case 2: return 18;
    case 3: return 11;
    default: return humanSide ? (rebels ? 9 : 4) : 6;
    }
}

void Game::kingArmyGrowth(int nation) {
    if ((save_.header[kIndependence] & 0x01) != 0) {
        return;
    }
    auto& n = save_.nations[static_cast<std::size_t>(nation)];
    // The Crown puts money by every turn, more each era.
    int add = save_.difficulty * 8 + 10;
    add *= save_.year >= 1600 ? 2 : 1;
    add *= save_.year >= 1700 ? 2 : 1;
    add *= save_.year >= 1750 ? 2 : 1;
    const int fund = long32(n.raw, kCrownFund) + add;
    setLong32(n.raw, kCrownFund, fund);
    if (fund < kArmyPrice) {
        return;
    }
    // A unit for every 1800: dragoons while there are fewer than a third
    // of the regulars, artillery below a quarter, and a man-of-war for
    // every ten of the others.
    const auto count = [&](int kind) { return word(save_.header, kRoyalForce + 2 * static_cast<std::size_t>(kind)); };
    const int regulars = count(0);
    const int dragoons = count(1);
    const int menOfWar = count(2);
    const int artillery = count(3);
    int kind = 0;
    if ((regulars + 2) / 3 > dragoons) {
        kind = 1;
    }
    if (regulars / 4 > artillery) {
        kind = 3;
    }
    if ((regulars + dragoons + artillery + 5) / 10 > menOfWar) {
        kind = 2;
    }
    const int type = royalUnitType(kind, word(save_.header, kFirstHalf));
    setWord(save_.header, kRoyalForce + 2 * static_cast<std::size_t>(kind), count(kind) + 1);
    GameNotice notice{"KINGBUY", {rules_.units[static_cast<std::size_t>(type)].name}};
    notices_.push_back(std::move(notice));
    setLong32(n.raw, kCrownFund, fund - kArmyPrice);
    int colonists = 0;
    for (const auto& u : save_.units) {
        colonists += u.nation == nation && u.type == unit_type::kColonist ? 1 : 0;
    }
    setWord(n.raw, kBellsToo, word(n.raw, kBellsToo) + colonists);
}

void Game::rebelSentiment(int nation) {
    if (nation < 0 || nation >= kEuropeanNations) {
        return;
    }
    if ((save_.header[kIndependence] & 0x01) != 0) {
        return; // the war of independence counts otherwise (not done yet)
    }
    const bool human = nation == this->human();
    if (human) {
        kingArmyGrowth(nation);
    }
    const int percent = nationRebelPercent(nation);
    save_.nations[static_cast<std::size_t>(nation)].raw[kRebelShare] = static_cast<std::uint8_t>(percent);
    if (!human) {
        return;
    }
    setWord(save_.header, kHumanRebels, percent);
    // At half the people the War of the Spanish Succession may come (not
    // done yet).
    if (nationPeople(nation) < 4) {
        return;
    }
    const int told = word(save_.header, kRebelsTold);
    GameNotice notice;
    notice.numbers = {percent};
    notice.countries = {{0, nation}};
    if (percent / 10 > told) {
        notice.section = percent >= 50 ? "REBELUP50" : "REBELUP";
        notice.music = {MusicCue{MusicCue::Kind::ChangeMood, 3}};
    } else if ((percent + 4) / 10 < told) {
        notice.section = "REBELDOWN";
        notice.music = {MusicCue{MusicCue::Kind::ChangeMood, 1}};
    } else {
        return;
    }
    notices_.push_back(std::move(notice));
    setWord(save_.header, kRebelsTold, percent / 10);
}

void Game::offerMercenaries() {
    // Read from the original: one turn in 21 a European king (perhaps the
    // player's own) offers a few veteran dragoons and an artillery or two,
    // if the player has met his nation and can pay.
    if ((save_.header[kIndependence] & 0x01) != 0 || aiRandom(0, 20) != 0) {
        return;
    }
    const int nation = human();
    const int country = aiRandom(0, kEuropeanNations - 1);
    constexpr std::size_t kRelations = 52;
    constexpr std::uint8_t kMet = 0x40;
    if (country != nation &&
        (save_.nations[static_cast<std::size_t>(country)].raw[kRelations + static_cast<std::size_t>(nation)] & kMet) ==
            0) {
        return;
    }
    MercenaryOffer offer;
    offer.country = country;
    offer.dragoons = aiRandom(1, 3);
    if (aiRandom(0, 1) != 0) {
        ++offer.dragoons;
    } else {
        offer.artillery = aiRandom(0, 1) != 0 ? 1 : 2;
    }
    // A gun costs as much as two dragoons.
    const int each = (aiRandom(0, 6) + 2 * (save_.difficulty + 4)) * 100;
    offer.price = (offer.dragoons + 2 * offer.artillery) * each;
    if (save_.nations[static_cast<std::size_t>(nation)].gold < offer.price) {
        return;
    }
    // "3 Dragoons, 2 Artillery": the artillery's number only when more than one.
    std::string force = std::to_string(offer.dragoons) + " " + rules_.units[unit_type::kDragoon].name;
    if (offer.artillery > 0) {
        force += ", ";
        if (offer.artillery > 1) {
            force += std::to_string(offer.artillery) + " ";
        }
        force += rules_.units[kArtillery].name;
    }
    GameNotice notice{"MERCENARIES", {"", force}, kMercenaryQuestion, {offer.price}};
    notice.countries.emplace_back(0, country);
    notices_.push_back(std::move(notice));
    mercenaries_ = offer;
}

void Game::answerMercenaries(bool pay) {
    const MercenaryOffer offer = std::exchange(mercenaries_, {});
    if (!pay || offer.price <= 0) {
        return;
    }
    const int nation = human();
    save_.nations[static_cast<std::size_t>(nation)].gold -= offer.price;
    // They come by ship to one of the first ten coastal colonies, picked
    // by their people, and walk in (read from the original).
    std::vector<int> colonies;
    int people = 0;
    for (int c = 0; c < static_cast<int>(save_.colonies.size()) && colonies.size() < 10; ++c) {
        const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
        if (colony.nation == nation && (colony.raw[28] & kCoastal) != 0) {
            colonies.push_back(c);
            people += colony.population;
        }
    }
    if (colonies.empty() || people <= 0) {
        return; // the gold is gone all the same
    }
    int roll = aiRandom(1, people);
    int picked = colonies.front();
    for (const int c : colonies) {
        roll -= save_.colonies[static_cast<std::size_t>(c)].population;
        if (roll <= 0) {
            picked = c;
            break;
        }
    }
    const auto& colony = save_.colonies[static_cast<std::size_t>(picked)];
    constexpr int kVeteranSoldier = 0x15;
    auto land = [&](int type, int count) {
        for (int i = 0; i < count; ++i) {
            addBuiltUnit(colony.x, colony.y, nation, type);
            save_.units.back().profession = kVeteranSoldier;
        }
    };
    land(royalUnitType(0, nation), offer.dragoons);
    land(royalUnitType(3, nation), offer.artillery);
    GameNotice notice{"MERCS", {colony.name, ""}};
    notice.adjectives.emplace_back(1, offer.country);
    notices_.push_back(std::move(notice));
}

void Game::treasureArrives(int index) {
    // Read from the original: when the player's treasure enters a colony by
    // the sea and there is no galleon to carry it (or Cortes is in
    // congress), the King offers to; after independence foreign agents buy
    // it outright.
    const auto& u = save_.units[static_cast<std::size_t>(index)];
    const int nation = u.nation;
    if (nation < 0 || nation >= kEuropeanNations || save_.players[static_cast<std::size_t>(nation)].control != 0) {
        return;
    }
    const auto colony = colonyAt(u.x, u.y);
    if (!colony || (save_.colonies[static_cast<std::size_t>(*colony)].raw[28] & kCoastal) == 0) {
        return;
    }
    constexpr int kCortes = 10;
    constexpr int kGalleon = 15;
    const bool independence = (save_.header[kIndependence] & 0x01) != 0;
    bool galleon = false;
    for (const auto& other : save_.units) {
        galleon = galleon || (other.nation == nation && other.type == kGalleon);
    }
    const bool cortes = hasFather(nation, kCortes);
    if (galleon && !independence && !cortes) {
        return;
    }
    const int amount = u.profession * 100;
    if (independence) {
        GameNotice notice{"CASHTREASURE", {}, -1, {amount}};
        notice.music = {MusicCue{MusicCue::Kind::MoodEvent, 2}};
        notices_.push_back(std::move(notice));
        cashTreasure(index, 0);
        return;
    }
    GameNotice notice{cortes ? "KINGGALLEON3" : "KINGGALLEON2",
                      {"", save_.players[static_cast<std::size_t>(nation)].name, ""},
                      kTreasureQuestion,
                      {save_.nations[static_cast<std::size_t>(nation)].taxRate}};
    notice.lines.push_back({0, true, "DIFFICULTY", save_.difficulty});
    notice.countries.emplace_back(2, nation);
    notice.music = {MusicCue{MusicCue::Kind::Sound, sounds::kKing}};
    notices_.push_back(std::move(notice));
    treasure_ = index;
}

void Game::answerTreasure(bool ship) {
    const int index = std::exchange(treasure_, -1);
    if (!ship || index < 0 || index >= unitCount() || unit(index).type != unit_type::kTreasure) {
        return;
    }
    const int nation = unit(index).nation;
    // The Crown's share: the tax rate with Cortes, otherwise twice the tax
    // but at least 50% (5% more each level), never more than 90%.
    int share = save_.nations[static_cast<std::size_t>(nation)].taxRate;
    if (!hasFather(nation, 10)) {
        share = std::max(5 * (save_.difficulty + 10), 2 * share);
    }
    share = std::min(share, 90);
    const int amount = unit(index).profession * 100;
    const int crown = static_cast<int>(static_cast<long>(amount) * share / 100);
    GameNotice notice{"LOOTCASH", {"", ""}, -1, {amount, share, amount - crown}};
    notice.adjectives.emplace_back(0, nation);
    notice.ports.emplace_back(1, nation);
    notice.music = {MusicCue{MusicCue::Kind::MoodEvent, 2}};
    notices_.push_back(std::move(notice));
    auto& raw = save_.nations[static_cast<std::size_t>(nation)].raw;
    setLong32(raw, kCrownFund, long32(raw, kCrownFund) + crown);
    cashTreasure(index, share);
}

void Game::cashTreasure(int index, int crownPercent) {
    const int nation = unit(index).nation;
    const int amount = unit(index).profession * 100;
    const int mine = amount - static_cast<int>(static_cast<long>(amount) * crownPercent / 100);
    auto& n = save_.nations[static_cast<std::size_t>(nation)];
    n.gold += mine;
    // The original also counts it at byte 38 (32 bits), next to the gold.
    constexpr std::size_t kTreasureTotal = 0x26;
    setLong32(n.raw, kTreasureTotal, long32(n.raw, kTreasureTotal) + mine);
    removeUnit(index);
}

} // namespace viceroy
