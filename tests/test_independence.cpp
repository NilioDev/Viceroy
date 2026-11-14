// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/savegame.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>

using namespace viceroy;

namespace {

constexpr int kHuman = 3; // the Dutch
constexpr int kCrown = 1; // the French left after the succession
constexpr int kPower = 0; // the English: the weakest of the others
constexpr int kOther = 2;
constexpr std::size_t kExtra = static_cast<std::size_t>(kTribes) * kTribeRecordSize;

Ruleset warRules() {
    Ruleset r;
    r.terrain.resize(27);
    r.units.resize(19);
    const char* names[] = {"Colonists", "Soldiers",  "Pioneers",    "Missionaries", "Dragoons",
                           "Scouts",    "Regulars",  "Cont. Cav.",  "Cavalry",      "Cont. Army",
                           "Treasure",  "Artillery", "Wagon Train", "Caravel",      "Merchantman",
                           "Galleon",   "Privateer", "Frigate",     "Man-O-War"};
    for (std::size_t i = 0; i < 19; ++i) {
        r.units[i].name = names[i];
        r.units[i].movement = 1;
    }
    for (std::size_t i = 13; i < 19; ++i) {
        r.units[i].hull = 2;
        r.units[i].cargoSlots = 2;
    }
    r.cargo.resize(16);
    return r;
}

SaveUnit unitAt(int x, int y, int nation, int type, int profession = 0) {
    SaveUnit u;
    u.x = x;
    u.y = y;
    u.nation = nation;
    u.type = type;
    u.profession = profession;
    u.raw[3] = static_cast<std::uint8_t>(nation);
    return u;
}

SaveGame warSave() {
    SaveGame s;
    s.year = 1700;
    s.turn = 201;
    s.difficulty = 1;
    s.humanNation = kHuman;
    s.header[40] = kHuman;
    s.map.width = 6;
    s.map.height = 6;
    s.map.squares.resize(36);
    for (int x = 0; x < 6; ++x) {
        // The sea along the east.
        s.map.squares[static_cast<std::size_t>(x * 6 + 5)].terrain = 25;
        s.map.squares[static_cast<std::size_t>(x * 6 + 5)].region = 1;
    }
    for (int n = 0; n < 4; ++n) {
        s.players[static_cast<std::size_t>(n)].control = n == kHuman ? 0 : n == kCrown ? 2 : 1;
        auto& raw = s.nations[static_cast<std::size_t>(n)].raw;
        raw[0x12] = 0xFF; // nobody up for election
        raw[0x13] = 0xFF;
    }
    s.players[kHuman].name = "Peter Minuit";
    s.header[98] = kCrown;
    s.header[99] = 0;
    s.header[100] = 0xFF; // no power yet
    s.header[101] = 0xFF;
    s.header[96] = 60; // rebels in %
    s.tribesAndMore.assign(kExtra + 727, 0);
    s.tail.assign(1502, 0);
    auto& b = s.tribesAndMore;
    b[kExtra + 28 + kPower] = 30; // people
    b[kExtra + 28 + kOther] = 40;
    b[kExtra + 60 + kPower] = 47;          // attack of the men abroad
    b[kExtra + 44 + 2 * kPower] = 95;      // attack (a word)
    b[kExtra + 64 + kPower * 19 + 16] = 1; // privateers
    b[kExtra + 64 + kPower * 19 + 17] = 2; // frigates
    SaveColony c;
    c.x = 4;
    c.y = 2;
    c.name = "Fort Orange";
    c.nation = kHuman;
    c.population = 10;
    c.rebelDividend = 6000; // 60%, kept over a turn
    c.rebelDivisor = 10000;
    c.stock[0] = 100;
    c.raw[28] = 0x40; // by the water
    s.colonies.push_back(c);
    return s;
}

int headerWord(const Game& g, std::size_t at) {
    return static_cast<std::int16_t>(g.state().header[at] | (g.state().header[at + 1] << 8));
}

bool has(const std::vector<GameNotice>& notices, const std::string& section) {
    return std::any_of(notices.begin(), notices.end(), [&](const GameNotice& n) { return n.section == section; });
}

const GameNotice* find(const std::vector<GameNotice>& notices, const std::string& section) {
    const auto it =
        std::find_if(notices.begin(), notices.end(), [&](const GameNotice& n) { return n.section == section; });
    return it == notices.end() ? nullptr : &*it;
}

} // namespace

VCR_TEST("independence: the menu asks only with a majority, once, and alone") {
    SaveGame s = warSave();
    s.header[96] = 49;
    {
        Game g(s, warRules());
        const auto q = g.independenceQuestion();
        VCR_CHECK(q.section == "TOOTORY" && q.numbers == std::vector<int>{49});
    }
    s.header[96] = 50;
    s.header[17] = 0x80; // several human players
    s.header[36] = 2;
    {
        Game g(s, warRules());
        VCR_CHECK(g.independenceQuestion().section == "MULTIREV");
        g.playAlone();
        VCR_CHECK(g.human() == 2 && g.state().header[40] == 2 && (g.state().header[17] & 0x80) == 0);
        const auto q = g.independenceQuestion();
        VCR_CHECK(q.section == "DECLARE" && q.countries.size() == 1 && q.countries[0].second == 2);
    }
    s.header[18] = 1;
    {
        Game g(s, warRules());
        VCR_CHECK(g.independenceQuestion().section == "ALREADYREVOLUTION");
    }
}

VCR_TEST("independence: the declaration clears the field for the war") {
    SaveGame s = warSave();
    s.units.push_back(unitAt(1, 1, kOther, 0));
    s.units.push_back(unitAt(1, 3, kPower, 1));
    s.units.push_back(unitAt(4, 2, kHuman, 1, 0x15));
    SaveUnit galleon = unitAt(0, 0, kHuman, 15);
    setEuropeStage(galleon, kPortStage);
    s.units.push_back(galleon);
    SaveUnit docks = unitAt(0, 0, kHuman, 0);
    setEuropeStage(docks, kPortStage);
    s.units.push_back(docks);
    s.nations[kHuman].raw[0x0C] = 77; // bells towards the next father
    s.map.squares[0].seen = 0x80 | kCrown;
    s.map.squares[1].seen = 0x20;
    Game g(std::move(s), warRules());
    g.declareIndependence();
    const auto news = g.takeNotices();
    VCR_CHECK(g.independenceDeclared());
    VCR_CHECK(g.crownNation() == kCrown);
    const auto& st = g.state();
    VCR_CHECK(st.header[55] == 17 && st.header[56] == 0);
    VCR_CHECK(st.nations[kHuman].raw[0x0C] == 0);
    // The others are gone; the Crown plays.
    VCR_CHECK(st.players[kOther].control == 2 && st.players[kPower].control == 2);
    VCR_CHECK(st.players[kCrown].control == 1);
    VCR_CHECK(g.unitCount() == 1 && g.unit(0).nation == kHuman && g.unit(0).type == 1);
    VCR_CHECK(g.unit(0).movesUsed >= 1);
    const auto rel = [&](int a, int b) { return st.nations[static_cast<std::size_t>(a)].raw[static_cast<std::size_t>(52 + b)]; };
    VCR_CHECK((rel(kOther, kHuman) & 0x60) == 0x60 && (rel(kHuman, kOther) & 0x60) == 0x60);
    VCR_CHECK((rel(kHuman, kCrown) & 0x62) == 0x22 && (rel(kCrown, kHuman) & 0x62) == 0x22);
    // The ship in Europe is seized; the colonist on the docks just goes.
    const auto* seized = find(news, "SEIZURE");
    VCR_CHECK(seized != nullptr && seized->strings == std::vector<std::string>{"Galleon"});
    VCR_CHECK(std::count_if(news.begin(), news.end(), [](const GameNotice& n) { return n.section == "SEIZURE"; }) == 1);
    const auto* told = find(news, "INDEPENDENCE");
    VCR_CHECK(told != nullptr && told->strings == std::vector<std::string>{"Peter Minuit"});
    // The Crown knows what the rebels know (and the original's mask with
    // the nation's number takes bit 0 off).
    VCR_CHECK(st.map.squares[0].seen == (0x80 | 0x20));
    VCR_CHECK(st.map.squares[1].seen == 0x20);
    // The power and its force: 5 regulars, 3 dragoons, 4 men-of-war, 4 guns.
    VCR_CHECK(headerWord(g, 100) == kPower && headerWord(g, 102) == kOther);
    VCR_CHECK(headerWord(g, 114) == 5 && headerWord(g, 116) == 3 && headerWord(g, 118) == 4 && headerWord(g, 120) == 4);
}

VCR_TEST("independence: veterans join the Continental Army where the Sons are "
         "half") {
    SaveGame s = warSave();
    s.colonies[0].population = 4;
    s.colonies[0].rebelDividend = 10000; // all Sons of Liberty
    s.units.push_back(unitAt(4, 2, kHuman, 1, 0x15));
    s.units.push_back(unitAt(4, 2, kHuman, 1, 0));
    s.units.push_back(unitAt(4, 2, kHuman, 4, 0x15));
    s.units.push_back(unitAt(4, 2, kHuman, 1, 0x15));
    Game g(std::move(s), warRules());
    g.declareIndependence();
    g.takeNotices();
    g.endTurn();
    const auto news = g.takeNotices();
    // 100% of 4: (100 - 50) x 4 / 50 = 4, but at most half the people.
    const auto* told = find(news, "MOBILIZE2");
    VCR_CHECK(told != nullptr && told->strings[0] == "Fort Orange" && told->numbers == std::vector<int>{2});
    VCR_CHECK(g.unit(0).type == 9 && g.unit(1).type == 1 && g.unit(2).type == 7 && g.unit(3).type == 1);
    VCR_CHECK((g.state().nations[kHuman].raw[0] & 0x08) != 0);
    // Only once.
    g.endTurn();
    VCR_CHECK(!has(g.takeNotices(), "MOBILIZE"));
    VCR_CHECK(g.unit(3).type == 1);
}

VCR_TEST("independence: bells bring the power in and its force lands") {
    SaveGame s = warSave();
    Game g(std::move(s), warRules());
    g.declareIndependence();
    g.takeNotices();
    g.endTurn(); // the Continental Army (none here)
    g.takeNotices();
    g.state().nations[kHuman].raw[0x0C] = 0; // the colony's own bell of the turn
    g.addCongressBells(kHuman, 10);
    VCR_CHECK(g.takeNotices().empty());
    g.addCongressBells(kHuman, 10);
    auto news = g.takeNotices();
    // Discoverer: 1500 + 2000 bells.
    const auto* consider = find(news, "CONSIDER");
    VCR_CHECK(has(news, "AMBUSHHINT") && consider != nullptr && consider->numbers == std::vector<int>{3500});
    VCR_CHECK(consider != nullptr && consider->sentenceCountries.size() == 1 &&
              consider->sentenceCountries[0].second == kPower);
    g.addCongressBells(kHuman, 3480);
    news = g.takeNotices();
    VCR_CHECK(news.size() == 1 && news[0].section == "INTERVENTION");
    VCR_CHECK((g.state().header[18] & 0x06) == 0x06);
    VCR_CHECK(g.state().nations[kHuman].raw[0x0C] == 0);
    const int before = g.unitCount();
    g.endTurn();
    news = g.takeNotices();
    const auto* arrive = find(news, "INTERVENE");
    VCR_CHECK(arrive != nullptr && arrive->strings[0] == "Fort Orange");
    // A man-of-war on the sea by the colony, and six men in it: two
    // dragoons and two guns at most, the rest regulars.
    VCR_CHECK(g.unitCount() == before + 7);
    int ships = 0;
    std::array<int, 19> types{};
    for (int i = before; i < g.unitCount(); ++i) {
        const auto& u = g.unit(i);
        if (u.type == 18) {
            ++ships;
            VCR_CHECK(u.x == 5 && std::abs(u.y - 2) <= 1);
        } else {
            VCR_CHECK(u.x == 4 && u.y == 2 && u.profession == 0x15);
        }
        ++types[static_cast<std::size_t>(u.type)];
    }
    VCR_CHECK(ships == 1 && types[9] == 2 && types[7] == 2 && types[11] == 2);
    VCR_CHECK(headerWord(g, 114) == 3 && headerWord(g, 116) == 1 && headerWord(g, 118) == 3 && headerWord(g, 120) == 2);
}
