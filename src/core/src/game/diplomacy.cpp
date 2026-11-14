// SPDX-License-Identifier: GPL-3.0-or-later
//
// The European nations among themselves: a unit that comes to stand next to
// another nation's men or town brings its leader to talk (with the player)
// or settles peace and war at once (between two computer nations). Read
// from the original with a debugger and a disassembler (we only looked at
// what it does and wrote it down in our own words); see
// docs/rules/diplomacy.md.
#include "viceroy/game/colony.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"

#include <algorithm>
#include <array>

namespace viceroy {

namespace {

// The counts in the block after the tribe records (see euro_ai.cpp).
constexpr std::size_t kExtra = static_cast<std::size_t>(kTribes) * kTribeRecordSize;
constexpr std::size_t kTraits = kExtra + 0;
constexpr std::size_t kColonyCount = kExtra + 16;
constexpr std::size_t kColonyPeople = kExtra + 24;
constexpr std::size_t kPeople = kExtra + 28;
constexpr std::size_t kAttack = kExtra + 44; // words
constexpr std::size_t kAttackOut = kExtra + 60;
constexpr std::size_t kShips = kExtra + 40;
constexpr std::size_t kByType = kExtra + 64; // 19 per nation
constexpr std::size_t kRegionUnits = kExtra + 172;
constexpr std::size_t kRegionColonies = kExtra + 236;
constexpr std::size_t kRegionAttackOut = kExtra + 300;
constexpr std::size_t kRegionPeople = kExtra + 364;
constexpr std::size_t kRegionAttack = kExtra + 492;
constexpr std::size_t kTribeStrength = kExtra + 565;
constexpr std::size_t kBlockEnd = kExtra + 727;
constexpr std::size_t kRegions = 16;

constexpr std::size_t kRelations = 52;
constexpr std::size_t kRelationTimers = 64;
constexpr std::size_t kTribeRelations = 58;
constexpr std::size_t kTribeFlags = 3;
constexpr std::uint8_t kTribeGone = 0x80;
constexpr std::size_t kReturnX = 50;
constexpr std::size_t kReturnY = 51;

constexpr std::uint8_t kMet = 0x20;
constexpr std::uint8_t kPeace = 0x40;
constexpr std::uint8_t kInsulted = 0x10;
constexpr std::uint8_t kWarIntent = 0x02;
constexpr std::uint8_t kGrudge = 0x08;
constexpr std::uint8_t kPiracy = 0x80;
constexpr std::uint8_t kUSA = 0x04; // byte 0 of the nation record

constexpr std::size_t kHeaderLastTalk = 88; // a word per nation
constexpr std::size_t kHeaderGoneNation = 98; // the nation gone after the War of the Spanish Succession, or -1
constexpr std::size_t kHeaderPlayers = 17;
constexpr std::uint8_t kSeveralHumans = 0x80;
constexpr std::size_t kHeaderIndependence = 18;
constexpr std::size_t kHeaderIntervention = 100; // the power that helps in the war of independence

constexpr int kFranklin = 19; // rows of @FATHERS
constexpr int kDeWitt = 4;
constexpr int kPrivateerType = 16;
constexpr int kFirstShip = 13;
constexpr int kLastShip = 18;
constexpr int kTalkTune = 0x8020; // plus the nation: its leader's tune
constexpr int kWarMood = 4;
constexpr int kSuccessionMood = 3;
constexpr int kStuffGood = 4; // see WANTSTUFF below
constexpr int kSeasonedScout = 0x16; // the scout's profession
constexpr int kStolenHorses = 100;
constexpr int kSoldierType = 1;
constexpr int kVeteranSoldier = 0x15;

constexpr std::array<int, 8> kDx{0, 1, 1, 1, 0, -1, -1, -1};
constexpr std::array<int, 8> kDy{-1, -1, 0, 1, 1, 1, 0, -1};

int byteAt(const std::vector<std::uint8_t>& block, std::size_t at) {
    return at < block.size() ? block[at] : 0;
}

int signedAt(const std::vector<std::uint8_t>& block, std::size_t at) {
    return static_cast<std::int8_t>(byteAt(block, at));
}

int wordAt(const std::vector<std::uint8_t>& block, std::size_t at) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(byteAt(block, at) | (byteAt(block, at + 1) << 8)));
}

template <std::size_t N>
int headerWord(const std::array<std::uint8_t, N>& header, std::size_t at) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(header[at] | (header[at + 1] << 8)));
}

template <std::size_t N>
void setHeaderWord(std::array<std::uint8_t, N>& header, std::size_t at, int value) {
    const auto v = static_cast<std::uint16_t>(value);
    header[at] = static_cast<std::uint8_t>(v & 0xFF);
    header[at + 1] = static_cast<std::uint8_t>(v >> 8);
}

bool isShipTypeNumber(int type) { return type >= kFirstShip && type <= kLastShip; }

} // namespace

// --- Relations ---------------------------------------------------------------

int Game::relation(int nation, int other) const {
    if (nation < 0 || nation >= kEuropeanNations || other < 0 || other >= kEuropeanNations + kTribes) {
        return 0;
    }
    return save_.nations[static_cast<std::size_t>(nation)].raw[kRelations + static_cast<std::size_t>(other)];
}

void Game::addRelationBits(int nation, int other, std::uint8_t bits) {
    // Both ways, as the original does: a tribe keeps its side in its record.
    if (nation < 0 || nation >= kEuropeanNations || other < 0 || other >= kEuropeanNations + kTribes) {
        return;
    }
    auto& mine = save_.nations[static_cast<std::size_t>(nation)].raw[kRelations + static_cast<std::size_t>(other)];
    mine = static_cast<std::uint8_t>(mine | bits);
    if (other < kEuropeanNations) {
        auto& theirs = save_.nations[static_cast<std::size_t>(other)].raw[kRelations + static_cast<std::size_t>(nation)];
        theirs = static_cast<std::uint8_t>(theirs | bits);
        return;
    }
    const auto at = static_cast<std::size_t>(other - kEuropeanNations) * kTribeRecordSize + kTribeRelations +
                    static_cast<std::size_t>(nation);
    if (at < save_.tribesAndMore.size()) {
        save_.tribesAndMore[at] = static_cast<std::uint8_t>(save_.tribesAndMore[at] | bits);
    }
}

void Game::clearRelationBits(int nation, int other, std::uint8_t bits) {
    if (nation < 0 || nation >= kEuropeanNations || other < 0 || other >= kEuropeanNations + kTribes) {
        return;
    }
    const auto keep = static_cast<std::uint8_t>(~bits);
    auto& mine = save_.nations[static_cast<std::size_t>(nation)].raw[kRelations + static_cast<std::size_t>(other)];
    mine = static_cast<std::uint8_t>(mine & keep);
    if (other < kEuropeanNations) {
        auto& theirs = save_.nations[static_cast<std::size_t>(other)].raw[kRelations + static_cast<std::size_t>(nation)];
        theirs = static_cast<std::uint8_t>(theirs & keep);
        return;
    }
    const auto at = static_cast<std::size_t>(other - kEuropeanNations) * kTribeRecordSize + kTribeRelations +
                    static_cast<std::size_t>(nation);
    if (at < save_.tribesAndMore.size()) {
        save_.tribesAndMore[at] = static_cast<std::uint8_t>(save_.tribesAndMore[at] & keep);
    }
}

int Game::lastTalk(int nation) const {
    return headerWord(save_.header, kHeaderLastTalk + 2 * static_cast<std::size_t>(nation));
}

int Game::strongestNation() const {
    for (int n = 0; n < kEuropeanNations; ++n) {
        if (aiRank(n) == kEuropeanNations - 1) {
            return n;
        }
    }
    return -1;
}

void Game::unfortifyNear(int owner, int nation) {
    // Peace: the nation's soldiers next to the other's colonies stop
    // fortifying there.
    for (auto& u : save_.units) {
        if (u.nation != nation || isShipTypeNumber(u.type) || u.type >= static_cast<int>(rules_.units.size()) ||
            rules_.units[static_cast<std::size_t>(u.type)].combat <= 1 || !save_.map.inside(u.x, u.y)) {
            continue;
        }
        bool near = false;
        for (std::size_t d = 0; d < kDx.size() && !near; ++d) {
            if (const auto c = colonyAt(u.x + kDx[d], u.y + kDy[d])) {
                near = save_.colonies[static_cast<std::size_t>(*c)].nation == owner;
            }
        }
        if (near && (u.orders == SaveUnit::kFortify || u.orders == SaveUnit::kFortified)) {
            u.orders = SaveUnit::kNoOrders;
        }
    }
}

// --- Two computer nations ----------------------------------------------------

bool Game::wantsWar(int a, int b) const {
    const auto& block = save_.tribesAndMore;
    if (block.size() < kBlockEnd) {
        return false;
    }
    const int h = human();
    if (strongestNation() == h || save_.turn < 40) {
        return false;
    }
    const auto ua = static_cast<std::size_t>(a);
    const auto ub = static_cast<std::size_t>(b);
    const auto uh = static_cast<std::size_t>(h);
    if (byteAt(block, kColonyPeople + ua) < 8 && byteAt(block, kColonyPeople + ub) < 8) {
        return false;
    }
    if ((save_.nations[ua].raw[0] & kUSA) != 0 || (save_.nations[ub].raw[0] & kUSA) != 0) {
        return false;
    }
    const auto atWarWith = [&](int n, int other) { return (relation(n, other) & (kMet | kPeace)) == kMet; };
    // A nation at war with a stronger player keeps out of other wars.
    for (const auto n : {ua, ub}) {
        if (h >= 0 && atWarWith(static_cast<int>(n), h) &&
            (wordAt(block, kAttack + 2 * uh) > wordAt(block, kAttack + 2 * n) ||
             byteAt(block, kPeople + uh) > byteAt(block, kPeople + n))) {
            return false;
        }
    }
    int wars = 0;
    for (int e = 0; e < kEuropeanNations; ++e) {
        if (e != a && e != b && atWarWith(a, e)) {
            ++wars;
        }
    }
    if (!atWarWith(a, b)) {
        ++wars;
    }
    wars -= signedAt(block, kTraits + 3 * ua);
    int attack = 0;
    int defence = 1;
    bool touch = false;
    for (std::size_t r = 1; r < 15; ++r) {
        const int units = byteAt(block, kRegionUnits + kRegions * ua + r);
        const int mine = byteAt(block, kRegionAttack + kRegions * ua + r);
        const int theirs = byteAt(block, kRegionAttack + kRegions * ub + r);
        if (byteAt(block, kRegionColonies + kRegions * ua + r) != 0 && (units >> 1) + (mine >> 1) < theirs) {
            return false; // its colonies there are not safe
        }
        const int theirUnits = byteAt(block, kRegionUnits + kRegions * ub + r);
        if (mine != 0 && theirUnits != 0) {
            attack += mine;
            defence += (theirs + theirUnits) >> 1;
            touch = true;
        }
    }
    return !touch || 4 * attack / defence >= wars + 4;
}

void Game::computerDiplomacy(int a, int b) {
    if ((save_.header[kHeaderIndependence] & 1) != 0) {
        return;
    }
    if ((a + save_.turn + b) % 3 != 0 && (relation(a, b) & kMet) != 0) {
        return;
    }
    if ((relation(a, b) & kWarIntent) != 0 || (relation(b, a) & kWarIntent) != 0) {
        return;
    }
    auto& timerA = save_.nations[static_cast<std::size_t>(a)].raw[kRelationTimers + static_cast<std::size_t>(b)];
    auto& timerB = save_.nations[static_cast<std::size_t>(b)].raw[kRelationTimers + static_cast<std::size_t>(a)];
    GameNotice news;
    news.adjectives = {{0, a}, {1, b}};
    news.value = kForeignNews;
    if (!wantsWar(a, b) && !wantsWar(b, a)) {
        if (atPeace(a, b)) {
            return;
        }
        // "The French and Spanish have signed a peace treaty."
        news.section = "SIGNTREATY";
        notices_.push_back(news);
        addRelationBits(a, b, kPeace);
        unfortifyNear(a, b);
        unfortifyNear(b, a);
        timerA = 1;
        timerB = 1;
        return;
    }
    const bool peace = atPeace(a, b);
    if (!peace && (relation(a, b) & kMet) != 0) {
        return;
    }
    // The original asks for CANCELTREATY when a treaty breaks, a section
    // its GAME.TXT does not have: nothing is shown then.
    if (!peace) {
        news.section = "DECLAREWAR"; // "The French and Spanish are now at war."
        notices_.push_back(news);
    }
    timerA = 0;
    timerB = 0;
    clearRelationBits(a, b, kPeace);
}

// --- Contact -------------------------------------------------------------------

void Game::meetEuropean(int a, int b, int mover, int dir, bool& met) {
    // `a` is the player's side when the player is one of the two.
    met = false;
    if (a < 0 || a >= kEuropeanNations || b < 0 || b >= kEuropeanNations) {
        return;
    }
    if (save_.players[static_cast<std::size_t>(a)].control != 0) {
        computerDiplomacy(a, b);
        met = true;
        endMeeting(a, b);
        return;
    }
    if ((save_.header[kHeaderIndependence] & 1) != 0) {
        endMeeting(a, b);
        return;
    }
    met = startMeeting(a, b, mover, dir, false);
    if (!met) {
        endMeeting(a, b);
    }
}

void Game::endMeeting(int a, int b) {
    // At peace, the computer leaves the player alone for a while.
    if (!atPeace(a, b)) {
        return;
    }
    int timer = 2 * (6 - save_.difficulty);
    if (hasFather(a, kFranklin)) {
        timer >>= 1;
    }
    save_.nations[static_cast<std::size_t>(b)].raw[kRelationTimers + static_cast<std::size_t>(a)] =
        static_cast<std::uint8_t>(timer);
}

// --- Talking with the player ---------------------------------------------------

bool Game::startMeeting(int a, int b, int mover, int dir, bool forced) {
    const auto& block = save_.tribesAndMore;
    bool talk = forced;
    if ((relation(a, b) & kMet) == 0) {
        talk = true;
    }
    if (lastTalk(b) + 16 <= save_.turn) {
        talk = true;
        clearRelationBits(a, b, kInsulted);
    }
    Meeting m;
    m.a = a;
    m.b = b;
    m.forced = forced;
    m.insulted = (relation(a, b) & kInsulted) != 0;
    if (!talk) {
        return false;
    }
    const auto ua = static_cast<std::size_t>(a);
    const auto ub = static_cast<std::size_t>(b);
    const int d = save_.difficulty;
    m.usa = (save_.nations[ub].raw[0] & kUSA) != 0;
    m.firstMeeting = (relation(a, b) & kMet) == 0;
    m.shipMover = mover >= 0 && mover < unitCount() && isShipTypeNumber(unit(mover).type);
    int previous = lastTalk(b);
    setHeaderWord(save_.header, kHeaderLastTalk + 2 * ub, save_.turn);
    const bool census = block.size() >= kBlockEnd;
    const auto at = [&](std::size_t base, std::size_t n, std::size_t r) {
        return census ? byteAt(block, base + kRegions * n + r) : 0;
    };
    const int colonies = census ? byteAt(block, kColonyCount + ub) : 0;

    // The regions: the player's men near the computer's colonies make it
    // meek, its own men outside make it bold.
    int threat = 0;
    int amount = 0;
    bool hostile = false;
    for (std::size_t r = 1; r < 15; ++r) {
        const int few = colonies > 1 ? 1 : 0;
        const int theirUnits = at(kRegionUnits, ub, r);
        const int myOut = at(kRegionAttackOut, ua, r);
        const int theirOut = at(kRegionAttackOut, ub, r);
        if (at(kRegionColonies, ub, r) > few && theirUnits < myOut) {
            threat += (myOut / (theirUnits + 1)) << (d == 0 ? 2 : 1);
            continue;
        }
        if (myOut != 0 && theirOut != 0) {
            hostile = true;
        }
        if (theirOut != 0 && at(kRegionPeople, ua, r) > 4) {
            hostile = true;
        }
        if (at(kRegionColonies, ua, r) != 0) {
            amount += at(kRegionColonies, ub, r) > 1 ? theirOut - myOut : theirOut;
        } else {
            amount += (theirOut - myOut) >> (at(kRegionColonies, ub, r) == 0 ? 2 : 1);
        }
    }
    // The computer's soldiers next to colonies (its own and the player's).
    m.forces = 0;
    m.forcesNear = false;
    for (int c = 0; c < static_cast<int>(save_.colonies.size()); ++c) {
        const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
        if (colony.nation != a && colony.nation != b) {
            continue;
        }
        int defenders = 0;
        for (const int i : unitsAt(colony.x, colony.y)) {
            const auto& u = unit(i);
            if (!isShipTypeNumber(u.type) && u.type < static_cast<int>(rules_.units.size()) &&
                rules_.units[static_cast<std::size_t>(u.type)].combat > 1) {
                ++defenders;
            }
        }
        int sum = 0;
        int count = 0;
        bool found = false;
        for (std::size_t dd = 0; dd < kDx.size(); ++dd) {
            const auto here = unitsAt(colony.x + kDx[dd], colony.y + kDy[dd]);
            if (here.empty() || unit(here.front()).nation != b) {
                continue;
            }
            found = true;
            const bool water = save_.map.at(colony.x + kDx[dd], colony.y + kDy[dd]).isWater();
            int force = 0;
            for (const int i : here) {
                const auto& u = unit(i);
                if (isShipTypeNumber(u.type) == water) {
                    force += unitForce(u, false);
                }
                if (!isShipTypeNumber(u.type) && u.type < static_cast<int>(rules_.units.size()) &&
                    rules_.units[static_cast<std::size_t>(u.type)].combat > 1) {
                    ++count;
                }
            }
            sum += force >> 3;
        }
        if (!found) {
            continue;
        }
        amount += 2 * sum;
        if (defenders == 0 || count > 1) {
            if (threat != 0) {
                --threat;
            }
            hostile = true;
        }
        const int region = landRegion(save_.map, colony.x, colony.y);
        if (region < 0 || at(kRegionColonies, ub, static_cast<std::size_t>(region)) == 0) {
            sum *= 2;
        }
        m.forces += sum;
        m.forcesNear = true;
    }

    const bool aggressive = strongestNation() == a && save_.turn >= 80 && census &&
                            byteAt(block, kColonyCount + ua) > 3 && colonies > 1;
    m.warIntent = (relation(b, a) & kWarIntent) != 0;
    const int myAttackOut = census ? byteAt(block, kAttackOut + ua) : 0;
    const int theirAttackOut = census ? byteAt(block, kAttackOut + ub) : 0;
    // The original keeps three times the attack in a byte.
    const int threeTimes = static_cast<std::uint8_t>(3 * theirAttackOut);
    if (aggressive || (m.warIntent && threeTimes > myAttackOut)) {
        hostile = true;
        threat = 0;
        amount = std::clamp(amount, 100 + 200 * d, 9900);
    }
    if (m.insulted) {
        hostile = true;
    }
    bool manly = false;
    if (threat != 0) {
        hostile = false;
    }
    bool calm = aggressive;
    const bool franklin = hasFather(a, kFranklin);
    if (franklin || (!m.warIntent && 10 * (10 - d) > save_.turn)) {
        calm = false;
        hostile = false;
        previous = std::max(previous, 0);
    }
    amount = amount * (d + 8) * 10 / 100;
    if (m.warIntent) {
        amount *= 2;
    } else if (previous < 0) {
        amount >>= 2;
    } else {
        if (save_.turn < 50) {
            amount >>= 1;
        } else if (save_.turn < 100) {
            amount -= amount >> 2;
        }
        if (census && byteAt(block, kColonyCount + ua) <= 2 && byteAt(block, kPeople + ua) < 8) {
            amount >>= 1;
        }
    }
    amount = std::clamp((d + 1) * amount >> 3, 0, 400) * 50;
    if (m.insulted) {
        amount += 500 * (d + 1);
    }
    const auto gold = save_.nations[ua].gold;
    if (gold < amount && 2 * gold > amount && gold >= 300) {
        amount = gold / 50 * 50;
    }
    if (franklin) {
        amount >>= 1;
    }
    if (amount == 0 || threeTimes < myAttackOut) {
        hostile = false;
    }
    if (hostile) {
        manly = true;
    }
    m.wasHostile = hostile;

    // What the computer may ask for: goods from the player's colony next to
    // its unit, the most valuable it has room for.
    m.good = -1;
    if (mover >= 0 && mover < unitCount() && unit(mover).nation == b && dir >= 0 && dir < 8) {
        const auto& u = unit(mover);
        const int theirs = nearestColony(save_, u.x, u.y, b);
        const auto mine = colonyAt(u.x + kDx[static_cast<std::size_t>(dir)], u.y + kDy[static_cast<std::size_t>(dir)]);
        if (theirs >= 0 && mine) {
            const auto& home = save_.colonies[static_cast<std::size_t>(theirs)];
            const auto& target = save_.colonies[static_cast<std::size_t>(*mine)];
            const int room = storageLimit(home);
            const int least = (d - 2) * (amount / 10) + amount / 2;
            int best = 0;
            for (int g = 0; g < kGoodsKinds; ++g) {
                const int q = std::min(room - home.stock[static_cast<std::size_t>(g)],
                                       static_cast<int>(target.stock[static_cast<std::size_t>(g)]));
                const int value = q * bidPrice(save_.nations[ub], g);
                if (value >= least && value >= best) {
                    best = value;
                    m.good = g;
                    m.goodAmount = q;
                }
            }
            m.homeColony = theirs;
            m.targetColony = *mine;
        }
    }

    // How friendly: troubles with tribes and other nations make it keener
    // on peace with the player; a tribe or nation the player is friends with
    // may be named.
    int friendship = m.warIntent ? -2 : 0;
    m.target = -1;
    for (int t = 0; t < kTribes; ++t) {
        const auto tribeAt = static_cast<std::size_t>(t) * kTribeRecordSize;
        if (byteAt(block, tribeAt + kTribeFlags) & kTribeGone) {
            continue;
        }
        if (tribeTension(save_, t, b) < 75 && (relation(a, kEuropeanNations + t) & kWarIntent) == 0) {
            continue;
        }
        if (census && wordAt(block, kAttack + 2 * ub) < byteAt(block, kTribeStrength + static_cast<std::size_t>(t))) {
            ++friendship;
        }
        ++friendship;
        if (tribeTension(save_, t, a) < 75 && (relation(a, kEuropeanNations + t) & kWarIntent) == 0) {
            m.target = kEuropeanNations + t;
        }
    }
    const int gone = headerWord(save_.header, kHeaderGoneNation);
    for (int e = 0; e < kEuropeanNations; ++e) {
        if (e == a || e == b || e == gone || (relation(b, e) & (kMet | kPeace)) != kMet) {
            continue;
        }
        const int theirAttack = wordAt(block, kAttack + 2 * ub);
        const int otherAttack = wordAt(block, kAttack + 2 * static_cast<std::size_t>(e));
        if (4 * otherAttack > theirAttack) {
            ++friendship;
        }
        if (otherAttack > theirAttack) {
            ++friendship;
        }
        if ((relation(a, e) & kPeace) != 0) {
            m.target = e;
        }
    }
    friendship -= signedAt(block, kTraits + 3 * ub);
    if (wordAt(block, kAttack + 2 * ub) > wordAt(block, kAttack + 2 * ua)) {
        --friendship;
    }
    if (!calm && (atPeace(a, b) ? 0 : 1) < friendship) {
        hostile = false;
        if (aiRandom(0, 1) != 0) {
            amount = 0;
        }
    }
    if (!forced && !hostile && threat != 0 && atPeace(a, b)) {
        endMeeting(a, b);
        return true; // they talked, and nothing was said
    }
    if (hostile && atPeace(a, b) && (m.warIntent || wordAt(block, kAttack + 2 * ub) < wordAt(block, kAttack + 2 * ua))) {
        m.grudge = true;
        hostile = false;
        manly = false;
    }
    m.threat = threat;
    m.amount = amount;
    m.hostile = hostile;
    m.manly = manly;

    // "Greetings, Viceroy Peter Minuit, and welcome to New France."
    GameNotice hello = leaderNotice(m, "HELLO");
    hello.section = m.usa ? "HELLOUSA"
                    : !m.firstMeeting ? std::string("HELLO") + (manly ? "MANLY" : "MEEK")
                    : m.shipMover      ? "HELLOAHOY"
                                       : "HELLOFIRST";
    hello.lines = {{0, true, "DIFFICULTY", d, " " + save_.players[ua].name},
                   {2, false, "GREATKINGS", b},
                   {3, false, "GREATDEEDS", b}};
    hello.strings = {"", save_.players[ub].country, "", ""};
    m.section = hello.section;
    hello.music = {{MusicCue::Kind::Sound, kTalkTune + b}};
    notices_.push_back(std::move(hello));
    m.stage = Meeting::Stage::Target;
    meetings_.push_back(m);
    runMeeting();
    return true;
}

GameNotice Game::leaderNotice(const Meeting& m, std::string section) const {
    GameNotice notice;
    notice.section = std::move(section);
    notice.leader = m.b;
    return notice;
}

void Game::sendHome(int index, int nation) {
    // The unit goes to the docks of the home port and will come back to the
    // nation's landing square.
    auto& u = save_.units[static_cast<std::size_t>(index)];
    setEuropeStage(u, 2);
    u.orders = SaveUnit::kNoOrders;
    const auto& raw = save_.nations[static_cast<std::size_t>(nation)].raw;
    u.raw[9] = raw[kReturnX];
    u.raw[10] = raw[kReturnY];
}

void Game::withdrawFrom(int owner, int nation, bool soldiersOnly) {
    // The nation's men on land next to the owner's colonies go home.
    for (int i = 0; i < unitCount(); ++i) {
        const auto& u = unit(i);
        if (u.nation != nation || isShipTypeNumber(u.type) || !onMap(i) || save_.map.at(u.x, u.y).isWater()) {
            continue;
        }
        if (soldiersOnly && (u.type >= static_cast<int>(rules_.units.size()) ||
                             rules_.units[static_cast<std::size_t>(u.type)].combat <= 1)) {
            continue;
        }
        bool near = false;
        for (std::size_t d = 0; d < kDx.size() && !near; ++d) {
            if (const auto c = colonyAt(u.x + kDx[d], u.y + kDy[d])) {
                near = save_.colonies[static_cast<std::size_t>(*c)].nation == owner;
            }
        }
        if (near) {
            sendHome(i, nation);
        }
    }
    refreshUnitFlags();
}

void Game::runMeeting() {
    while (!meetings_.empty()) {
        Meeting& m = meetings_.front();
        if (m.waiting) {
            return;
        }
        if (!stepMeeting(m)) {
            const int a = m.a;
            const int b = m.b;
            const bool grudge = m.grudge;
            meetings_.pop_front();
            if (grudge) {
                auto& r = save_.nations[static_cast<std::size_t>(b)].raw[kRelations + static_cast<std::size_t>(a)];
                r = static_cast<std::uint8_t>(r | kGrudge);
            }
            endMeeting(a, b);
        }
    }
}

void Game::askMeeting(Meeting& m, GameNotice notice) {
    notice.value = kMeetingQuestion;
    notices_.push_back(std::move(notice));
    m.waiting = true;
}

void Game::answerMeeting(int choice) {
    if (meetings_.empty()) {
        return;
    }
    Meeting& m = meetings_.front();
    m.answer = choice + 1; // the original counts the choices from 1
    m.waiting = false;
    runMeeting();
}

bool Game::meetingWaiting() const {
    return !meetings_.empty() && meetings_.front().waiting;
}

std::vector<std::pair<int, std::string>> Game::allianceChoices() const {
    std::vector<std::pair<int, std::string>> out;
    if (meetings_.empty()) {
        return out;
    }
    return meetings_.front().allies;
}

bool Game::stepMeeting(Meeting& m) {
    // One step of the talk; false when it is over.
    using S = Meeting::Stage;
    const int a = m.a;
    const int b = m.b;
    const auto ua = static_cast<std::size_t>(a);
    const auto ub = static_cast<std::size_t>(b);
    const auto& block = save_.tribesAndMore;
    const int d = save_.difficulty;
    auto& goldA = save_.nations[ua].gold;
    auto& goldB = save_.nations[ub].gold;
    const auto tone = [&]() { return std::string(m.manly ? "MANLY" : "MEEK"); };
    const auto pay = [&](int from, int to, int sum) {
        save_.nations[static_cast<std::size_t>(from)].gold -= sum;
        save_.nations[static_cast<std::size_t>(to)].gold += sum;
    };
    const auto say = [&](const std::string& section, GameNotice notice) {
        notice.section = section;
        notice.leader = b;
        notices_.push_back(std::move(notice));
    };
    const auto tribeName = [&](int t, bool adjectiveForm) {
        const auto& tribes = rules_.tribes;
        const auto i = static_cast<std::size_t>(t - kEuropeanNations);
        if (i >= tribes.size()) {
            return std::string();
        }
        return adjectiveForm ? tribes[i].adjective : tribes[i].name;
    };
    // A nation's or tribe's name in a %STRING: nations by their adjective
    // (the original's plural of a nation is the same word).
    const auto name = [&](GameNotice& notice, int string, int who, bool adjectiveForm) {
        if (notice.strings.size() <= static_cast<std::size_t>(string)) {
            notice.strings.resize(static_cast<std::size_t>(string) + 1);
        }
        if (who < kEuropeanNations) {
            notice.adjectives.push_back({string, who});
        } else {
            notice.strings[static_cast<std::size_t>(string)] = tribeName(who, adjectiveForm);
        }
    };
    const auto newLand = [&](int who) { return save_.players[static_cast<std::size_t>(who)].country; };
    const auto leaderLine = [&](GameNotice& notice, int string, const char* section) {
        notice.lines.push_back({string, false, section, b});
    };
    const auto meekness = [&](GameNotice& notice, int string, int row) {
        notice.lines.push_back({string, false, "MEEKNESS", row});
    };
    const auto warWords = [&](GameNotice& notice) {
        leaderLine(notice, 0, "GREATLEADER2");
        if (notice.strings.size() < 2) {
            notice.strings.resize(2);
        }
        notice.strings[1] = newLand(b);
        notice.music = {{MusicCue::Kind::ChangeMood, kWarMood}};
    };
    const int attackOutA = byteAt(block, kAttackOut + ua);
    const int attackOutB = byteAt(block, kAttackOut + ub);

    switch (m.stage) {
    case S::Target: {
        m.stage = S::Piracy;
        const int t = m.target;
        if (t < 0 || m.insulted || (relation(a, t) & kMet) == 0) {
            return true;
        }
        GameNotice ask = leaderNotice(m, "");
        std::string section;
        if (t < kEuropeanNations) {
            // "We note that you have signed a treaty with those unrepentant
            // heretics, the French." (the original picks "demand" for a meek
            // leader here, and "request" for a bold one)
            section = "APOSTATES";
            name(ask, 0, t, true);
            meekness(ask, 1, m.manly ? 0 : 1);
            if (m.usa) {
                section += "USA";
            }
        } else {
            section = "HEATHEN";
            ask.strings.resize(2);
            ask.strings[1] = tribeName(t, true);
            if (m.usa) {
                section += "USA";
                ask.strings[0] = newLand(b);
            } else {
                ask.strings[0] = tribeName(t, false);
            }
        }
        ask.section = section;
        m.section = section;
        m.stage = S::TargetAnswer;
        askMeeting(m, std::move(ask));
        return true;
    }
    case S::TargetAnswer:
        m.stage = S::Piracy;
        if (m.answer == 2) {
            // "Yes! Let us teach the Sioux a lesson!"
            m.hostile = false;
            if (aiRandom(0, 1) != 0) {
                m.amount = 0;
            }
            if (m.target < kEuropeanNations) {
                clearRelationBits(a, m.target, kPeace);
                auto& r = save_.nations[static_cast<std::size_t>(m.target)].raw[kRelations + ua];
                r = static_cast<std::uint8_t>(r | kWarIntent);
            } else {
                changeTension(m.target - kEuropeanNations, a, 100);
            }
        }
        return true;
    case S::Piracy: {
        m.stage = S::Sieges;
        if ((relation(b, a) & kPiracy) == 0 || m.insulted ||
            byteAt(block, kByType + 19 * ua + kPrivateerType) == 0) {
            return true;
        }
        // "The Queen is most displeased with the Dutch pirates ..."
        GameNotice ask = leaderNotice(m, m.usa ? "PIRACYUSA" : "PIRACY");
        m.section = ask.section;
        ask.strings.resize(4);
        if (m.usa) {
            ask.strings[0] = newLand(b);
        } else {
            leaderLine(ask, 0, "GREATLEADER");
        }
        ask.adjectives.push_back({1, a});
        ask.strings[2] = newLand(b);
        meekness(ask, 3, m.manly ? 1 : 0);
        m.stage = S::PiracyAnswer;
        askMeeting(m, std::move(ask));
        return true;
    }
    case S::PiracyAnswer: {
        m.stage = S::Sieges;
        if (m.answer != 2) {
            return true;
        }
        // "Very well, we shall withdraw our privateers to Europe."
        int sent = 0;
        for (int i = 0; i < unitCount(); ++i) {
            const auto& u = unit(i);
            if (u.nation != a || u.type != kPrivateerType) {
                continue;
            }
            if (u.x != static_cast<std::uint8_t>(a - 20)) {
                ++sent;
            }
            sendHome(i, a);
        }
        refreshUnitFlags();
        auto& r = save_.nations[ub].raw[kRelations + ua];
        r = static_cast<std::uint8_t>(r & ~kPiracy);
        if (sent != 0 && aiRandom(0, sent) != 0) {
            m.hostile = false;
        }
        m.amount /= sent + 1;
        return true;
    }
    case S::Sieges: {
        m.stage = S::Tribute;
        if (m.insulted) {
            return true;
        }
        // The player's soldiers outside the computer's colonies. The
        // original counts them in a way that never finds any (see
        // docs/rules/diplomacy.md), so it asks only a nation with fewer than
        // four people in its colonies.
        const int playerForces = 0;
        if ((byteAt(block, kColonyPeople + ub) >> 2) > playerForces) {
            if (playerForces <= 12 || aiRandom(0, 4) != 0) {
                return true;
            }
        }
        GameNotice ask = leaderNotice(m, m.usa ? "SIEGESUSA" : "SIEGES");
        m.section = ask.section;
        ask.strings.resize(4);
        if (m.usa) {
            ask.strings[0] = newLand(b);
        } else {
            leaderLine(ask, 0, "GREATLEADER");
        }
        ask.adjectives.push_back({1, a});
        ask.adjectives.push_back({2, b});
        meekness(ask, 3, m.manly ? 1 : 0);
        m.stage = S::SiegesAnswer;
        askMeeting(m, std::move(ask));
        return true;
    }
    case S::SiegesAnswer:
        m.stage = S::Tribute;
        if (m.answer == 2) {
            // The second choice withdraws the soldiers (for the United
            // States it is the other answer's text, as in the original).
            m.amount = std::max(0, m.amount);
            m.withdrew = true;
            m.hostile = false;
            withdrawFrom(b, a, true);
        }
        return true;
    case S::Tribute: {
        m.stage = S::Stuff;
        if (m.amount == 0 || !m.manly || goldA <= m.amount) {
            return true;
        }
        // "... in exchange for a 'donation' of 1500 to the 'Church'."
        GameNotice ask = leaderNotice(m, m.usa ? "TRIBUTEUSA" : "TRIBUTE");
        m.section = ask.section;
        ask.strings.resize(3);
        if (m.usa) {
            ask.strings[0] = newLand(b);
        } else {
            leaderLine(ask, 0, "GREATLEADER");
        }
        ask.adjectives.push_back({1, a});
        ask.strings[2] = newLand(b);
        ask.numbers = {m.amount};
        m.stage = S::TributeAnswer;
        askMeeting(m, std::move(ask));
        return true;
    }
    case S::TributeAnswer:
        m.stage = S::Stuff;
        if (m.answer == 2) {
            pay(a, b, m.amount);
            m.hostile = false;
        }
        m.amount = kPaidTribute;
        return true;
    case S::Stuff: {
        m.stage = S::Threats;
        if (!m.manly || m.amount == kPaidTribute || m.good < 0) {
            return true;
        }
        // "We demand 100 furs as reparations."
        GameNotice ask = leaderNotice(m, m.usa ? "WANTSTUFFUSA" : "WANTSTUFF");
        m.section = ask.section;
        ask.strings.resize(3);
        if (m.usa) {
            ask.strings[0] = newLand(b);
        } else {
            leaderLine(ask, 0, "GREATLEADER2");
        }
        ask.numbers = {m.goodAmount};
        ask.lines.push_back({1, true, "CARGO", m.good});
        ask.adjectives.push_back({2, b});
        m.stage = S::StuffAnswer;
        askMeeting(m, std::move(ask));
        return true;
    }
    case S::StuffAnswer:
        m.stage = S::Threats;
        if (m.answer == 2 && m.targetColony >= 0 && m.homeColony >= 0) {
            // The original moves the amount asked for, but always of the
            // fifth good (furs): the good it chose is lost by then.
            auto& from = save_.colonies[static_cast<std::size_t>(m.targetColony)].stock[kStuffGood];
            auto& to = save_.colonies[static_cast<std::size_t>(m.homeColony)].stock[kStuffGood];
            from = static_cast<std::int16_t>(from - m.goodAmount);
            to = static_cast<std::int16_t>(to + m.goodAmount);
            m.hostile = false;
        }
        return true;
    case S::Threats: {
        if (m.hostile && atPeace(a, b) && m.amount > 100) {
            // "We can no longer tolerate your foul provocations."
            say("PROVOKE", {});
            clearRelationBits(a, b, kPeace);
        } else if (m.hostile && m.amount == kPaidTribute) {
            GameNotice war;
            warWords(war);
            say("WARMANLY", std::move(war));
            clearRelationBits(a, b, kPeace);
        } else if (m.hostile) {
            GameNotice rid;
            leaderLine(rid, 0, "GREATLEADER2");
            rid.strings = {std::string(), newLand(b)};
            m.section = m.usa ? "RIDUSA" : "RID";
            say(m.section, std::move(rid));
        }
        m.old = false;
        if (m.hostile || atPeace(a, b)) {
            if (!m.hostile) {
                m.section = "OLDPEACE";
                m.old = true;
            }
            m.stage = S::Peace;
            return true;
        }
        if (m.usa) {
            m.answer = 1;
            m.stage = S::PartitionAnswer;
            return true;
        }
        // "... we propose a demarcation treaty ... Will you agree?"
        GameNotice ask = leaderNotice(m, "WORTHY");
        leaderLine(ask, 0, "GREATLEADER2");
        ask.adjectives.push_back({1, a});
        ask.adjectives.push_back({2, b});
        m.stage = S::PartitionAnswer;
        askMeeting(m, std::move(ask));
        return true;
    }
    case S::PartitionAnswer:
        if (m.answer == 1) {
            m.section = "PEACE";
            addRelationBits(a, b, kPeace);
            setHeaderWord(save_.header, kHeaderLastTalk + 2 * ub, save_.turn + 16);
            m.stage = S::Peace;
            return true;
        }
        if (!m.warIntent) {
            m.gift = std::clamp(2 * (m.threat - 2), 0, static_cast<int>(goldB / 100)) * 100;
            if (m.gift != 0) {
                m.manly = false;
                // "We will give you 300 if you will agree not to attack us."
                GameNotice ask = leaderNotice(m, "GIVECASH");
                ask.numbers = {m.gift};
                m.stage = S::GiftAnswer;
                askMeeting(m, std::move(ask));
                return true;
            }
        }
        m.stage = S::War;
        return true;
    case S::GiftAnswer:
        if (m.answer == 1) {
            addRelationBits(a, b, kPeace);
            pay(b, a, m.gift);
        }
        m.stage = S::War;
        return true;
    case S::War: {
        m.stage = S::Peace;
        if (!atPeace(a, b)) {
            GameNotice war;
            warWords(war);
            m.section = "WAR" + tone();
            say(m.section, std::move(war));
        }
        return true;
    }
    case S::Peace: {
        if (atPeace(a, b)) {
            unfortifyNear(a, b);
            unfortifyNear(b, a);
        }
        if (m.hostile || !atPeace(a, b)) {
            return false;
        }
        // "Go in peace", "withdraw your forces", "how much do you value
        // your lives", "an alliance".
        GameNotice ask = leaderNotice(m, "");
        std::string section = m.section + tone();
        ask.adjectives = {{0, a}, {1, b}};
        if (m.old) {
            ask.lines.push_back({2, true, "DIFFICULTY", d});
            ask.strings.resize(4);
            ask.strings[3] = save_.players[ua].name;
        }
        if (m.usa) {
            section = "PEACEUSA";
            ask.adjectives = {{1, a}};
            ask.strings.resize(std::max<std::size_t>(ask.strings.size(), 2));
            ask.strings[0] = newLand(b);
        }
        // After a gift the original still holds an earlier text's name
        // ("HELLOFIRST" + "MEEK"), which GAME.TXT does not have: no menu.
        constexpr std::string_view kMenus[] = {"PEACEMEEK", "PEACEMANLY", "OLDPEACEMEEK", "OLDPEACEMANLY",
                                               "PEACEUSA"};
        if (std::find(std::begin(kMenus), std::end(kMenus), section) == std::end(kMenus)) {
            return false;
        }
        ask.section = section;
        m.stage = S::PeaceAnswer;
        askMeeting(m, std::move(ask));
        return true;
    }
    case S::PeaceAnswer:
        if (m.answer == 2) {
            return askWithdrawal(m);
        }
        if (m.answer == 3) {
            // A threat: a gift, war, or laughter.
            if (hasFather(a, kFranklin) && aiRandom(0, 2) == 0) {
                ++m.threat;
            }
            const int gift = std::clamp(m.threat, 0, static_cast<int>(goldB / 100)) * 100;
            if (gift > 0) {
                GameNotice notice;
                notice.numbers = {gift};
                say("GIFTS", std::move(notice));
                pay(b, a, gift);
            } else if (m.wasHostile) {
                GameNotice notice;
                notice.music = {{MusicCue::Kind::ChangeMood, kWarMood}};
                clearRelationBits(a, b, kPeace);
                say("PROVOKE", std::move(notice));
            } else {
                say("THREATS", {});
            }
            return false;
        }
        if (m.answer == 4) {
            // "You must attack the infidel . . ." and the nations and tribes
            // the player has met.
            m.allies.clear();
            for (int i = 0; i < kEuropeanNations + kTribes; ++i) {
                if (i == a || i == b || (relation(a, i) & kMet) == 0) {
                    continue;
                }
                if (i >= kEuropeanNations) {
                    const auto tribeAt = static_cast<std::size_t>(i - kEuropeanNations) * kTribeRecordSize;
                    if (byteAt(block, tribeAt + kTribeFlags) & kTribeGone) {
                        continue;
                    }
                }
                m.allies.push_back({i, i < kEuropeanNations ? "" : tribeName(i, false)});
            }
            if (m.allies.empty()) {
                m.grudge = false; // the original leaves here before the grudge
                return false;
            }
            GameNotice ask = leaderNotice(m, "MILITARY");
            m.stage = S::AllianceChoice;
            askMeeting(m, std::move(ask));
            return true;
        }
        return false;
    case S::WithdrawAnswer:
        if (m.answer == 1) {
            pay(a, b, m.price);
            withdrawFrom(a, b, false);
        } else if (m.answer == 2) {
            // "Withdraw or perish": the odds of the player's men outside.
            int odds = attackOutB + attackOutA;
            if (m.warIntent) {
                odds *= 2;
            }
            if (attackOutA >= aiRandom(0, odds)) {
                say("WITHDRAW", {});
                withdrawFrom(a, b, false);
            } else {
                GameNotice war;
                warWords(war);
                say("WAR" + std::string(m.usa ? "MANLY" : tone()), std::move(war));
                clearRelationBits(a, b, kPeace);
            }
        }
        return false;
    case S::AllianceChoice: {
        const int pick = m.answer - 1;
        if (pick < 0 || static_cast<std::size_t>(pick) >= m.allies.size()) {
            return false;
        }
        const int t = m.allies[static_cast<std::size_t>(pick)].first;
        m.ally = t;
        GameNotice notice;
        name(notice, 0, t, false);
        if ((relation(b, t) & kMet) == 0) {
            say("NOCONTACT", std::move(notice));
            return false;
        }
        if ((relation(b, t) & kPeace) == 0) {
            say("ALREADYSMITE", std::move(notice));
            return false;
        }
        const long long fifties = goldA / 50;
        long long price;
        if (t >= kEuropeanNations) {
            const int strength = byteAt(block, kTribeStrength + static_cast<std::size_t>(t - kEuropeanNations));
            price = 3 * (fifties * strength) / 50;
        } else {
            const auto ut = static_cast<std::size_t>(t);
            price = fifties * (byteAt(block, kAttackOut + ut) + wordAt(block, kAttack + 2 * ut)) / 50;
        }
        m.price = static_cast<int>(std::clamp<long long>(price, 10, 200)) * 50;
        if (hasFather(a, kFranklin)) {
            m.price >>= 1;
        }
        GameNotice ask = leaderNotice(m, t >= kEuropeanNations ? "SMITEINDIANS" : "SMITEEUROPE");
        name(ask, 0, t, true);
        ask.numbers = {m.price};
        m.stage = S::AllianceAnswer;
        askMeeting(m, std::move(ask));
        return true;
    }
    case S::AllianceAnswer: {
        if (m.answer != 1) {
            return false;
        }
        if (goldA < m.price) {
            say("UNFORTUNATE", {});
            return false;
        }
        const int t = m.ally;
        clearRelationBits(b, t, kPeace);
        if (t < kEuropeanNations) {
            auto& r = save_.nations[static_cast<std::size_t>(t)].raw[kRelations + ub];
            r = static_cast<std::uint8_t>(r | kWarIntent);
        } else {
            addRelationBits(b, t, kWarIntent);
        }
        // "The French declare war on the Sioux."
        GameNotice news;
        name(news, 0, b, false);
        name(news, 1, t, false);
        news.music = {{MusicCue::Kind::ChangeMood, kWarMood}};
        say("MERCENARY", std::move(news));
        pay(a, b, m.price);
        return false;
    }
    }
    return false;
}

// --- Attacks between nations ------------------------------------------------

bool Game::europeanAttack(int a, int d) {
    if (a < 0 || a >= kEuropeanNations || d < 0 || d >= kEuropeanNations || a == d) {
        return true;
    }
    const auto ua = static_cast<std::size_t>(a);
    const auto ud = static_cast<std::size_t>(d);
    const bool humanAttacks = save_.players[ua].control == 0;
    if (!humanAttacks && atPeace(a, d) && save_.nations[ua].raw[kRelationTimers + ud] != 0) {
        return false;
    }
    if (save_.players[ud].control == 0 && (relation(d, a) & kPeace) != 0) {
        // "Sneak attack by the treacherous French!"
        GameNotice sneak{"SNEAK", {}};
        sneak.adjectives = {{0, a}};
        sneak.music = {{MusicCue::Kind::ChangeMood, kWarMood}};
        notices_.push_back(std::move(sneak));
    }
    if ((relation(a, d) & kPeace) != 0 || (relation(d, a) & kPeace) != 0) {
        if (!humanAttacks) {
            // "The French and Spanish are now at war."
            GameNotice news{"DECLAREWAR", {}};
            news.adjectives = {{0, a}, {1, d}};
            news.value = kForeignNews;
            notices_.push_back(std::move(news));
        } else if ((relation(d, a) & kPeace) != 0) {
            auto& r = save_.nations[ud].raw[kRelations + ua];
            r = static_cast<std::uint8_t>(r | kWarIntent);
        }
        clearRelationBits(a, d, kPeace);
    }
    auto& r = save_.nations[ua].raw[kRelations + ud];
    r = static_cast<std::uint8_t>(r & ~1u);
    return true;
}

// --- Trade with a foreign colony ---------------------------------------------

int Game::startForeignTrade(int index, int colony) {
    if (index < 0 || index >= unitCount() || colony < 0 || colony >= static_cast<int>(save_.colonies.size())) {
        return 0;
    }
    auto& u = save_.units[static_cast<std::size_t>(index)];
    u.movesUsed = movementAllowance(index);
    const int a = u.nation;
    const int b = save_.colonies[static_cast<std::size_t>(colony)].nation;
    if (a < 0 || a >= kEuropeanNations || save_.players[static_cast<std::size_t>(a)].control != 0 || b < 0 ||
        b >= kEuropeanNations) {
        return 0;
    }
    if ((relation(a, b) & kPeace) == 0) {
        // "Ships and wagon trains cannot enter the colonies of foreign
        // powers with whom you are at war ..."
        notices_.push_back(GameNotice{"TRADEATWAR", {}});
        return 0;
    }
    GameNotice refusal;
    refusal.leader = b;
    if (!hasFather(a, kDeWitt)) {
        // "In the interests of Mercantilism, the King has ordered us not to
        // trade with foreign merchants."
        refusal.section = "TRADEMERCANTILISM";
        refusal.lines = {{0, false, "GREATLEADER2", b}};
        notices_.push_back(std::move(refusal));
        return 0;
    }
    if (u.holds == 0) {
        refusal.section = "TRADENOCARGO";
        notices_.push_back(std::move(refusal));
        return 0;
    }
    return u.holds;
}

void Game::offerForeignTrade(int index, int colony, int hold) {
    foreignTrade_.reset();
    if (index < 0 || index >= unitCount() || colony < 0 || colony >= static_cast<int>(save_.colonies.size())) {
        return;
    }
    const auto& u = unit(index);
    if (hold < 0 || hold >= u.holds) {
        return;
    }
    const auto& c = save_.colonies[static_cast<std::size_t>(colony)];
    const int a = u.nation;
    const int b = c.nation;
    const auto& theirs = save_.nations[static_cast<std::size_t>(b)];
    const int d = save_.difficulty;
    const bool independence = (save_.header[kHeaderIndependence] & 1) != 0;
    const int ally = headerWord(save_.header, kHeaderIntervention);
    ForeignTradeOffer offer;
    offer.unit = index;
    offer.colony = colony;
    offer.hold = hold;
    offer.good = holdGood(u, hold);
    offer.amount = holdAmount(u, hold);
    // What it is worth on their market, less their tax, less a margin.
    // The original keeps only 16 bits of each product before dividing.
    const auto low16 = [](int v) { return static_cast<int>(static_cast<std::int16_t>(v)); };
    const int value = low16(bidPrice(theirs, offer.good) * offer.amount);
    int gold = value - low16(value * theirs.taxRate) / 100;
    if (!independence && (relation(a, b) & kWarIntent) != 0) {
        gold >>= 1;
    }
    if (independence && b == ally) {
        gold += low16((-5 - d) * gold) / 100;
    } else {
        gold += low16(aiRandom(10, 12 * (d + 1)) * gold) / -100;
    }
    offer.gold = std::max(gold, 1);
    // The goods they would give instead: as much as is worth no more than
    // the player's, of the good that comes to most (not food, lumber, tools
    // or muskets before independence; in the war, muskets and horses are
    // easier to have).
    int best = -1;
    for (int g = 0; g < kGoodsKinds; ++g) {
        if (g == offer.good) {
            continue;
        }
        if (!independence && (g == cargo::kMuskets || g == cargo::kTools || g == cargo::kFood || g == cargo::kLumber)) {
            continue;
        }
        int amount = std::min<int>(c.stock[static_cast<std::size_t>(g)], kHoldSize);
        if (independence && (g == cargo::kMuskets || g == cargo::kHorses)) {
            amount = b == ally ? kHoldSize : std::max(amount, kHoldSize / 2);
        }
        const int price = bidPrice(theirs, g);
        int worth = price * amount;
        while (worth > value) {
            --amount;
            worth -= price;
        }
        if (amount > 0 && worth > best) {
            best = worth;
            offer.giveGood = g;
            offer.giveAmount = amount;
        }
    }
    GameNotice notice;
    notice.leader = b;
    if (best < 0) {
        // "We are not interested in your 100 furs."
        notice.section = "TRADENOWANT";
        notice.lines = {{0, true, "CARGO", offer.good}};
        notice.numbers = {offer.amount};
        notices_.push_back(std::move(notice));
        return;
    }
    // "We will give you 100 cloth in exchange for your 100 furs. Or, if you
    // like, we are willing to give you 450."
    notice.section = "TRADEWITH";
    notice.lines = {{0, true, "CARGO", offer.giveGood}, {1, true, "CARGO", offer.good}};
    notice.numbers = {offer.giveAmount, offer.amount, offer.gold};
    notices_.push_back(std::move(notice));
    foreignTrade_ = offer;
}

void Game::answerForeignTrade(int choice) {
    if (!foreignTrade_) {
        return;
    }
    const ForeignTradeOffer offer = *foreignTrade_;
    foreignTrade_.reset();
    if (choice != 0 && choice != 1) {
        return;
    }
    auto& u = save_.units[static_cast<std::size_t>(offer.unit)];
    if (choice == 0) {
        // The hold now carries their goods. The original takes nothing
        // from their colony for it.
        setHold(u, offer.hold, offer.giveGood, offer.giveAmount);
    } else {
        removeHold(u, offer.hold);
        save_.nations[static_cast<std::size_t>(u.nation)].gold += offer.gold;
    }
    auto& stock = save_.colonies[static_cast<std::size_t>(offer.colony)].stock[static_cast<std::size_t>(offer.good)];
    stock = static_cast<std::int16_t>(stock + offer.amount);
}

// --- The King's wars -----------------------------------------------------------

void Game::kingDeclaresWar(int nation, int turn) {
    // Read from the original: on a turn the King does not touch the tax,
    // he may declare war on a nation the player has a treaty with. Never
    // with Franklin, and not before (difficulty + 2) x turn reaches 800.
    if (nation < 0 || nation >= kEuropeanNations || save_.players[static_cast<std::size_t>(nation)].control != 0 ||
        hasFather(nation, kFranklin) || (save_.difficulty + 2) * turn < 800) {
        return;
    }
    const auto& block = save_.tribesAndMore;
    const int gone = headerWord(save_.header, kHeaderGoneNation);
    const auto usa = [&](int e) { return (save_.nations[static_cast<std::size_t>(e)].raw[0] & kUSA) != 0; };
    int treaties = 0;
    for (int e = 0; e < kEuropeanNations; ++e) {
        if (e == nation || e == gone || usa(e)) {
            continue;
        }
        if ((relation(nation, e) & kPeace) != 0) {
            ++treaties;
        }
        if ((relation(nation, e) & (kMet | kPeace)) == kMet) {
            return; // already at war with someone
        }
    }
    if (treaties == 0 || aiRandom(0, (4 - treaties) * 20) > save_.difficulty) {
        return;
    }
    int enemy = 0;
    do {
        enemy = aiRandom(0, kEuropeanNations - 1);
    } while (enemy == nation || (relation(nation, enemy) & kPeace) == 0 || usa(enemy));
    // Help for the war: more against a stronger enemy, less at the harder
    // levels.
    int soldiers = 1;
    int money = (save_.difficulty + 1) * 100;
    const int mine = byteAt(block, kAttackOut + static_cast<std::size_t>(nation));
    const int theirs = byteAt(block, kAttackOut + static_cast<std::size_t>(enemy));
    if (mine < theirs) {
        soldiers = ((theirs - mine) >> 3) + 1;
        money += (theirs - mine) * 25;
    }
    soldiers = std::min(soldiers, 6 - save_.difficulty);
    money = std::min(money, (5 - save_.difficulty) * 500);
    // "The arrogant attitude of the French has forced us to declare war
    // on them ..."
    GameNotice notice{"KINGNEWWAR", {"", save_.players[static_cast<std::size_t>(nation)].name, ""}};
    notice.lines = {{0, true, "DIFFICULTY", save_.difficulty}};
    notice.adjectives = {{2, enemy}};
    notice.numbers = {money, soldiers};
    notices_.push_back(std::move(notice));
    save_.nations[static_cast<std::size_t>(nation)].gold += money;
    for (int i = 0; i < soldiers; ++i) {
        addUnitInEurope(kSoldierType, kVeteranSoldier, nation);
    }
    clearRelationBits(nation, enemy, kPeace);
    addRelationBits(nation, enemy, kInsulted);
    setHeaderWord(save_.header, kHeaderLastTalk + 2 * static_cast<std::size_t>(enemy), turn);
}

// --- The War of the Spanish Succession -------------------------------------------

void Game::spanishSuccession() {
    // Read from the original: once the player's rebels first reach half,
    // the weakest computer nation gives up its colonies, men and maps to
    // the next weakest and leaves the New World. Not in a game of several
    // human players.
    if ((save_.header[kHeaderPlayers] & kSeveralHumans) != 0 || headerWord(save_.header, kHeaderGoneNation) >= 0) {
        return;
    }
    const auto& block = save_.tribesAndMore;
    // 3 x ships + 2 x colonies + people, sorted from the weakest the way
    // the original sorts (an out-of-order one goes before its equals).
    std::array<int, kEuropeanNations> score{};
    std::array<int, kEuropeanNations> order{};
    for (int n = 0; n < kEuropeanNations; ++n) {
        const auto un = static_cast<std::size_t>(n);
        score[un] = 3 * byteAt(block, kShips + un) + 2 * byteAt(block, kColonyCount + un) + byteAt(block, kPeople + un);
        order[un] = n;
    }
    for (;;) {
        std::size_t i = 0;
        while (i + 1 < score.size() && score[i + 1] >= score[i]) {
            ++i;
        }
        if (i + 1 >= score.size()) {
            break;
        }
        const int value = score[i + 1];
        const int who = order[i + 1];
        std::size_t j = 0;
        while (j < i + 1 && value > score[j]) {
            ++j;
        }
        for (std::size_t k = i + 1; k > j; --k) {
            score[k] = score[k - 1];
            order[k] = order[k - 1];
        }
        score[j] = value;
        order[j] = who;
    }
    const auto computer = [&](int n) { return save_.players[static_cast<std::size_t>(n)].control != 0; };
    int loser = -1;
    int winner = -1;
    for (const int n : order) {
        if (!computer(n)) {
            continue;
        }
        if (loser < 0) {
            loser = n;
        } else if (winner < 0) {
            winner = n;
        }
    }
    if (loser < 0 || winner < 0) {
        return;
    }
    // "War of the Spanish Succession ends in Europe! Spain, ravaged by
    // war, agrees to cede New Spain to the Dutch ..."
    GameNotice notice{"SUCCESSION", {"", save_.players[static_cast<std::size_t>(loser)].country, "", ""}};
    notice.sentenceCountries = {{0, loser}};
    notice.adjectives = {{2, winner}, {3, loser}};
    notice.value = kForeignNews;
    notice.music = {{MusicCue::Kind::ChangeMood, kSuccessionMood}};
    notices_.push_back(std::move(notice));

    // What the loser had explored, and the squares it last walked, become
    // the winner's.
    for (auto& square : save_.map.squares) {
        if ((square.seen & (0x10 << loser)) != 0) {
            square.seen = static_cast<std::uint8_t>(square.seen | (0x10 << winner));
        }
        if ((square.region >> 4) == loser) {
            square.region = static_cast<std::uint8_t>((square.region & 0x0F) | (winner << 4));
        }
    }
    // Its men on the map change sides; those in Europe or at sea are lost.
    for (int i = unitCount() - 1; i >= 0; --i) {
        auto& u = save_.units[static_cast<std::size_t>(i)];
        if (u.nation != loser) {
            continue;
        }
        if (!save_.map.inside(u.x, u.y)) {
            removeUnit(i);
            continue;
        }
        u.nation = winner;
        u.raw[3] = static_cast<std::uint8_t>(winner);
        auto& square = save_.map.squares[static_cast<std::size_t>(u.y) * static_cast<std::size_t>(save_.map.width) +
                                         static_cast<std::size_t>(u.x)];
        square.region = static_cast<std::uint8_t>((square.region & 0x0F) | (winner << 4));
    }
    // Its colonies too, and their liberty bells are forgotten.
    for (auto& c : save_.colonies) {
        if (c.nation != loser) {
            continue;
        }
        c.nation = winner;
        c.raw[26] = static_cast<std::uint8_t>(winner);
        c.rebelDividend = 0;
        if (save_.map.inside(c.x, c.y)) {
            auto& square = save_.map.squares[static_cast<std::size_t>(c.y) * static_cast<std::size_t>(save_.map.width) +
                                             static_cast<std::size_t>(c.x)];
            square.region = static_cast<std::uint8_t>((square.region & 0x0F) | (winner << 4));
        }
    }
    // And its missions.
    for (auto& settlement : save_.settlements) {
        if ((settlement.raw[5] & 0x0F) == loser) {
            settlement.raw[5] = static_cast<std::uint8_t>((settlement.raw[5] & 0xF0) | winner);
        }
    }
    refreshUnitFlags();
    nationCensus(loser);
    nationCensus(winner);
    save_.players[static_cast<std::size_t>(loser)].control = 2;
    setHeaderWord(save_.header, kHeaderGoneNation, loser);
}

// --- Scouts at a foreign colony ---------------------------------------------

void Game::scoutMeetMayor(int index, int colony) {
    if (index < 0 || index >= unitCount() || colony < 0 || colony >= static_cast<int>(save_.colonies.size())) {
        return;
    }
    auto& u = save_.units[static_cast<std::size_t>(index)];
    u.movesUsed = movementAllowance(index);
    if ((save_.header[kHeaderIndependence] & 1) != 0) {
        // "Scouts cannot meet with mayors during the War of Independence."
        notices_.push_back(GameNotice{"NOMAYORSDURINGREV", {}});
        return;
    }
    const auto& c = save_.colonies[static_cast<std::size_t>(colony)];
    const int a = u.nation;
    const int b = c.nation;
    if (a < 0 || a >= kEuropeanNations || b < 0 || b >= kEuropeanNations || a == b) {
        return;
    }
    int dir = -1;
    for (std::size_t d = 0; d < kDx.size(); ++d) {
        if (u.x + kDx[d] == c.x && u.y + kDy[d] == c.y) {
            dir = static_cast<int>(d);
        }
    }
    if (!startMeeting(a, b, index, dir, true)) {
        endMeeting(a, b);
    }
}

int Game::infiltrationRisk(int index, int colony) const {
    // (walls + 6) x 2 in 36, half for a seasoned scout, and the difficulty
    // above Conquistador adds to it for the player (less below).
    const auto& c = save_.colonies[static_cast<std::size_t>(colony)];
    int risk = (buildingLevel(c, BuildingKind::Fortification) + 6) * 2;
    if (unit(index).profession == kSeasonedScout) {
        risk >>= 1;
    }
    if (unit(index).nation == human()) {
        risk += save_.difficulty - 2;
    }
    return risk;
}

bool Game::infiltrateColony(int index, int colony) {
    if (index < 0 || index >= unitCount() || colony < 0 || colony >= static_cast<int>(save_.colonies.size())) {
        return false;
    }
    save_.units[static_cast<std::size_t>(index)].movesUsed = movementAllowance(index);
    if (aiRandom(1, 36) > infiltrationRisk(index, colony)) {
        return false; // the player looks round the colony
    }
    auto& c = save_.colonies[static_cast<std::size_t>(colony)];
    // "Our scouts near Quebec have been captured by the French!"
    GameNotice caught{"LOSTOURSCOUTS", {"", c.name}};
    caught.adjectives = {{0, c.nation}};
    caught.numbers = {kStolenHorses};
    notices_.push_back(std::move(caught));
    c.stock[static_cast<std::size_t>(cargo::kHorses)] =
        static_cast<std::int16_t>(c.stock[static_cast<std::size_t>(cargo::kHorses)] + kStolenHorses);
    removeUnit(index);
    return true;
}

bool Game::askWithdrawal(Meeting& m) {
    // "First you must withdraw your forces from our colonies!"
    const int a = m.a;
    const int b = m.b;
    GameNotice notice;
    notice.leader = b;
    if (!m.forcesNear) {
        notice.section = "NOTHINGWITHDRAW";
        notices_.push_back(std::move(notice));
        return false;
    }
    if (m.threat != 0 && !m.grudge) {
        notice.section = "WITHDRAW";
        notices_.push_back(std::move(notice));
        withdrawFrom(a, b, false);
        return false;
    }
    int price = (save_.difficulty + 2) * m.forces * 25;
    if (m.warIntent) {
        price *= 2;
    }
    if (m.wasHostile) {
        price += price / 2;
    }
    if (hasFather(a, kFranklin)) {
        price >>= 1;
    }
    price = std::max(price, 100);
    notice.adjectives = {{0, b}};
    if (price > save_.nations[static_cast<std::size_t>(a)].gold || m.grudge) {
        notice.section = "NOTWITHDRAW";
        notices_.push_back(std::move(notice));
        return false;
    }
    m.price = price;
    notice.section = "MAYBEWITHDRAW";
    notice.numbers = {price};
    m.stage = Meeting::Stage::WithdrawAnswer;
    askMeeting(m, std::move(notice));
    return true;
}

} // namespace viceroy
