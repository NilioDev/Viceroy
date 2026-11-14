// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/game/new_game.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/game/resource.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

namespace viceroy {

namespace {

constexpr int kMaxSide = 256;
constexpr std::uint8_t kArctic = 24;
constexpr std::uint8_t kOcean = 25;
constexpr std::uint8_t kSeaLane = 26;
constexpr std::uint8_t kNoFish = 0x04;

constexpr int kBrave = 19;

struct Site {
    int nation; // 4-11
    int x;
    int y;
};

// Where the original's settlements lie on the map of America: the middle of
// where each one came out over eight new games (see docs/rules/new-game.md).
// The first of each tribe is its capital.
constexpr std::array<Site, 56> kAmericaSites{{
    {4, 36, 52}, {4, 35, 66}, {4, 35, 60}, {4, 36, 56},
    {4, 26, 43}, {5, 17, 27}, {5, 12, 25}, {5, 24, 28},
    {6, 37, 29}, {6, 32, 25}, {6, 31, 28}, {6, 45, 30},
    {6, 46, 35}, {7, 27, 15}, {7, 30, 11}, {7, 23, 15},
    {7, 18, 14}, {7, 25, 10}, {7, 16, 10}, {7, 14, 5},
    {7, 20, 8}, {7, 25, 6}, {7, 30, 5}, {7, 36, 6},
    {8, 26, 21}, {8, 28, 24}, {8, 21, 19}, {8, 20, 22},
    {9, 15, 16}, {9, 3, 17}, {9, 8, 14}, {9, 11, 15},
    {9, 17, 20}, {9, 13, 19}, {9, 16, 23}, {10, 9, 8},
    {10, 12, 2}, {10, 7, 3}, {10, 3, 2}, {10, 4, 7},
    {10, 13, 12}, {10, 4, 13}, {11, 48, 46}, {11, 40, 59},
    {11, 44, 56}, {11, 47, 52}, {11, 51, 48}, {11, 31, 35},
    {11, 34, 38}, {11, 38, 36}, {11, 42, 38}, {11, 47, 40},
    {11, 53, 44}, {11, 43, 52}, {11, 41, 40}, {11, 44, 48},
}};

int u16(std::span<const std::uint8_t> b, std::size_t at) {
    return b[at] | (b[at + 1] << 8);
}

} // namespace

SaveMap readMapFile(std::span<const std::uint8_t> file) {
    if (file.size() < 6) {
        throw FormatError("map file too short");
    }
    SaveMap map;
    map.width = u16(file, 0);
    map.height = u16(file, 2);
    if (map.width < 3 || map.height < 3 || map.width > kMaxSide || map.height > kMaxSide) {
        throw FormatError("map file with impossible size");
    }
    const auto layer = static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height);
    if (file.size() < 6 + 3 * layer) {
        throw FormatError("map file ends early");
    }
    map.squares.resize(layer);
    for (std::size_t i = 0; i < layer; ++i) {
        auto& sq = map.squares[i];
        const auto t = file[6 + i];
        sq.terrain = t & 0x1Fu;
        sq.relief = static_cast<std::uint8_t>(t >> 5);
        sq.flags = file[6 + layer + i];
        sq.region = file[6 + 2 * layer + i];
    }
    return map;
}

void prepareStartingMap(SaveMap& map) {
    for (auto& sq : map.squares) {
        if (sq.terrain >= 16 && sq.terrain < 24) {
            sq.terrain = static_cast<std::uint8_t>(sq.terrain - 8);
        }
        if (sq.isForest() && (sq.hills() || sq.mountains())) {
            sq.terrain &= 7;
        }
        // Nobody has been anywhere yet.
        sq.region = static_cast<std::uint8_t>(0xF0 | (sq.region & 0x0F));
        sq.seen = 0;
    }
    for (int x = 0; x < map.width; ++x) {
        for (int y : {0, map.height - 1}) {
            map.at(x, y).terrain = kArctic;
            map.at(x, y).relief = 0;
        }
    }
    for (int y = 1; y < map.height - 1; ++y) {
        map.at(1, y).terrain = kSeaLane;
        map.at(1, y).relief = 0;
    }
    // The original marks the sea without fish after the arctic rows and the
    // second column but before the outermost columns turn to sea lane: those
    // keep the mark where they were ocean.
    markSeaWithoutFish(map);
    for (int y = 1; y < map.height - 1; ++y) {
        for (int x = 0; x < map.width; ++x) {
            auto& sq = map.at(x, y);
            if (x == 0 || x == map.width - 1) {
                sq.terrain = kSeaLane;
                sq.relief = 0;
            }
        }
    }
}

void markSeaWithoutFish(SaveMap& map) {
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            auto& sq = map.at(x, y);
            if (sq.terrain != kOcean || !onResourcePattern(x, y, map.resourceSeed)) {
                continue;
            }
            bool land = false;
            for (int dy = -2; dy <= 2 && !land; ++dy) {
                for (int dx = -2; dx <= 2 && !land; ++dx) {
                    if (std::abs(dx) + std::abs(dy) <= 3 && map.inside(x + dx, y + dy) &&
                        map.at(x + dx, y + dy).isLand() && map.at(x + dx, y + dy).terrain != kArctic) {
                        land = true;
                    }
                }
            }
            if (!land) {
                sq.flags |= kNoFish;
            }
        }
    }
}

int NewGameRandom::below(int n) {
    state_ = state_ * 1103515245U + 12345U;
    return n <= 0 ? 0 : static_cast<int>((state_ >> 16) % static_cast<std::uint32_t>(n));
}

std::vector<SaveSettlement> placeAmericaSettlements(const SaveMap& map, const std::vector<Tribe>& tribes,
                                                    NewGameRandom& random) {
    constexpr int kJitter = 2;
    constexpr int kTries = 25;
    std::vector<SaveSettlement> placed;
    const auto fits = [&](int x, int y) {
        if (x < 2 || y < 1 || x >= map.width - 1 || y >= map.height - 1) {
            return false;
        }
        const auto& sq = map.at(x, y);
        if (!sq.isLand() || sq.terrain == kArctic || sq.hills() || sq.mountains()) {
            return false;
        }
        for (const auto& s : placed) {
            if (std::abs(s.x - x) <= 1 && std::abs(s.y - y) <= 1) {
                return false;
            }
        }
        return true;
    };
    int previousNation = -1;
    for (const auto& site : kAmericaSites) {
        const bool capital = site.nation != previousNation;
        previousNation = site.nation;
        for (int t = 0; t < kTries; ++t) {
            const int x = site.x + random.below(2 * kJitter + 1) - kJitter;
            const int y = site.y + random.below(2 * kJitter + 1) - kJitter;
            if (!fits(x, y)) {
                continue;
            }
            const auto tribe = static_cast<std::size_t>(site.nation - kEuropeanNations);
            const int level = tribe < tribes.size() ? tribes[tribe].level : 0;
            SaveSettlement s;
            s.x = x;
            s.y = y;
            s.nation = site.nation;
            s.population = 2 * level + 3;
            s.raw.fill(0);
            s.raw[0] = static_cast<std::uint8_t>(x);
            s.raw[1] = static_cast<std::uint8_t>(y);
            s.raw[2] = static_cast<std::uint8_t>(site.nation);
            s.raw[3] = capital ? 4 : 0;
            s.raw[4] = static_cast<std::uint8_t>(s.population);
            s.raw[5] = 0xFF;
            s.raw[6] = capital ? static_cast<std::uint8_t>(s.population) : 0;
            s.raw[7] = s.raw[8] = s.raw[9] = 0xFF;
            placed.push_back(s);
            break;
        }
    }
    return placed;
}

std::vector<SaveUnit> settlementBraves(const std::vector<SaveSettlement>& settlements) {
    std::vector<SaveUnit> braves;
    for (std::size_t i = 0; i < settlements.size(); ++i) {
        const auto& s = settlements[i];
        SaveUnit u;
        u.x = s.x;
        u.y = s.y;
        u.type = kBrave;
        u.nation = s.nation;
        u.profession = 0x1C;
        u.raw.fill(0);
        u.raw[0] = static_cast<std::uint8_t>(s.x);
        u.raw[1] = static_cast<std::uint8_t>(s.y);
        u.raw[2] = kBrave;
        u.raw[3] = static_cast<std::uint8_t>(s.nation);
        u.raw[6] = static_cast<std::uint8_t>(i); // its home settlement
        u.raw[7] = 0x58;
        u.raw[22] = 1;
        u.workTurns = 1; // byte 22 again: what it means for braves is not known
        u.raw[23] = 0x1C;
        u.raw[24] = u.raw[25] = u.raw[26] = u.raw[27] = 0xFF;
        braves.push_back(u);
    }
    return braves;
}

namespace {

constexpr int kCaravel = 13;
constexpr int kMerchantman = 14;
constexpr int kSoldier = 1;
constexpr int kPioneer = 2;
constexpr int kFreeColonist = 0x1C;
constexpr int kVeteran = 0x15;
constexpr int kHardyPioneer = 0x14;
constexpr int kStartYear = 1492;
constexpr int kStartGold = 1000;
constexpr int kStartTools = 100;
constexpr std::size_t kTailSize = 1502;
constexpr std::size_t kTribesAndMoreSize = 8 * 78 + 727;
constexpr std::size_t kSeedAt = 612;

// The squares the ships of the four nations start on (and later sail to
// from Europe), dealt out at random.
constexpr std::array<std::array<int, 2>, 4> kStartSquares{{{51, 56}, {37, 14}, {56, 42}, {47, 28}}};
// Who serves as soldier and pioneer, by nation.
constexpr std::array<int, 4> kSoldierProfession{kFreeColonist, kFreeColonist, kVeteran, kVeteran};
constexpr std::array<int, 4> kPioneerProfession{kFreeColonist, kHardyPioneer, kFreeColonist, kFreeColonist};
// The trade volumes every nation starts with (bytes 92 on of the record).
constexpr std::array<int, kGoodsKinds> kStartVolumes{-13, -16, -16, -25, 0, -7, -8, -2, -11, -12, -3, -11, -13, 4, 5, 6};
// The first person on the docks, by nation; the Dutch player's three were
// always the same.
constexpr std::array<int, 4> kFirstRecruit{25, 25, 24, 13};
constexpr std::array<int, 3> kDutchPlayerRecruits{13, 0, 22};

void put16(std::uint8_t* p, int v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}

SaveUnit europeanUnit(int x, int y, int type, int nation, int profession) {
    SaveUnit u;
    u.x = x;
    u.y = y;
    u.type = type;
    u.nation = nation;
    u.profession = profession;
    u.raw.fill(0);
    u.raw[3] = static_cast<std::uint8_t>(nation | (0x10 << nation)); // its own nation sees it
    u.raw[6] = 0xFF;
    u.raw[9] = static_cast<std::uint8_t>(x);
    u.raw[10] = static_cast<std::uint8_t>(y);
    u.raw[18] = 0xFF;
    return u;
}

} // namespace

SaveGame newAmericaGame(std::span<const std::uint8_t> mapFile, const Ruleset& rules, const NewGameSetup& setup) {
    NewGameRandom random(setup.seed);
    SaveGame save;
    save.map = readMapFile(mapFile);
    save.map.resourceSeed = static_cast<std::uint8_t>(random.below(256));
    prepareStartingMap(save.map);

    save.year = kStartYear;
    save.season = 0;
    save.turn = 0;
    save.difficulty = setup.difficulty;
    save.humanNation = setup.humanNation & 3;
    auto* h = save.header.data();
    std::memcpy(h, "COLONIZE", 9);
    put16(h + 40, save.humanNation);
    // As in the original's new games: no talks with the other nations yet
    // (the turn of the last, 88 + 2 × nation) and no royal side (98).
    for (int n = 0; n < kEuropeanNations; ++n) {
        put16(h + 88 + 2 * n, -1);
    }
    put16(h + 98, -1);

    // The market is drawn once and shared by all.
    std::array<int, kGoodsKinds> prices{};
    for (std::size_t g = 0; g < kGoodsKinds && g < rules.cargo.size(); ++g) {
        const auto& c = rules.cargo[g];
        prices[g] = c.startPriceLow + random.below(std::max(1, c.startPriceHigh - c.startPriceLow + 1));
    }
    std::array<int, 4> squareOf{0, 1, 2, 3};
    for (int i = 3; i > 0; --i) {
        std::swap(squareOf[static_cast<std::size_t>(i)], squareOf[static_cast<std::size_t>(random.below(i + 1))]);
    }

    for (int n = 0; n < kEuropeanNations; ++n) {
        const auto i = static_cast<std::size_t>(n);
        const bool human = n == save.humanNation;
        auto& player = save.players[i];
        player.name = setup.leaders[i];
        player.country = setup.countries[i];
        player.control = human ? 0 : 1;
        player.raw.fill(0);

        auto& nation = save.nations[i];
        nation.raw.fill(0);
        nation.raw[18] = nation.raw[19] = 0xFF;
        if (human) {
            nation.raw[34] = 0x0A;
        }
        nation.gold = human ? kStartGold : 0;
        nation.prices = prices;
        nation.volumes = kStartVolumes;
        nation.crosses = 2;
        nation.crossesNeeded = n == 0 ? 9 : 14;
        nation.recruits = {kFirstRecruit[i], random.below(32), random.below(32)};
        if (human && n == 3) {
            nation.recruits = kDutchPlayerRecruits;
        }
        const auto& start = kStartSquares[static_cast<std::size_t>(squareOf[i])];
        nation.raw[50] = static_cast<std::uint8_t>(start[0]);
        nation.raw[51] = static_cast<std::uint8_t>(start[1]);

        // The ship, then the pioneer and the soldier aboard: they are chained
        // pioneer, soldier, ship, the ship drawn on top.
        const int first = static_cast<int>(save.units.size());
        auto ship = europeanUnit(start[0], start[1], n == 3 ? kMerchantman : kCaravel, n, 0);
        auto pioneer = europeanUnit(start[0], start[1], kPioneer, n, kPioneerProfession[i]);
        auto soldier = europeanUnit(start[0], start[1], kSoldier, n, kSoldierProfession[i]);
        pioneer.orders = soldier.orders = SaveUnit::kSentry;
        pioneer.tools = kStartTools;
        pioneer.nextInTransport = first + 2;
        soldier.prevInTransport = first + 1;
        soldier.nextInTransport = first;
        ship.prevInTransport = first + 2;
        save.units.push_back(ship);
        save.units.push_back(pioneer);
        save.units.push_back(soldier);
        if (human) {
            save.activeUnit = first;
            // The ship's crew has looked around.
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int x = start[0] + dx;
                    const int y = start[1] + dy;
                    if (x >= 1 && y >= 0 && x < save.map.width - 1 && y < save.map.height) {
                        save.map.at(x, y).seen |= static_cast<std::uint8_t>(0x10 << n);
                    }
                }
            }
        }
    }

    save.settlements = placeAmericaSettlements(save.map, rules.tribes, random);
    for (auto& brave : settlementBraves(save.settlements)) {
        save.units.push_back(brave);
    }
    for (const auto& u : save.units) {
        save.map.at(u.x, u.y).flags |= MapSquare::kHasUnit;
    }
    save.tribesAndMore.assign(kTribesAndMoreSize, 0);
    save.tail.assign(kTailSize, 0);
    save.tail[kSeedAt] = save.map.resourceSeed;
    return save;
}

} // namespace viceroy
