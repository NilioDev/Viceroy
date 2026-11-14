// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/savegame.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"

#include <algorithm>
#include <string>

using namespace viceroy;

namespace {

constexpr int kHuman = 3; // the Dutch
constexpr int kCrown = 1; // the French left after the succession
constexpr std::size_t kExtra = static_cast<std::size_t>(kTribes) * kTribeRecordSize;

Ruleset endRules() {
    Ruleset r;
    r.terrain.resize(27);
    r.units.resize(19);
    for (auto& u : r.units) {
        u.movement = 1;
    }
    r.cargo.resize(16);
    r.independent = {"United States of America", "Republic of Quebec", "Republic of Mexico", "Republic of Surinam"};
    return r;
}

SaveColony colonyOf(int nation, int population, const std::string& name, bool coastal = true) {
    SaveColony c;
    c.name = name;
    c.nation = nation;
    c.population = population;
    c.raw[28] = coastal ? 0x40 : 0;
    return c;
}

SaveGame endSave() {
    SaveGame s;
    s.year = 1700;
    s.season = 1;
    s.difficulty = 1;
    s.humanNation = kHuman;
    s.header[40] = kHuman;
    s.map.width = 4;
    s.map.height = 4;
    s.map.squares.resize(16);
    for (int n = 0; n < 4; ++n) {
        s.players[static_cast<std::size_t>(n)].control = n == kHuman ? 0 : 1;
    }
    s.players[kHuman].name = "Peter Minuit";
    s.players[kHuman].country = "New Netherland";
    s.header[98] = kCrown;
    s.header[100] = 0; // the English help
    s.tribesAndMore.assign(kExtra + 727, 0);
    s.tail.assign(1502, 0);
    s.colonies.push_back(colonyOf(kHuman, 4, "Fort Orange"));
    return s;
}

SaveGame atWar() {
    SaveGame s = endSave();
    s.header[18] = 0x01 | 0x02;
    s.players[kCrown].control = 1;
    return s;
}

std::vector<std::string> sections(const std::vector<GameNotice>& notices) {
    std::vector<std::string> out;
    for (const auto& n : notices) {
        out.push_back(n.section);
    }
    return out;
}

} // namespace

VCR_TEST("the end: the war is won when the Crown holds nothing and has little left") {
    SaveGame s = atWar();
    s.header[106] = 2; // two regulars and a gun at home
    s.header[112] = 1;
    Game g(s, endRules());
    g.checkTheEnd();
    const auto news = g.takeNotices();
    VCR_CHECK((sections(news) == std::vector<std::string>{"WINNING", "KINGLOSE", "SCORED"}));
    VCR_CHECK(news[0].strings == (std::vector<std::string>{"Peter Minuit", "New Netherland"}));
    VCR_CHECK((g.state().header[18] & 0x18) == 0x18);
    VCR_CHECK(g.state().header[50] == 1);
    VCR_CHECK(g.gameOver());
    g.keepPlaying();
    VCR_CHECK(!g.gameOver());
    g.checkTheEnd();
    VCR_CHECK(g.takeNotices().empty());
}

VCR_TEST("the end: not won while the Crown keeps a colony or troops") {
    {
        SaveGame s = atWar();
        s.header[106] = 3; // three regulars and a gun: four
        s.header[112] = 1;
        Game g(s, endRules());
        g.checkTheEnd();
        VCR_CHECK(!g.gameOver());
    }
    {
        // Once the Crown has taken a colony, up to 7 of its men may stay.
        SaveGame s = atWar();
        s.header[18] |= 0x40;
        SaveUnit u;
        u.nation = kCrown;
        u.type = 6;
        for (int i = 0; i < 7; ++i) {
            s.units.push_back(u);
        }
        Game g(s, endRules());
        g.checkTheEnd();
        VCR_CHECK(g.gameOver());
        s.header[18] = 0x03; // without that, one is too many
        Game h(s, endRules());
        h.checkTheEnd();
        VCR_CHECK(!h.gameOver());
    }
    {
        SaveGame s = atWar();
        s.colonies.push_back(colonyOf(kCrown, 1, "Fort Nassau"));
        Game g(s, endRules());
        g.checkTheEnd();
        VCR_CHECK(!g.gameOver());
    }
}

VCR_TEST("the end: the war is lost with the ports, the colonies or the people") {
    {
        // One port of one colony left, the Crown with most colonists:
        // warned of the colonies (the strongest warning).
        SaveGame s = atWar();
        s.colonies.push_back(colonyOf(kCrown, 30, "Fort Nassau"));
        Game g(s, endRules());
        g.checkTheEnd();
        const auto news = g.takeNotices();
        VCR_CHECK(sections(news) == std::vector<std::string>{"WARN2"});
        // 31 of 36 counted: 86%.
        VCR_CHECK((news[0].numbers == std::vector<int>{1, 1, 86}));
        VCR_CHECK(!g.gameOver());
    }
    {
        // Three ports, but the Crown holds over 90% of the people.
        SaveGame s = atWar();
        s.colonies[0].population = 1;
        s.colonies.push_back(colonyOf(kHuman, 1, "Bergen"));
        s.colonies.push_back(colonyOf(kHuman, 1, "Haarlem"));
        s.colonies.push_back(colonyOf(kCrown, 50, "Fort Nassau"));
        Game g(s, endRules());
        g.checkTheEnd();
        const auto news = g.takeNotices();
        VCR_CHECK((sections(news) == std::vector<std::string>{"LOSING3", "KINGWIN"}));
        VCR_CHECK(news[0].strings == (std::vector<std::string>{"New Netherland", "Peter Minuit"}));
        VCR_CHECK((news[0].countries == std::vector<std::pair<int, int>>{{2, 0}}));
        VCR_CHECK(g.gameOver());
    }
    {
        // An inland colony only: no port.
        SaveGame s = atWar();
        s.colonies[0].raw[28] = 0;
        s.colonies.push_back(colonyOf(kHuman, 4, "Bergen", false));
        s.colonies.push_back(colonyOf(kHuman, 4, "Haarlem", false));
        s.colonies.push_back(colonyOf(kCrown, 1, "Fort Nassau"));
        Game g(s, endRules());
        g.checkTheEnd();
        VCR_CHECK((sections(g.takeNotices()) == std::vector<std::string>{"LOSING1", "KINGWIN"}));
    }
}

VCR_TEST("the end: no colonies after 1600 ends the game") {
    SaveGame s = endSave();
    s.colonies.clear();
    s.year = 1599;
    {
        Game g(s, endRules());
        g.checkTheEnd();
        VCR_CHECK(!g.gameOver());
    }
    s.year = 1600;
    Game g(s, endRules());
    g.checkTheEnd();
    const auto news = g.takeNotices();
    VCR_CHECK(sections(news) == std::vector<std::string>{"LOSENOCOLONIES"});
    VCR_CHECK(news[0].strings[1] == "Peter Minuit");
    VCR_CHECK(g.gameOver());
}

VCR_TEST("the end: the Viceroy retires in 1800, or in 1850 at war") {
    SaveGame s = endSave();
    s.colonies.push_back(colonyOf(kHuman, 9, "Bergen"));
    s.colonies.push_back(colonyOf(kHuman, 2, "Haarlem"));
    s.year = 1790;
    s.season = 0;
    {
        Game g(s, endRules());
        g.checkTheEnd();
        VCR_CHECK(sections(g.takeNotices()) == std::vector<std::string>{"SOONRETIRING0"});
    }
    s.year = 1800;
    {
        Game g(s, endRules());
        g.checkTheEnd();
        const auto news = g.takeNotices();
        VCR_CHECK((sections(news) == std::vector<std::string>{"RETIRING", "SCORED"}));
        VCR_CHECK(news[0].strings[2] == "Bergen");
        VCR_CHECK(g.gameOver());
    }
    {
        // At war the Viceroy stays on.
        SaveGame w = s;
        w.header[18] = 0x03;
        w.header[106] = 9;
        w.players[kCrown].control = 1;
        Game g(w, endRules());
        g.checkTheEnd();
        VCR_CHECK(!g.gameOver());
        w.year = 1850;
        Game h(w, endRules());
        h.checkTheEnd();
        VCR_CHECK((sections(h.takeNotices()) == std::vector<std::string>{"RETIRING2", "SCORED"}));
    }
}

VCR_TEST("the end: another nation's King lets its colonies go") {
    SaveGame s = endSave();
    s.difficulty = 4; // 40 rebels are needed
    auto& french = s.nations[2].raw;
    french[0x19] = 100; // all rebels
    SaveUnit u;
    u.nation = 2;
    for (int i = 0; i < 25; ++i) {
        s.units.push_back(u);
    }
    {
        Game g(s, endRules());
        g.checkTheEnd();
        const auto news = g.takeNotices();
        VCR_CHECK(sections(news) == std::vector<std::string>{"OTHERMIGHT"});
        VCR_CHECK((news[0].numbers == std::vector<int>{25, 25, 40}));
        VCR_CHECK(g.state().nations[2].raw[0x1A] == 25);
        g.checkTheEnd();
        VCR_CHECK(g.takeNotices().empty()); // told already
    }
    {
        SaveGame t = s;
        t.nations[2].raw[0x1A] = 31;
        Game g(t, endRules());
        g.checkTheEnd();
        VCR_CHECK(sections(g.takeNotices()) == std::vector<std::string>{"OTHERLESS"});
    }
    for (int i = 0; i < 15; ++i) {
        s.units.push_back(u);
    }
    s.players[2].country = "New Spain";
    s.nations[2].raw[52 + 1] = 0x22;
    Game g(s, endRules());
    g.checkTheEnd();
    const auto news = g.takeNotices();
    VCR_CHECK(sections(news) == std::vector<std::string>{"OTHERGRANTED"});
    VCR_CHECK(news[0].strings[1] == "New Spain" && news[0].strings[3] == "Republic of Mexico");
    VCR_CHECK(g.state().players[2].country == "Republic of Mexico");
    VCR_CHECK((g.state().nations[2].raw[0] & 0x04) != 0);
    VCR_CHECK(g.state().nations[2].raw[52 + 1] == 0x40 && g.state().nations[1].raw[52 + 2] == 0x40);
    VCR_CHECK(!g.gameOver());
}
