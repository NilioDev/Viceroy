// SPDX-License-Identifier: GPL-3.0-or-later
//
// The war of independence: the declaration, the Continental Army, the
// mercenaries offered during the war and the foreign power that comes to
// help. Read from the original with a debugger and a disassembler (we only
// looked at what it does and wrote it down in our own words); see
// docs/rules/independence.md.
#include "viceroy/game/colony.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>

namespace viceroy {

namespace {

// The counts in the block after the tribe records (see euro_ai.cpp).
constexpr std::size_t kExtra = static_cast<std::size_t>(kTribes) * kTribeRecordSize;
constexpr std::size_t kColonyCount = kExtra + 16;
constexpr std::size_t kPeople = kExtra + 28;
constexpr std::size_t kAttack = kExtra + 44; // words
constexpr std::size_t kAttackOut = kExtra + 60;
constexpr std::size_t kShips = kExtra + 40;
constexpr std::size_t kByType = kExtra + 64; // 19 per nation

// Header.
constexpr std::size_t kHeaderPlayers = 17;
constexpr std::uint8_t kSeveralHumans = 0x80;
constexpr std::size_t kHeaderWar = 18; // bit 0 declared, bit 1 the power has joined, bit 2 it was asked
constexpr std::uint8_t kDeclared = 0x01;
constexpr std::uint8_t kPowerJoined = 0x02;
constexpr std::uint8_t kPowerAsked = 0x04;
constexpr std::size_t kHeaderFirstHuman = 36;    // a word
constexpr std::size_t kHeaderHuman = 40;         // a word
constexpr std::size_t kHeaderCentury = 55;       // the year of the declaration / 100
constexpr std::size_t kHeaderYearInCentury = 56; // and % 100
constexpr std::size_t kHeaderRebels = 96;        // the human's rebels in % (a word)
constexpr std::size_t kHeaderCrown = 98;         // the nation whose slot the Crown's side takes (a word)
constexpr std::size_t kHeaderPower = 100;        // the power that helps the rebels (a word)
constexpr std::size_t kHeaderNextPower = 102;    // the next one; also the king who offers mercenaries
constexpr std::size_t kHeaderRoyalForce = 106;   // regulars, dragoons, men-of-war, artillery
constexpr std::size_t kHeaderPowerForce = 114;   // the same for the power's force
constexpr std::uint8_t kRisen = 0x01;            // colony byte 28: its Tories have risen

// Nation record.
constexpr std::size_t kNationFlags = 0;
constexpr std::uint8_t kMobilized = 0x08;
constexpr std::uint8_t kUSA = 0x04;
constexpr std::size_t kBells = 0x0C;
constexpr std::size_t kCleared = 0x16; // a word the declaration clears

constexpr std::uint8_t kMet = 0x20;
constexpr std::uint8_t kPeace = 0x40;
constexpr std::uint8_t kWarIntent = 0x02;
constexpr std::uint8_t kGrudge = 0x08;

constexpr std::uint8_t kCoastal = 0x40; // colony byte 28: by the water

constexpr int kSoldiers = 1; // rows of @UNIT
constexpr int kDragoons = 4;
constexpr int kContinentalCavalry = 7;
constexpr int kContinentalArmy = 9;
constexpr int kPrivateers = 16;
constexpr int kFrigates = 17;
constexpr int kManOWar = 18;
constexpr int kFirstShip = 13;
constexpr int kLastShip = 18;
constexpr int kArtillery = 11;
constexpr int kMuskets = 15;   // row of @CARGO
constexpr int kVeteran = 0x15; // the veteran soldier's profession

constexpr int kIndependenceMood = 3;
constexpr int kInterventionTune = 0x3F;

constexpr std::array<int, 8> kDx{0, 1, 1, 1, 0, -1, -1, -1};
constexpr std::array<int, 8> kDy{-1, -1, 0, 1, 1, 1, 0, -1};

int byteAt(const std::vector<std::uint8_t>& block, std::size_t at) { return at < block.size() ? block[at] : 0; }

int wordAt(const std::vector<std::uint8_t>& block, std::size_t at) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(byteAt(block, at) | (byteAt(block, at + 1) << 8)));
}

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

int unitsStanding(const SaveGame& save, int x, int y) {
    return static_cast<int>(
        std::count_if(save.units.begin(), save.units.end(), [&](const SaveUnit& u) { return u.x == x && u.y == y; }));
}

// The original halves with a signed division: towards zero.
int half(int value) { return value / 2; }

} // namespace

int Game::crownNation() const { return word(save_.header, kHeaderCrown); }

GameNotice Game::independenceQuestion() const {
    // Read from the original (Game menu, "DECLARE INDEPENDENCE").
    if (independenceDeclared()) {
        return GameNotice{"ALREADYREVOLUTION", {}};
    }
    const int rebels = word(save_.header, kHeaderRebels);
    if (rebels < 50) {
        return GameNotice{"TOOTORY", {}, -1, {rebels}};
    }
    if ((save_.header[kHeaderPlayers] & kSeveralHumans) != 0) {
        return GameNotice{"MULTIREV", {}};
    }
    // "Shall we declare our independence from England, Your Excellency?"
    GameNotice notice{"DECLARE", {""}};
    notice.countries.emplace_back(0, human());
    return notice;
}

void Game::playAlone() {
    // "Declare independence" in a game of several human players: the game
    // becomes one for a single player, the first.
    save_.header[kHeaderPlayers] = static_cast<std::uint8_t>(save_.header[kHeaderPlayers] & ~kSeveralHumans);
    const int first = word(save_.header, kHeaderFirstHuman) & 3;
    setWord(save_.header, kHeaderHuman, first);
    save_.humanNation = first;
}

void Game::declareIndependence() {
    if (independenceDeclared()) {
        return;
    }
    const int rebels = human();
    // The year of the declaration takes the place of the King's wars.
    save_.header[kHeaderYearInCentury] = static_cast<std::uint8_t>(save_.year % 100);
    save_.header[kHeaderCentury] = static_cast<std::uint8_t>(save_.year / 100);
    setWord(save_.nations[static_cast<std::size_t>(rebels)].raw, kBells, 0);
    if (crownNation() < 0) {
        spanishSuccession();
    }
    const int crown = crownNation();
    if (crown < 0 || crown >= kEuropeanNations) {
        return; // the original always has one by now
    }
    musicCues_.push_back({MusicCue::Kind::ChangeMood, kIndependenceMood});

    // The power that will help: the weakest other nation by 3 x ships +
    // 2 x colonies + people, sorted the way the original sorts.
    const auto& block = save_.tribesAndMore;
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
    setWord(save_.header, kHeaderNextPower, -1);
    for (std::size_t i = 0; i < order.size() && word(save_.header, kHeaderNextPower) < 0; ++i) {
        const int n = order[i];
        if (n == rebels || n == crown) {
            continue;
        }
        if (word(save_.header, kHeaderPower) < 0) {
            setWord(save_.header, kHeaderPower, n);
        } else {
            setWord(save_.header, kHeaderNextPower, n);
        }
    }
    setInterventionForce();

    // The other nations leave the New World: their men are gone, their
    // towns stay and cannot be attacked.
    for (int n = 0; n < kEuropeanNations; ++n) {
        if (n == rebels || n == crown) {
            continue;
        }
        for (const int other : {rebels, crown}) {
            clearRelationBits(n, other, static_cast<std::uint8_t>(kGrudge | kWarIntent | 0x01));
            addRelationBits(n, other, static_cast<std::uint8_t>(kMet | kPeace));
        }
        for (int i = unitCount() - 1; i >= 0; --i) {
            if (save_.units[static_cast<std::size_t>(i)].nation == n) {
                removeUnit(i);
            }
        }
        save_.players[static_cast<std::size_t>(n)].control = 2;
    }
    // The rebels' men in Europe and at sea between are lost; the ships are
    // named.
    for (int i = unitCount() - 1; i >= 0; --i) {
        const auto& u = save_.units[static_cast<std::size_t>(i)];
        if (u.nation != rebels || save_.map.inside(u.x, u.y)) {
            continue;
        }
        if (u.type >= kFirstShip && u.type <= kLastShip) {
            notices_.push_back(GameNotice{"SEIZURE", {rules_.units[static_cast<std::size_t>(u.type)].name}});
        }
        removeUnit(i);
    }
    save_.header[kHeaderWar] = static_cast<std::uint8_t>(save_.header[kHeaderWar] | kDeclared);

    // The Crown's side takes the slot of the nation that left after the
    // succession, and knows the map as far as the rebels do. (The original
    // masks with the nation's number, not its bit.)
    const auto crownBit = static_cast<std::uint8_t>(0x10 << crown);
    const auto rebelBit = static_cast<std::uint8_t>(0x10 << rebels);
    for (auto& square : save_.map.squares) {
        square.seen = static_cast<std::uint8_t>(square.seen & static_cast<std::uint8_t>(~crown));
        if ((square.seen & rebelBit) != 0) {
            square.seen = static_cast<std::uint8_t>(square.seen | crownBit);
        }
    }
    save_.players[static_cast<std::size_t>(crown)].control = 1;
    addRelationBits(rebels, crown, static_cast<std::uint8_t>(kMet | kWarIntent));
    clearRelationBits(rebels, crown, kPeace);

    // "Continental Congress signs Declaration of Independence! ...
    // General Peter Minuit calls for volunteers for new Continental Army!"
    notices_.push_back(GameNotice{"INDEPENDENCE", {save_.players[static_cast<std::size_t>(rebels)].name}});
    for (int i = unitCount() - 1; i >= 0; --i) {
        if (save_.units[static_cast<std::size_t>(i)].nation == rebels) {
            save_.units[static_cast<std::size_t>(i)].movesUsed = movementAllowance(i);
        }
    }
    setWord(save_.nations[static_cast<std::size_t>(rebels)].raw, kCleared, 0);
    auto& crownRecord = save_.nations[static_cast<std::size_t>(crown)].raw;
    setWord(crownRecord, kCleared, 0);
    crownRecord[kNationFlags] = static_cast<std::uint8_t>(crownRecord[kNationFlags] & ~kUSA);
    refreshUnitFlags();
}

void Game::setInterventionForce() {
    // The power's force, from its own strength and the difficulty, at most
    // two land units of each kind and six in all per man-of-war.
    const int power = word(save_.header, kHeaderPower);
    if (power < 0 || power >= kEuropeanNations) {
        return;
    }
    const auto p = static_cast<std::size_t>(power);
    const auto& block = save_.tribesAndMore;
    const int ships = byteAt(block, kByType + p * 19 + kPrivateers) + byteAt(block, kByType + p * 19 + kFrigates);
    const int d = save_.difficulty;
    const int easier = (4 - d) >> 1;
    int regulars = half(byteAt(block, kPeople + p) / 10 - d + 8 + 1);
    int dragoons = half(((byteAt(block, kAttackOut + p) + 1) >> 4) + easier + 1 + 1);
    int artillery =
        half(easier + static_cast<int>(static_cast<std::uint16_t>(wordAt(block, kAttack + 2 * p) + 1) >> 5) + 3 + 1);
    const int menOfWar = half(easier + ships + 3 + 1);
    artillery = std::min(artillery, 2 * menOfWar);
    dragoons = std::min(dragoons, 2 * menOfWar);
    regulars = std::min(regulars, 6 * menOfWar - artillery - dragoons);
    setWord(save_.header, kHeaderPowerForce + 0, regulars);
    setWord(save_.header, kHeaderPowerForce + 2, dragoons);
    setWord(save_.header, kHeaderPowerForce + 4, menOfWar);
    setWord(save_.header, kHeaderPowerForce + 6, artillery);
}

void Game::independenceTurn(int nation) {
    // Each turn of the war, after the colonies: for the rebels the
    // Continental Army first, then help from abroad; for the Crown its
    // campaign.
    if (nation == crownNation()) {
        // The Crown's army grows no more during the war (the original
        // calls for it and returns at once). While it has land units to
        // send it lands them; then the Tories rise.
        const auto force = [&](int kind) {
            return word(save_.header, kHeaderRoyalForce + 2 * static_cast<std::size_t>(kind));
        };
        if (force(0) + (force(1) > 0 ? 1 : 0) + (force(3) > 0 ? 1 : 0) != 0) {
            crownLands();
        } else {
            toryUprising();
        }
        return;
    }
    auto& flags = save_.nations[static_cast<std::size_t>(nation)].raw[kNationFlags];
    if ((flags & kMobilized) == 0) {
        mobilize(nation);
        flags = static_cast<std::uint8_t>(flags | kMobilized);
        return;
    }
    if ((save_.header[kHeaderWar] & kPowerJoined) != 0 && word(save_.header, kHeaderPowerForce + 4) != 0) {
        forceArrives(false);
        return;
    }
    if (aiRandom(0, 2) != 0) {
        return;
    }
    // Mercenaries: a few regulars and a cavalry or an artillery.
    MercenaryOffer offer;
    offer.country = word(save_.header, kHeaderNextPower);
    offer.counts[0] = aiRandom(2, ((4 - save_.difficulty) >> 1) + 2);
    if (aiRandom(0, 1) != 0) {
        offer.counts[1] = 1;
    } else {
        offer.counts[3] = 1;
    }
    const int each = ((save_.difficulty + 3) * 2 + aiRandom(0, 6)) * 100;
    offer.price = each * (2 * (offer.counts[1] + offer.counts[3]) + offer.counts[0]);
    if (save_.nations[static_cast<std::size_t>(nation)].gold < offer.price) {
        return;
    }
    std::string force = std::to_string(offer.counts[0]) + " " + rules_.units[6].name;
    if (offer.counts[1] > 0) {
        force += ", " + rules_.units[8].name;
    }
    if (offer.counts[3] > 0) {
        force += ", " + rules_.units[11].name;
    }
    GameNotice notice{"MERCENARIES", {"", force}, kMercenaryQuestion, {offer.price}};
    notice.countries.emplace_back(0, offer.country);
    notices_.push_back(std::move(notice));
    mercenaries_ = offer;
}

void Game::mobilize(int nation) {
    // In every colony where the Sons of Liberty are at least half, some of
    // the veteran soldiers and dragoons standing there join the
    // Continental Army.
    musicCues_.push_back({MusicCue::Kind::ChangeMood, kIndependenceMood});
    const int extra = nationEffects(save_, nation).libertyExtra(true);
    for (int c = 0; c < static_cast<int>(save_.colonies.size()); ++c) {
        const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
        if (colony.nation != nation) {
            continue;
        }
        const int sons = sonsOfLibertyPercent(colony, extra);
        if (sons < 50) {
            continue;
        }
        const int people = colony.population;
        int left = std::max(1, std::min((sons - 50) * people / 50, people >> 1));
        int promoted = 0;
        std::string kind;
        for (int i = 0; i < unitCount() && left > 0; ++i) {
            auto& u = save_.units[static_cast<std::size_t>(i)];
            if (u.x != colony.x || u.y != colony.y || (u.type != kSoldiers && u.type != kDragoons) ||
                u.profession != kVeteran) {
                continue;
            }
            --left;
            kind = rules_.units[static_cast<std::size_t>(u.type)].name;
            u.type = u.type == kSoldiers ? kContinentalArmy : kContinentalCavalry;
            ++promoted;
        }
        if (promoted == 0) {
            continue;
        }
        // "Continental Army mobilizes at Boston! Our Veteran Soldiers have
        // been promoted to Continental Army status."
        GameNotice notice = promoted == 1 ? GameNotice{"MOBILIZE", {colony.name, kind}}
                                          : GameNotice{"MOBILIZE2", {colony.name}, -1, {promoted}};
        notice.colony = c;
        notices_.push_back(std::move(notice));
    }
}

void Game::forceArrives(bool mercenaries) {
    // A man-of-war brings the power's force (or the mercenaries) to one of
    // the rebels' first ten coastal colonies, picked by their people, onto
    // the best open water next to it.
    const int nation = human();
    std::vector<int> colonies;
    int people = 0;
    for (int c = 0; c < static_cast<int>(save_.colonies.size()) && colonies.size() < 10; ++c) {
        const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
        if (colony.nation == nation && (colony.raw[28] & kCoastal) != 0) {
            colonies.push_back(c);
            people += static_cast<std::int8_t>(colony.population);
        }
    }
    if (colonies.empty()) {
        return;
    }
    int roll = aiRandom(1, people);
    int picked = -1;
    for (const int c : colonies) {
        roll -= static_cast<std::int8_t>(save_.colonies[static_cast<std::size_t>(c)].population);
        if (roll <= 0) {
            picked = c;
            break;
        }
    }
    if (picked < 0) {
        return;
    }
    const auto& colony = save_.colonies[static_cast<std::size_t>(picked)];
    const auto& map = save_.map;
    const auto squareAt = [&](int x, int y) -> const MapSquare& {
        return map
            .squares[static_cast<std::size_t>(y) * static_cast<std::size_t>(map.width) + static_cast<std::size_t>(x)];
    };
    const auto landRegion = [&](int x, int y) {
        return map.inside(x, y) && squareAt(x, y).isLand() ? squareAt(x, y).region & 0x0F : -1;
    };
    const int region = landRegion(colony.x, colony.y);
    int best = 0;
    int shipX = -1;
    int shipY = -1;
    for (std::size_t d = 0; d < kDx.size(); ++d) {
        const int x = colony.x + kDx[d];
        const int y = colony.y + kDy[d];
        // Open sea (the ocean's region), with no royal man-of-war on it
        // and no one else's ships.
        if (!map.inside(x, y) || !squareAt(x, y).isWater() || (squareAt(x, y).region & 0x0F) != 1) {
            continue;
        }
        int score = 1;
        int owner = -1;
        for (const auto& u : save_.units) {
            if (u.x == x && u.y == y) {
                owner = u.nation;
                if (u.nation == crownNation() && u.type == kManOWar) {
                    score -= 999;
                }
            }
        }
        if (score < 0 || (owner >= 0 && owner != nation)) {
            continue;
        }
        // The more free land of the colony's own next to it, the better.
        for (std::size_t e = 0; e < kDx.size(); ++e) {
            const int ax = x + kDx[e];
            const int ay = y + kDy[e];
            if (map.inside(ax, ay) && !squareAt(ax, ay).isWater() && landRegion(ax, ay) == region &&
                !colonyAt(ax, ay)) {
                ++score;
            }
        }
        if (score > best) {
            best = score;
            shipX = x;
            shipY = y;
        }
    }
    if (best <= 0) {
        return; // nowhere to land: the gold is gone all the same
    }
    if (!mercenaries) {
        setWord(save_.header, kHeaderPowerForce + 4, word(save_.header, kHeaderPowerForce + 4) - 1);
    }
    addBuiltUnit(shipX, shipY, nation, kManOWar);
    const int ship = unitCount() - 1;
    GameNotice notice{mercenaries ? "MERCS" : "INTERVENE", {colony.name, ""}};
    notice.adjectives.emplace_back(1, word(save_.header, mercenaries ? kHeaderNextPower : kHeaderPower));
    notice.colony = picked;
    if (!mercenaries) {
        notice.music = {{MusicCue::Kind::MoodEvent, kIndependenceMood}, {MusicCue::Kind::Sound, kInterventionTune}};
    }
    notices_.push_back(std::move(notice));
    // Up to six land units a ship: two dragoons, two guns, the rest
    // regulars; the mercenaries all come.
    const auto force = [&](int kind) {
        return word(save_.header, kHeaderPowerForce + 2 * static_cast<std::size_t>(kind));
    };
    std::array<int, 4> room{6, 0, 0, 0};
    if (force(1) != 0) {
        room[1] = std::min(force(1), 2);
    }
    if (force(3) != 0) {
        room[3] = std::min(force(3), 2);
    }
    room[0] -= room[1] + room[3];
    const int x = colony.x;
    const int y = colony.y;
    for (const int kind : {0, 1, 3}) {
        const int type = royalUnitType(kind, nation);
        const int count = mercenaries ? mercenaries_.counts[static_cast<std::size_t>(kind)]
                                      : std::min(room[static_cast<std::size_t>(kind)], force(kind));
        for (int i = 0; i < count; ++i) {
            addBuiltUnit(x, y, nation, type);
            save_.units.back().profession = kVeteran;
            if (!mercenaries) {
                setWord(save_.header, kHeaderPowerForce + 2 * static_cast<std::size_t>(kind), force(kind) - 1);
            }
        }
    }
    if (mercenaries) {
        removeUnit(ship); // the mercenaries' ship sails home
    }
    refreshUnitFlags();
}

int Game::colonyStrength(int colony) const {
    // How many men the Crown thinks it needs against a colony: one per
    // hundred muskets in store (rounded), one more, half the defence of the
    // men standing there, all of it doubled behind a fortress or half as
    // much again behind a fort.
    const auto& c = save_.colonies[static_cast<std::size_t>(colony)];
    int need = (c.stock[kMuskets] + 50) / 100 + 1;
    for (int i = 0; i < unitCount(); ++i) {
        const auto& u = unit(i);
        if (u.x != c.x || u.y != c.y || (u.type >= kFirstShip && u.type <= kLastShip) || u.type < 0 ||
            u.type >= static_cast<int>(rules_.units.size())) {
            continue;
        }
        int v = rules_.units[static_cast<std::size_t>(u.type)].combat;
        if (u.type == kArtillery && (u.raw[4] & 0x80) != 0) {
            v -= 2; // damaged
        }
        v = std::max(0, v) * 8;
        if ((u.type == kSoldiers || u.type == kDragoons) && u.profession == kVeteran) {
            v += v >> 1;
        }
        need += v >> 4;
    }
    const int level = buildingLevel(c, BuildingKind::Fortification);
    if (level >= 2) {
        need *= 2;
    } else if (level == 1) {
        need = need * 3 / 2;
    }
    return std::max(1, need);
}

void Game::seizeAt(int x, int y) {
    // Everyone not of the Crown on the square is lost; the rebels hear of
    // their men on land and of their ships.
    const int crown = crownNation();
    const bool water = save_.map.inside(x, y) && save_.map.at(x, y).isWater();
    for (int i = unitCount() - 1; i >= 0; --i) {
        const auto& u = unit(i);
        if (u.x != x || u.y != y || u.nation == crown) {
            continue;
        }
        const bool told = u.nation < kEuropeanNations && save_.players[static_cast<std::size_t>(u.nation)].control == 0;
        const bool ship = u.type >= kFirstShip && u.type <= kLastShip;
        if (told && (!water || ship) && u.type >= 0 && u.type < static_cast<int>(rules_.units.size())) {
            notices_.push_back(GameNotice{water ? "SEIZURESEA" : "SEIZURELAND",
                                          {rules_.units[static_cast<std::size_t>(u.type)].name}});
        }
        removeUnit(i);
    }
}

void Game::placeCrownUnit(int x, int y, int type) {
    const int crown = crownNation();
    addBuiltUnit(x, y, crown, type);
    auto& u = save_.units.back();
    u.movesUsed = movementAllowance(unitCount() - 1);
    u.raw[3] = static_cast<std::uint8_t>(u.raw[3] | (0x10 << human())); // the rebels see it
    auto& square = save_.map.squares[static_cast<std::size_t>(y) * static_cast<std::size_t>(save_.map.width) +
                                     static_cast<std::size_t>(x)];
    square.region = static_cast<std::uint8_t>((square.region & 0x0F) | (crown << 4));
}

void Game::crownLands() {
    // The Crown's turn of the war while it has land units at home: a
    // man-of-war lands them next to one of the rebels' coastal colonies.
    const int rebels = human();
    const int crown = crownNation();
    const auto forceAt = [&](int kind) { return kHeaderRoyalForce + 2 * static_cast<std::size_t>(kind); };
    const auto force = [&](int kind) { return word(save_.header, forceAt(kind)); };
    const auto take = [&](int kind) { setWord(save_.header, forceAt(kind), force(kind) - 1); };
    bool givesUp = false;
    const auto end = [&]() {
        if (givesUp) {
            for (int kind = 0; kind < 4; ++kind) {
                setWord(save_.header, forceAt(kind), 0);
            }
        }
    };
    if (force(2) == 0) {
        // No man-of-war left at home: one more if none is out either.
        if (byteAt(save_.tribesAndMore, kByType + static_cast<std::size_t>(crown) * 19 + kManOWar) == 0) {
            setWord(save_.header, forceAt(2), 1);
        }
        return;
    }
    const int total = force(0) + force(1) + force(3) + force(2);
    if (total <= 4 || total == force(2)) {
        givesUp = true; // the last of it, should nothing land this turn
    }
    if (total == force(2)) {
        end();
        return;
    }
    // The candidates: the first ten coastal colonies, sorted by Tories
    // times people less their armed men.
    const int extra = nationEffects(save_, rebels).libertyExtra(true);
    std::vector<int> keys;
    std::vector<int> picks;
    for (int c = 0; c < static_cast<int>(save_.colonies.size()) && keys.size() < 10; ++c) {
        const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
        if (colony.nation != rebels || (colony.raw[28] & kCoastal) == 0) {
            continue;
        }
        const int tories = 100 - sonsOfLibertyPercent(colony, extra);
        int armed = 0;
        for (const auto& u : save_.units) {
            if (u.x == colony.x && u.y == colony.y && !(u.type >= kFirstShip && u.type <= kLastShip) && u.type >= 0 &&
                u.type < static_cast<int>(rules_.units.size()) &&
                rules_.units[static_cast<std::size_t>(u.type)].combat > 1) {
                ++armed;
            }
        }
        keys.push_back(std::max(colony.population * (tories + 25) - 75 * armed, tories));
        picks.push_back(c);
    }
    sortLikeTheOriginal(keys, picks);
    std::vector<int> strength;
    for (const int c : picks) {
        strength.push_back(colonyStrength(c));
    }
    // From the highest key: the first the force at home can take, then the
    // same with at most a shipload, then the first.
    const int count = static_cast<int>(picks.size());
    const int shipload = rules_.units.size() > static_cast<std::size_t>(kManOWar)
                             ? rules_.units[static_cast<std::size_t>(kManOWar)].cargoSlots
                             : 6;
    int chosen = -1;
    int need = 0;
    int wanted = 0;
    for (int round = 0; round < 3 && chosen < 0; ++round) {
        for (int i = 0; i < count; ++i) {
            const int k = count - 1 - i;
            wanted = strength[static_cast<std::size_t>(k)];
            need = std::max(1, wanted);
            int each = std::max(1, need >> 3);
            if (force(1) + force(3) <= force(0)) {
                each = 1;
            }
            const int dragoons = std::min(force(1), each);
            const int guns = std::min(force(3), each);
            if (round != 0) {
                need = std::min(need, shipload);
            }
            if (round >= 2 || force(0) + dragoons + guns >= need) {
                chosen = k;
                break;
            }
        }
    }
    if (chosen < 0) {
        end();
        return;
    }
    need = std::min(need, shipload);
    const int colonyIndex = picks[static_cast<std::size_t>(chosen)];
    const auto& colony = save_.colonies[static_cast<std::size_t>(colonyIndex)];
    const int colonyX = colony.x;
    const int colonyY = colony.y;
    const auto& map = save_.map;
    const auto landRegion = [&](int x, int y) {
        return map.inside(x, y) && map.at(x, y).isLand() ? map.at(x, y).region & 0x0F : -1;
    };
    const auto water = [&](int x, int y) { return !map.inside(x, y) || map.at(x, y).isWater(); };
    const int region = landRegion(colonyX, colonyY);
    // The open sea next to it with the most free land of the colony's own
    // around; a square with a rebel man-of-war counts as one.
    int best = 0;
    int sx = -1;
    int sy = -1;
    for (std::size_t d = 0; d < kDx.size(); ++d) {
        const int x = colonyX + kDx[d];
        const int y = colonyY + kDy[d];
        if (!map.inside(x, y) || !map.at(x, y).isWater() || (map.at(x, y).region & 0x0F) != 1) {
            continue;
        }
        int score = 0;
        for (std::size_t e = 0; e < kDx.size(); ++e) {
            const int ax = x + kDx[e];
            const int ay = y + kDy[e];
            if (!water(ax, ay) && landRegion(ax, ay) == region && !colonyAt(ax, ay)) {
                ++score;
            }
        }
        if (score == 0) {
            continue;
        }
        bool ours = false;
        bool manOWar = false;
        bool anyone = false;
        for (const auto& u : save_.units) {
            if (u.x == x && u.y == y) {
                anyone = true;
                ours = ours || u.nation == crown;
                manOWar = manOWar || u.type == kManOWar;
            }
        }
        if (anyone && !ours && manOWar) {
            score = 1;
        }
        if (score > best) {
            best = score;
            sx = x;
            sy = y;
        }
    }
    if (best <= 0) {
        end();
        return;
    }
    const std::string colonyName = colony.name;
    seizeAt(sx, sy);
    take(2);
    placeCrownUnit(sx, sy, kManOWar);
    // "Royal Expeditionary Force lands near Boston!"
    GameNotice invasion{"INVASION", {colonyName}};
    invasion.colony = colonyIndex;
    invasion.music = {{MusicCue::Kind::ChangeMood, kIndependenceMood}};
    notices_.push_back(std::move(invasion));
    givesUp = false;
    int each = std::max(1, wanted >> 3);
    if (force(0) >= 2) {
        each = std::min(each, 2);
    }
    if (force(1) + force(3) <= force(0)) {
        each = 1;
    }
    need = std::max(need, 3);
    // Onto the land next to both the ship and the colony: first where the
    // fewest stand (whose men are taken), then next to the men already
    // landed, a few dragoons and guns among the regulars.
    int fewest = 99;
    bool found = false;
    bool second = false;
    int dragoons = 0;
    int guns = 0;
    while (need > 0) {
        bool landed = false;
        for (std::size_t d = 0; d < kDx.size(); ++d) {
            const int lx = sx + kDx[d];
            const int ly = sy + kDy[d];
            if (water(lx, ly) || colonyAt(lx, ly) || std::abs(lx - colonyX) > 1 || std::abs(ly - colonyY) > 1 ||
                landRegion(lx, ly) != region || need <= 0) {
                continue;
            }
            int kind = 0;
            bool attack = false;
            const int last = map.at(lx, ly).region >> 4;
            if (last != crown) {
                const int here = unitsStanding(save_, lx, ly);
                if (!second) {
                    if (here < fewest) {
                        found = true;
                        fewest = here;
                    }
                    continue;
                }
                if (here > fewest) {
                    continue;
                }
                attack = true;
            } else if (each > dragoons && force(1) > 0) {
                ++dragoons;
                kind = 1;
            } else if (each > guns && force(3) > 0) {
                ++guns;
                kind = 3;
            }
            if (force(kind) <= 0) {
                continue;
            }
            if (attack) {
                seizeAt(lx, ly);
            }
            placeCrownUnit(lx, ly, royalUnitType(kind, crown));
            take(kind);
            --need;
            landed = true;
        }
        if (landed) {
            second = false;
            found = false;
            fewest = 99;
        } else if (found && !second) {
            second = true;
        } else {
            need = 0;
        }
    }
    refreshUnitFlags();
    end();
}

void Game::toryUprising() {
    // The Crown has no more land units to send: now and then the Tories of
    // a colony rise, once in each colony, as soldiers and dragoons of the
    // Crown on the free land around it.
    const int rebels = human();
    const int crown = crownNation();
    const int d = save_.difficulty;
    if (aiRandom(0, d + 1) == 0) {
        return;
    }
    const int extra = nationEffects(save_, rebels).libertyExtra(true);
    int best = 0;
    int picked = -1;
    for (int c = 0; c < static_cast<int>(save_.colonies.size()); ++c) {
        const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
        if (colony.nation != rebels || (colony.raw[28] & kRisen) != 0) {
            continue;
        }
        int score = (100 - sonsOfLibertyPercent(colony, extra)) * 2 * colony.population / 100 + d + 1;
        for (const auto& u : save_.units) {
            if (u.x == colony.x && u.y == colony.y && u.type >= 0 && u.type < static_cast<int>(rules_.units.size())) {
                score -= rules_.units[static_cast<std::size_t>(u.type)].combat;
            }
        }
        int open = 0;
        for (std::size_t k = 0; k < kDx.size(); ++k) {
            const int x = colony.x + kDx[k];
            const int y = colony.y + kDy[k];
            if (!save_.map.inside(x, y) || save_.map.at(x, y).isWater() || colonyAt(x, y)) {
                continue;
            }
            bool crownHere = false;
            for (const auto& u : save_.units) {
                crownHere = crownHere || (u.x == x && u.y == y && u.nation == crown);
            }
            if (crownHere) {
                score = -1;
            } else {
                ++open;
            }
        }
        if (open == 0) {
            score = -1;
        }
        if (score > best) {
            best = score;
            picked = c;
        }
    }
    if (picked < 0) {
        return;
    }
    int tories = best;
    auto& colony = save_.colonies[static_cast<std::size_t>(picked)];
    colony.raw[28] = static_cast<std::uint8_t>(colony.raw[28] | kRisen);
    const int cx = colony.x;
    const int cy = colony.y;
    bool any = false;
    while (tories > 0) {
        bool placed = false;
        for (std::size_t k = 0; k < kDx.size() && tories > 0; ++k) {
            const int x = cx + kDx[k];
            const int y = cy + kDy[k];
            if (!save_.map.inside(x, y) || save_.map.at(x, y).isWater() || colonyAt(x, y)) {
                continue;
            }
            const int last = save_.map.at(x, y).region >> 4;
            if (unitsStanding(save_, x, y) > 0 && last != crown) {
                continue;
            }
            placeCrownUnit(x, y, kSoldiers);
            any = true;
            placed = true;
            auto& u = save_.units.back();
            if ((tories & 1) != 0 && aiRandom(0, d + 1) != 0) {
                u.profession = kVeteran;
            }
            if (tories % 3 == 0 && aiRandom(0, d + 1) != 0) {
                u.type = kDragoons;
            }
            --tories;
        }
        if (!placed) {
            tories = 0;
        }
    }
    auto& risen = save_.colonies[static_cast<std::size_t>(picked)];
    if (!any) {
        risen.raw[28] = static_cast<std::uint8_t>(risen.raw[28] & ~kRisen);
        return;
    }
    refreshUnitFlags();
    // "Tory uprising near Boston! Parliament arms Tory Militia!"
    GameNotice notice{"TORYUPRISING", {risen.name}};
    notice.colony = picked;
    notices_.push_back(std::move(notice));
}

void Game::sortLikeTheOriginal(std::vector<int>& keys, std::vector<int>& values) {
    // The original's sort: from the smallest; an out-of-order key goes
    // before its equals.
    for (;;) {
        std::size_t i = 0;
        while (i + 1 < keys.size() && keys[i + 1] >= keys[i]) {
            ++i;
        }
        if (i + 1 >= keys.size()) {
            break;
        }
        const int key = keys[i + 1];
        const int value = values[i + 1];
        std::size_t j = 0;
        while (j < i + 1 && key > keys[j]) {
            ++j;
        }
        for (std::size_t k = i + 1; k > j; --k) {
            keys[k] = keys[k - 1];
            values[k] = values[k - 1];
        }
        keys[j] = key;
        values[j] = value;
    }
}

void Game::interventionDeclared() {
    // Enough bells after the declaration: the power declares war on the
    // Crown. Its envoy goes to the rebels' biggest colony by the sea.
    const int nation = human();
    int biggest = 0;
    int most = -1;
    for (int c = 0; c < static_cast<int>(save_.colonies.size()); ++c) {
        const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
        if (colony.nation == nation && (colony.raw[28] & kCoastal) != 0 &&
            static_cast<std::int8_t>(colony.population) > most) {
            most = static_cast<std::int8_t>(colony.population);
            biggest = c;
        }
    }
    const int power = word(save_.header, kHeaderPower);
    // "France declares war on England and joins the War of Independence on
    // the Rebel side! French General Lafayette dispatched to Boston for
    // strategic consultations. French navy plans bombardment of Tory-held
    // fortifications."
    GameNotice notice{"INTERVENTION", {"", "", "", "", ""}};
    notice.sentenceCountries.emplace_back(0, power);
    notice.countries.emplace_back(1, crownNation());
    notice.lines.push_back({2, false, "FRIEND", power});
    if (biggest < static_cast<int>(save_.colonies.size())) {
        notice.strings[3] = save_.colonies[static_cast<std::size_t>(biggest)].name;
    }
    notice.adjectives.emplace_back(4, power);
    notice.music = {{MusicCue::Kind::ChangeMood, kIndependenceMood}};
    notices_.push_back(std::move(notice));
    save_.header[kHeaderWar] = static_cast<std::uint8_t>(save_.header[kHeaderWar] | kPowerJoined);
}

void Game::independenceBells(int nation, int bells) {
    // The bells after the declaration bring the power in: first it lets
    // the rebels know how many it waits for.
    if (nation != human()) {
        return;
    }
    auto& raw = save_.nations[static_cast<std::size_t>(nation)].raw;
    const int have = word(raw, kBells);
    if ((save_.header[kHeaderWar] & (kPowerJoined | kPowerAsked)) == 0 && have > bells) {
        notices_.push_back(GameNotice{"AMBUSHHINT", {}});
        GameNotice consider{"CONSIDER", {""}, -1, {bellsForNextFather(nation)}};
        consider.sentenceCountries.emplace_back(0, word(save_.header, kHeaderPower));
        notices_.push_back(std::move(consider));
        save_.header[kHeaderWar] = static_cast<std::uint8_t>(save_.header[kHeaderWar] | kPowerAsked);
    }
    if (bellsForNextFather(nation) > static_cast<std::uint16_t>(have)) {
        return;
    }
    if ((save_.header[kHeaderWar] & kPowerJoined) != 0) {
        return;
    }
    interventionDeclared();
    setWord(raw, kBells, 0);
}

} // namespace viceroy
