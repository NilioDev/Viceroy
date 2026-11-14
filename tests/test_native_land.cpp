// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"

using namespace viceroy;

namespace {

constexpr int kDutch = 3;
constexpr int kEnglish = 0;
constexpr int kArawak = 2; // tribe index (nation 6)
constexpr std::size_t kExtra = static_cast<std::size_t>(kTribes) * kTribeRecordSize;

Ruleset landRules() {
    Ruleset r;
    r.terrain.resize(27);
    for (auto& t : r.terrain) {
        t.movementCost = 1;
        t.improvementTurns = 2;
    }
    r.units.resize(14);
    r.units[unit_type::kColonist] = {"Colonists", 101, 1, 0, 1, 0, 1, 1, 0, 0, 0, 0};
    r.units[unit_type::kPioneer] = {"Pioneers", 102, 1, 0, 1, 0, 1, 2, 0, 0, 0, 0};
    return r;
}

// A 7 x 7 land of mixed forest (one region) with an Arawak village in the
// middle and a pioneer next to it.
SaveGame landSave(int nation = kDutch) {
    SaveGame s;
    s.year = 1600;
    s.difficulty = 1;
    s.humanNation = kDutch;
    s.header[40] = kDutch;
    s.map.width = 7;
    s.map.height = 7;
    s.map.squares.resize(49);
    for (auto& sq : s.map.squares) {
        sq.terrain = 10; // mixed forest
        sq.region = 1;
    }
    for (int n = 0; n < 4; ++n) {
        s.players[static_cast<std::size_t>(n)].control = n == kDutch ? 0 : 2; // the others gone: no turns of theirs
    }
    s.tribesAndMore.assign(kExtra + 727, 0);
    s.tribesAndMore[kExtra + 28 + static_cast<std::size_t>(nation)] = 10; // people
    // A treaty with the Arawak.
    s.tribesAndMore[static_cast<std::size_t>(kArawak) * kTribeRecordSize + 58 + static_cast<std::size_t>(nation)] =
        0x60;
    SaveSettlement v;
    v.x = 3;
    v.y = 3;
    v.nation = 4 + kArawak;
    v.raw[0] = 3;
    v.raw[1] = 3;
    v.raw[2] = static_cast<std::uint8_t>(4 + kArawak);
    s.settlements.push_back(v);
    SaveUnit u;
    u.x = 3;
    u.y = 2;
    u.type = unit_type::kPioneer;
    u.nation = nation;
    u.tools = 100;
    s.units.push_back(u);
    s.nations[static_cast<std::size_t>(nation)].gold = 1000;
    return s;
}

int tension(const Game& g, int nation) { return tribeTension(g.state(), kArawak, nation); }

} // namespace

VCR_TEST("native land: the price of a square") {
    Game g(landSave(), landRules());
    // (difficulty 1 + 3) x 2 + level 0 + land sold 0 - distance 1 = 7, x 65,
    // x (tension step 0 + 1), halved.
    VCR_CHECK(g.landPrice(0, kDutch, 3, 2) == 7 * 65 / 2);
    // Two away: 6.
    VCR_CHECK(g.landPrice(0, kDutch, 3, 1) == 6 * 65 / 2);
    // A computer player: level + sold - difficulty - distance + 12 = 10, x 50.
    Game e(landSave(kEnglish), landRules());
    VCR_CHECK(e.landPrice(0, kEnglish, 3, 2) == 10 * 50 / 2);
    // A small nation pays less: 4 people take (10 - 4) / 2 = 3 off.
    SaveGame small = landSave();
    small.tribesAndMore[kExtra + 28 + kDutch] = 4;
    Game s(std::move(small), landRules());
    VCR_CHECK(s.landPrice(0, kDutch, 3, 2) == 4 * 65 / 2);
}

VCR_TEST("native land: the tribe asks before forest is cleared or a road built") {
    Game g(landSave(), landRules());
    const auto forest = g.workClaim(0, SaveUnit::kPlow);
    VCR_CHECK(forest.has_value() && forest->tribe == kArawak && forest->price == 7 * 65 / 2);
    VCR_CHECK(g.workClaim(0, SaveUnit::kRoad).has_value());
    // Paid for: no more claim, and the tribe has sold one more square.
    VCR_CHECK(g.buyLand(*forest, kDutch));
    VCR_CHECK(g.state().nations[kDutch].gold == 1000 - 7 * 65 / 2);
    VCR_CHECK(!g.workClaim(0, SaveUnit::kPlow).has_value());
    VCR_CHECK(g.state().tribesAndMore[kArawak * kTribeRecordSize + 5] == 1);
    // Plowing open land, or without a treaty, nobody asks.
    SaveGame open = landSave();
    open.map.at(3, 2).terrain = 2;
    Game o(std::move(open), landRules());
    VCR_CHECK(!o.workClaim(0, SaveUnit::kPlow).has_value());
    VCR_CHECK(o.workClaim(0, SaveUnit::kRoad).has_value());
    SaveGame strangers = landSave();
    strangers.tribesAndMore[kArawak * kTribeRecordSize + 58 + kDutch] = 0x20;
    Game n(std::move(strangers), landRules());
    VCR_CHECK(!n.workClaim(0, SaveUnit::kPlow).has_value());
}

VCR_TEST("native land: cleared forest and roads on the tribe's land anger it") {
    // A road next to the village: (3 + difficulty 1) x 3.
    Game g(landSave(), landRules());
    g.startWork(0, SaveUnit::kRoad);
    for (int turn = 0; turn < 10 && g.unit(0).orders != SaveUnit::kNoOrders; ++turn) {
        g.endTurn();
    }
    VCR_CHECK(tension(g, kDutch) == 12);
    // Forest cleared two away from an advanced tribe's village (its land
    // reaches 2): (5 + 1) x 2, even without a treaty.
    SaveGame far = landSave();
    far.units[0].y = 1;
    far.tribesAndMore[kArawak * kTribeRecordSize + 2] = 2;
    far.tribesAndMore[kArawak * kTribeRecordSize + 58 + kDutch] = 0;
    Game f(std::move(far), landRules());
    f.startWork(0, SaveUnit::kPlow);
    for (int turn = 0; turn < 10 && f.unit(0).orders != SaveUnit::kNoOrders; ++turn) {
        f.endTurn();
    }
    VCR_CHECK(tension(f, kDutch) == 12);
    // A semi-nomadic tribe's land reaches only 1.
    SaveGame nomads = landSave();
    nomads.units[0].y = 1;
    Game m(std::move(nomads), landRules());
    m.startWork(0, SaveUnit::kPlow);
    for (int turn = 0; turn < 10 && m.unit(0).orders != SaveUnit::kNoOrders; ++turn) {
        m.endTurn();
    }
    VCR_CHECK(tension(m, kDutch) == 0);
    // Bought land angers nobody.
    Game b(landSave(), landRules());
    VCR_CHECK(b.buyLand(*b.workClaim(0, SaveUnit::kRoad), kDutch));
    b.startWork(0, SaveUnit::kRoad);
    for (int turn = 0; turn < 10 && b.unit(0).orders != SaveUnit::kNoOrders; ++turn) {
        b.endTurn();
    }
    VCR_CHECK(tension(b, kDutch) == 0);
}

VCR_TEST("native land: a tribe without settlements is wiped out") {
    SaveGame s = landSave();
    s.tribesAndMore[kExtra + 581 + kArawak] = 1;
    Game g(std::move(s), landRules());
    g.burnSettlement(0);
    VCR_CHECK((g.state().tribesAndMore[kArawak * kTribeRecordSize + 3] & 0x80) != 0);
    const auto news = g.takeNotices();
    VCR_CHECK(news.size() == 1 && news[0].section == "EXTINCT");
    VCR_CHECK((news[0].adjectives == std::vector<std::pair<int, int>>{{0, 4 + kArawak}}));
}

VCR_TEST("native trade: a tense tribe refuses ships and wagon trains") {
    Ruleset rules = landRules();
    rules.units.resize(15);
    rules.units[unit_type::kWagonTrain] = {"Wagon Train", 112, 1, 0, 1, 2, 1, 0, 0, 0, 0, 0};
    rules.units[unit_type::kCaravel] = {"Caravel", 113, 4, 0, 2, 2, 1, 0, 0, 0, 1, 0};
    const std::size_t relation = static_cast<std::size_t>(kArawak) * kTribeRecordSize + 58 + kDutch;
    {
        SaveGame s = landSave();
        s.units[0].type = unit_type::kCaravel;
        s.tribesAndMore[relation] = 0x40; // a treaty, but never met on land
        Game g(s, rules);
        VCR_CHECK(g.shipTradeRefusal(0, 0) == "DONTKNOWSHIPS");
    }
    {
        SaveGame s = landSave();
        s.units[0].type = unit_type::kCaravel;
        Game g(s, rules);
        VCR_CHECK(g.shipTradeRefusal(0, 0).empty());
        setTribeTension(g.state(), kArawak, kDutch, 75);
        VCR_CHECK(g.shipTradeRefusal(0, 0) == "MADATSHIPS");
        setTribeTension(g.state(), kArawak, kDutch, 74);
        VCR_CHECK(g.shipTradeRefusal(0, 0).empty());
        g.state().settlements[0].raw[10 + 2 * kDutch] = 64; // the settlement's alarm
        VCR_CHECK(g.shipTradeRefusal(0, 0) == "MADATSHIPS");
    }
    {
        SaveGame s = landSave();
        s.units[0].type = unit_type::kWagonTrain;
        Game g(s, rules);
        setTribeTension(g.state(), kArawak, kDutch, 74);
        VCR_CHECK(g.wagonTradeWelcome(0, 0).empty());
        VCR_CHECK(g.unitCount() == 1);
        // Above 500 the roll always falls under the tension: gone.
        setTribeTension(g.state(), kArawak, kDutch, 600);
        VCR_CHECK(g.wagonTradeWelcome(0, 0) == "KILLWAGONS");
        VCR_CHECK(g.unitCount() == 0);
    }
    {
        // At 75 the three answers come in about 76, 75 and 350 of 501.
        int killed = 0;
        int mad = 0;
        int grudging = 0;
        SaveGame s = landSave();
        s.units[0].type = unit_type::kWagonTrain;
        const SaveUnit wagon = s.units[0];
        Game g(s, rules);
        setTribeTension(g.state(), kArawak, kDutch, 75);
        for (int k = 0; k < 400; ++k) {
            const auto w = g.wagonTradeWelcome(0, 0);
            killed += w == "KILLWAGONS";
            mad += w == "MADATWAGONS";
            grudging += w == "GRUDGEWAGONS";
            if (g.unitCount() == 0) {
                g.state().units.push_back(wagon);
            }
        }
        VCR_CHECK(killed + mad + grudging == 400);
        VCR_CHECK(grudging > killed + mad);
        VCR_CHECK(killed > 20 && mad > 20);
    }
}
