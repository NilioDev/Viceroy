// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Tutorial Hints: the advisers' short lessons, each given once a game
// while the option is on, and the naming of the new land. What triggers
// each hint was read from the original; see docs/rules/tutorial.md.
#include "viceroy/game/colony.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"
#include "viceroy/game/resource.hpp"

#include <cstdlib>

namespace viceroy {

namespace {

// The advisers by the number of their picture (MSSn.SS).
constexpr int kNaval = 0;
constexpr int kColonial = 1;
constexpr int kFrontier = 3;
constexpr int kReligious = 4;
constexpr int kWoman = 5;
constexpr int kNobody = kTutorialNoAdviser;

// The original's kind of terrain: the terrain number, with mountains 27
// and hills 28.
int kindOf(const MapSquare& sq) { return sq.mountains() ? 27 : sq.hills() ? 28 : sq.terrain; }

bool isForestKind(int kind) { return kind >= 8 && kind < 24; }

// Plains, prairie, grassland and savannah.
bool isFarmland(int kind) { return kind >= 2 && kind <= 5; }

// The distance the original measures: the longer side plus half the shorter.
int distance(int dx, int dy) {
    dx = std::abs(dx);
    dy = std::abs(dy);
    return dx > dy ? dx + dy / 2 : dy + dx / 2;
}

} // namespace

bool Game::tutorialHints() const { return (save_.header[18] & 0x80) != 0; }

bool Game::tutorialShown(TutorialFlag flag) const {
    return (save_.header[static_cast<std::size_t>(flag.byte)] & flag.mask) != 0;
}

void Game::markTutorialShown(TutorialFlag flag) {
    auto& b = save_.header[static_cast<std::size_t>(flag.byte)];
    b = static_cast<std::uint8_t>(b | flag.mask);
}

bool Game::landSighted() const {
    const int h = human();
    return h >= 0 && h < kEuropeanNations && (save_.players[static_cast<std::size_t>(h)].raw[48] & 0x80) != 0;
}

void Game::setCountryName(const std::string& name) {
    const int h = human();
    if (h >= 0 && h < kEuropeanNations && !name.empty()) {
        save_.players[static_cast<std::size_t>(h)].country = name.substr(0, 23);
    }
}

std::optional<GameNotice> Game::unitHint(int index) {
    if (!tutorialHints() || index < 0 || index >= unitCount()) {
        return std::nullopt;
    }
    const auto& u = unit(index);
    const auto& map = save_.map;
    if (u.orders != SaveUnit::kNoOrders || u.nation != human() || !onMap(index) || !map.inside(u.x, u.y)) {
        return std::nullopt;
    }
    const auto typeName = [&](int type) {
        return type >= 0 && type < static_cast<int>(rules_.units.size())
                   ? rules_.units[static_cast<std::size_t>(type)].name
                   : std::string();
    };
    const auto cargoName = [&](int good) {
        return good >= 0 && good < static_cast<int>(rules_.cargo.size())
                   ? rules_.cargo[static_cast<std::size_t>(good)].name
                   : std::string();
    };
    const auto give = [&](TutorialFlag flag, const char* section, int adviser, std::vector<std::string> strings = {}) {
        markTutorialShown(flag);
        GameNotice notice{section, std::move(strings), adviser};
        return std::optional<GameNotice>(std::move(notice));
    };
    const int turn = save_.turn;
    const bool water = map.at(u.x, u.y).isWater();
    int colonies = 0;
    for (const auto& c : save_.colonies) {
        colonies += c.nation == u.nation ? 1 : 0;
    }

    // The first unit up in a Discoverer game: the ship and how to sail.
    if (turn == 0 && save_.difficulty == 0 && !tutorialShown(kTutorialSail)) {
        return give(kTutorialSail, "TUTORIAL1", kNaval, {typeName(u.type)});
    }
    // An empty ship early on (no cargo, nobody aboard): what ships are for.
    if (!tutorialShown(kTutorialShip) && turn < 20 && isShip(index) && u.holds == 0 && passengers(index).empty()) {
        auto notice = give(kTutorialShip, "TUTORIAL11", kNaval, {typeName(u.type), ""});
        notice->ports.emplace_back(1, u.nation);
        return notice;
    }
    // The pioneers ashore before the first colony.
    if (!tutorialShown(kTutorialPioneer) && turn < 20 && u.type == unit_type::kPioneer && colonies == 0 && !water) {
        return give(kTutorialPioneer, "TUTORIAL13", kFrontier);
    }
    if (!tutorialShown(kTutorialSoldier) && turn < 20 && !water && u.type == unit_type::kSoldier) {
        return give(kTutorialSoldier, "TUTORIAL14", kColonial);
    }
    // A colonist standing in a colony: how to put newcomers to work.
    if (turn < 40 && !tutorialShown(kTutorialNewcomers) && u.type == unit_type::kColonist) {
        if (const auto c = colonyAt(u.x, u.y)) {
            return give(kTutorialNewcomers, "TUTORIAL15", kWoman, {save_.colonies[static_cast<std::size_t>(*c)].name});
        }
    }
    // A pioneer on a good site for the first colony: on land, not next to
    // a colony, by water that reaches the sea, with five or more good
    // squares around and a resource worth naming.
    if (!tutorialShown(kTutorialSite) && colonies == 0 && u.type == unit_type::kPioneer) {
        bool good = !(kindOf(map.at(u.x, u.y)) >= 25 && kindOf(map.at(u.x, u.y)) <= 28);
        const int near = nearestColony(save_, u.x, u.y, -1);
        if (near >= 0) {
            const auto& c = save_.colonies[static_cast<std::size_t>(near)];
            if (distance(c.x - u.x, c.y - u.y) == 1) {
                good = false;
            }
        }
        bool port = false;
        for (const auto& s : kSteps) {
            const int x = u.x + s.dx;
            const int y = u.y + s.dy;
            port = port || (map.inside(x, y) && map.at(x, y).isWater() && (map.at(x, y).region & 0x0F) == 1);
        }
        good = good && port;
        if (good) {
            int best = -1; // a good of @CARGO
            int count = 0;
            for (int k = 0; k <= 8; ++k) {
                const int x = u.x + (k < 8 ? kSteps[static_cast<std::size_t>(k)].dx : 0);
                const int y = u.y + (k < 8 ? kSteps[static_cast<std::size_t>(k)].dy : 0);
                if (!map.inside(x, y)) {
                    continue;
                }
                const auto row = static_cast<std::size_t>(terrainRow(map.at(x, y)));
                if (row < rules_.terrain.size() && rules_.terrain[row].yield[0] != 0) {
                    ++count;
                }
                if (map.resourceSeed == 0) {
                    continue;
                }
                switch (resourceAt(map, x, y)) {
                case Resource::Grain:
                    best = best < 0 ? cargo::kFood : best;
                    break;
                case Resource::Cotton:
                    best = cargo::kCotton;
                    break;
                case Resource::Tobacco:
                    best = cargo::kTobacco;
                    break;
                case Resource::Sugar:
                    best = cargo::kSugar;
                    break;
                case Resource::Minerals:
                case Resource::HillsOre:
                    best = cargo::kOre;
                    break;
                case Resource::Beaver:
                case Resource::Deer:
                    best = cargo::kFurs;
                    break;
                case Resource::Timber:
                    best = best < 0 ? cargo::kLumber : best;
                    break;
                case Resource::Silver:
                    best = cargo::kSilver;
                    break;
                default:
                    break;
                }
            }
            // A small piece of land counts once more.
            const int region = landRegion(map, u.x, u.y);
            if (region > 0) {
                int size = 0;
                for (int y = 0; y < map.height; ++y) {
                    for (int x = 0; x < map.width; ++x) {
                        size += landRegion(map, x, y) == region ? 1 : 0;
                    }
                }
                count += size < 7 ? 1 : 0;
            }
            if (best >= 0 && count >= 5) {
                return give(kTutorialSite, "TUTORIAL3", kFrontier, {cargoName(best)});
            }
        }
    }
    // A colonist without a trade once a tribe is met: natives teach.
    if (!tutorialShown(kTutorialLearn) && u.type == unit_type::kColonist &&
        (u.profession == skill::kFreeColonist || u.profession == skill::kServant)) {
        bool met = false;
        for (int tribe = 0; tribe < kTribes; ++tribe) {
            met = met || (save_.nations[static_cast<std::size_t>(u.nation)]
                              .raw[static_cast<std::size_t>(52 + kEuropeanNations + tribe)] &
                          0x20) != 0;
        }
        if (met) {
            const int row = u.profession == skill::kFreeColonist ? skill::kFreeColonistUnit : u.profession;
            const std::string name =
                row < static_cast<int>(rules_.jobs.size()) ? rules_.jobs[static_cast<std::size_t>(row)].name : "";
            return give(kTutorialLearn, "TUTORIAL8", kWoman, {name});
        }
    }
    // A pioneer next to one of our colonies: roads and plowing.
    const auto besideOwnColony = [&]() -> int {
        const int near = nearestColony(save_, u.x, u.y, -1);
        if (near < 0) {
            return -1;
        }
        const auto& c = save_.colonies[static_cast<std::size_t>(near)];
        return distance(c.x - u.x, c.y - u.y) == 1 && c.nation == u.nation ? near : -1;
    };
    if (!tutorialShown(kTutorialRoad) && u.type == unit_type::kPioneer && besideOwnColony() >= 0) {
        const auto& sq = map.at(u.x, u.y);
        const int kind = kindOf(sq);
        if ((sq.flags & (MapSquare::kRoad | MapSquare::kHasColony)) == 0 &&
            (isForestKind(kind) || kind == 27 || kind == 28)) {
            return give(kTutorialRoad, "TUTORIAL9", kFrontier);
        }
    }
    if (!tutorialShown(kTutorialPlow) && u.type == unit_type::kPioneer) {
        const int c = besideOwnColony();
        const auto& sq = map.at(u.x, u.y);
        if (c >= 0 && (sq.flags & MapSquare::kPlowed) == 0) {
            const int kind = kindOf(sq);
            // A forest is worth clearing only later in the game, on good
            // ground, where the colony has no open fields and two forests.
            bool clear = isForestKind(kind) && isFarmland(kind & 7) && turn >= 16;
            const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
            int forests = 0;
            for (const auto& s : kSteps) {
                const int x = colony.x + s.dx;
                const int y = colony.y + s.dy;
                const int k = map.inside(x, y) ? kindOf(map.at(x, y)) : 25;
                clear = clear && !isFarmland(k);
                forests += isForestKind(k) ? 1 : 0;
            }
            clear = clear && forests >= 2;
            if (clear || isFarmland(kind)) {
                return give(kTutorialPlow, "TUTORIAL10", kFrontier);
            }
        }
    }
    if (!tutorialShown(kTutorialConvert) && u.type == unit_type::kColonist && u.profession == skill::kConvert) {
        return give(kTutorialConvert, "TUTORIAL19", kReligious);
    }
    return std::nullopt;
}

std::vector<GameNotice> Game::colonyScreenHints(int c) {
    std::vector<GameNotice> out;
    if (!tutorialHints() || c < 0 || c >= static_cast<int>(save_.colonies.size())) {
        return out;
    }
    const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    const auto cargoName = [&](int good) {
        return good >= 0 && good < static_cast<int>(rules_.cargo.size())
                   ? rules_.cargo[static_cast<std::size_t>(good)].name
                   : std::string();
    };
    if (!tutorialShown(kTutorialJobs) && colony.population > 0) {
        // What the first colonist makes and something else the square
        // could give. The original names the good with the job's number
        // (a carpenter makes hammers).
        const int jobRow = colony.jobs[0];
        int good = jobRow;
        int other = 0;
        if (jobRow == job::kCarpenter) {
            good = cargo::kHammers;
        } else {
            int x = colony.x;
            int y = colony.y;
            for (std::size_t s = 0; s < colony.squares.size(); ++s) {
                if (colony.squares[s] == 0) {
                    x += kColonySquares[s].dx;
                    y += kColonySquares[s].dy;
                }
            }
            const auto row = static_cast<std::size_t>(terrainRow(save_.map.at(x, y)));
            if (row < rules_.terrain.size()) {
                for (int k = 0; k < 8; ++k) {
                    if (rules_.terrain[row].yield[static_cast<std::size_t>(k)] != 0 && k != good) {
                        other = k;
                    }
                }
            }
        }
        markTutorialShown(kTutorialJobs);
        out.push_back(GameNotice{"TUTORIAL4", {cargoName(good), cargoName(other)}, kWoman});
    }
    if (!tutorialShown(kTutorialLoad)) {
        bool ship = false;
        for (const int other : unitsAt(colony.x, colony.y)) {
            ship = ship || isShip(other);
        }
        if (ship) {
            markTutorialShown(kTutorialLoad);
            out.push_back(GameNotice{"TUTORIAL12", {colony.name}, kWoman});
        }
    }
    return out;
}

std::optional<GameNotice> Game::foodHint(int c) {
    if (!tutorialHints() || tutorialShown(kTutorialFood) || c < 0 || c >= static_cast<int>(save_.colonies.size())) {
        return std::nullopt;
    }
    const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    const auto made =
        colonyProduction(colony, save_.map, rules_, save_.difficulty, true, nationEffects(save_, colony.nation));
    if (made.foodEaten <= made.produced[cargo::kFood]) {
        return std::nullopt;
    }
    markTutorialShown(kTutorialFood);
    return GameNotice{"TUTORIAL16", {}, kNobody};
}

std::optional<GameNotice> Game::stockadeHint(int c) {
    if (!tutorialHints() || tutorialShown(kTutorialStockade) || c < 0 || c >= static_cast<int>(save_.colonies.size())) {
        return std::nullopt;
    }
    const auto& colony = save_.colonies[static_cast<std::size_t>(c)];
    if (colony.population < 3 || buildingLevel(colony, BuildingKind::Fortification) > 0) {
        return std::nullopt;
    }
    markTutorialShown(kTutorialStockade);
    return GameNotice{"TUTORIAL7", {colony.name}, kColonial};
}

std::optional<GameNotice> Game::europeHint() {
    if (!tutorialHints() || tutorialShown(kTutorialEurope)) {
        return std::nullopt;
    }
    markTutorialShown(kTutorialEurope);
    const int h = human();
    GameNotice notice{"TUTORIAL17", {"", "", save_.players[static_cast<std::size_t>(h)].country}, kNobody};
    notice.ports.emplace_back(0, h);
    notice.countries.emplace_back(1, h);
    return notice;
}

std::optional<GameNotice> Game::docksHint(int profession) {
    if (!tutorialHints() || tutorialShown(kTutorialDocks)) {
        return std::nullopt;
    }
    markTutorialShown(kTutorialDocks);
    // The same words as the news before it: the home port and who waits
    // there ("Free Colonists").
    const int row = profession == skill::kFreeColonist ? skill::kFreeColonistUnit : profession;
    const std::string who = row >= 0 && row < static_cast<int>(rules_.jobs.size())
                                ? rules_.jobs[static_cast<std::size_t>(row)].expertName
                                : "";
    GameNotice notice{"TUTORIAL5", {"", who}, kNaval};
    notice.ports.emplace_back(0, human());
    return notice;
}

} // namespace viceroy
