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
        // calls for it and returns at once).
        return; // the campaign itself: not done yet
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
