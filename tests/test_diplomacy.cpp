// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/savegame.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"

#include <string>

using namespace viceroy;

namespace {

constexpr int kHuman = 3;  // Dutch
constexpr int kFrench = 1;
constexpr int kEnglish = 0;
constexpr std::size_t kExtra = static_cast<std::size_t>(kTribes) * kTribeRecordSize;

Ruleset diplomacyRules() {
    Ruleset r;
    r.terrain.resize(26);
    r.units.resize(19);
    r.cargo.resize(16);
    return r;
}

SaveGame diplomacySave() {
    SaveGame s;
    s.year = 1520;
    s.turn = 10;
    s.humanNation = kHuman;
    s.map.width = 8;
    s.map.height = 8;
    s.map.squares.resize(64);
    for (int n = 0; n < 4; ++n) {
        s.players[static_cast<std::size_t>(n)].control = n == kHuman ? 0 : 2;
        s.header[88 + 2 * static_cast<std::size_t>(n)] = 0xFF; // never talked
        s.header[89 + 2 * static_cast<std::size_t>(n)] = 0xFF;
    }
    s.header[98] = 0xFF;
    s.header[99] = 0xFF;
    s.players[kHuman].name = "Peter Minuit";
    s.players[kFrench].country = "New France";
    s.tribesAndMore.assign(kExtra + 727, 0);
    // Enough people in the French colonies that they do not complain of sieges.
    s.tribesAndMore[kExtra + 24 + kFrench] = 8;
    SaveUnit dutch;
    dutch.x = 3;
    dutch.y = 3;
    dutch.nation = kHuman;
    s.units.push_back(dutch);
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

VCR_TEST("diplomacy: a first meeting offers a treaty, and peace keeps them away") {
    SaveGame s = diplomacySave();
    SaveUnit french;
    french.x = 4;
    french.y = 3;
    french.nation = kFrench;
    s.units.push_back(french);
    Game g(std::move(s), diplomacyRules());
    VCR_CHECK(g.meetNeighbours(1));
    VCR_CHECK(g.meetingWaiting());
    auto notices = g.takeNotices();
    VCR_CHECK(sections(notices) == (std::vector<std::string>{"HELLOFIRST", "WORTHY"}));
    VCR_CHECK(notices[0].leader == kFrench);
    VCR_CHECK(notices[0].strings.size() == 4 && notices[0].strings[1] == "New France");
    VCR_CHECK(notices[0].music == (std::vector<MusicCue>{{MusicCue::Kind::Sound, 0x8020 + kFrench}}));
    VCR_CHECK(notices[1].value == Game::kMeetingQuestion);
    VCR_CHECK(g.lastTalk(kFrench) == 10);
    VCR_CHECK((g.relation(kHuman, kFrench) & 0x20) != 0);
    g.answerMeeting(0); // "Yes"
    VCR_CHECK((g.relation(kHuman, kFrench) & 0x40) != 0);
    VCR_CHECK((g.relation(kFrench, kHuman) & 0x40) != 0);
    VCR_CHECK(g.lastTalk(kFrench) == 26);
    notices = g.takeNotices();
    VCR_CHECK(sections(notices) == std::vector<std::string>{"PEACEMEEK"});
    g.answerMeeting(0); // go in peace
    VCR_CHECK(!g.meetingWaiting());
    VCR_CHECK(g.takeNotices().empty());
    // At peace the French stay away 2 x (6 - difficulty) turns.
    VCR_CHECK(g.state().nations[kFrench].raw[64 + kHuman] == 12);
}

VCR_TEST("diplomacy: refusing the treaty leaves them at war") {
    SaveGame s = diplomacySave();
    SaveUnit french;
    french.x = 4;
    french.y = 4;
    french.nation = kFrench;
    s.units.push_back(french);
    Game g(std::move(s), diplomacyRules());
    VCR_CHECK(g.meetNeighbours(1));
    g.takeNotices();
    g.answerMeeting(1); // "No"
    VCR_CHECK((g.relation(kHuman, kFrench) & 0x40) == 0);
    const auto notices = g.takeNotices();
    VCR_CHECK(sections(notices) == std::vector<std::string>{"WARMEEK"});
    VCR_CHECK(notices[0].music == (std::vector<MusicCue>{{MusicCue::Kind::ChangeMood, 4}}));
    VCR_CHECK(!g.meetingWaiting());
}

VCR_TEST("diplomacy: two computer nations make peace early in the game") {
    SaveGame s = diplomacySave();
    SaveUnit english;
    english.x = 6;
    english.y = 6;
    english.nation = kEnglish;
    SaveUnit french;
    french.x = 7;
    french.y = 6;
    french.nation = kFrench;
    s.units.push_back(english);
    s.units.push_back(french);
    Game g(std::move(s), diplomacyRules());
    VCR_CHECK(!g.meetNeighbours(2));
    VCR_CHECK((g.relation(kFrench, kEnglish) & 0x60) == 0x60);
    VCR_CHECK((g.relation(kEnglish, kFrench) & 0x60) == 0x60);
    const auto notices = g.takeNotices();
    VCR_CHECK(sections(notices) == std::vector<std::string>{"SIGNTREATY"});
    VCR_CHECK(notices[0].value == Game::kForeignNews);
    VCR_CHECK(notices[0].adjectives == (std::vector<std::pair<int, int>>{{0, kFrench}, {1, kEnglish}}));
    // At peace, the timer of the side met is set as after a talk.
    VCR_CHECK(g.state().nations[kEnglish].raw[64 + kFrench] == 12);
}

namespace {

SaveGame scoutSave() {
    SaveGame s = diplomacySave();
    SaveColony quebec;
    quebec.x = 5;
    quebec.y = 3;
    quebec.name = "Quebec";
    quebec.nation = kFrench;
    quebec.population = 1;
    s.colonies.push_back(quebec);
    SaveUnit scout;
    scout.x = 4;
    scout.y = 3;
    scout.type = unit_type::kScout;
    scout.nation = kHuman;
    s.units.push_back(scout);
    // Met and talked this turn: only a forced talk brings the leader.
    s.nations[kHuman].raw[52 + kFrench] = 0x20;
    s.nations[kFrench].raw[52 + kHuman] = 0x20;
    s.header[88 + 2 * kFrench] = 10;
    s.header[89 + 2 * kFrench] = 0;
    return s;
}

} // namespace

VCR_TEST("diplomacy: a scout meets the mayor, except in the war of independence") {
    {
        Game g(scoutSave(), diplomacyRules());
        g.scoutMeetMayor(1, 0);
        const auto notices = g.takeNotices();
        VCR_CHECK(!notices.empty() && notices[0].section == "HELLOMEEK" && notices[0].leader == kFrench);
        VCR_CHECK(g.movesLeft(1) <= 0);
    }
    SaveGame s = scoutSave();
    s.header[18] = 1;
    Game g(std::move(s), diplomacyRules());
    g.scoutMeetMayor(1, 0);
    const auto notices = g.takeNotices();
    VCR_CHECK(sections(notices) == std::vector<std::string>{"NOMAYORSDURINGREV"});
}

VCR_TEST("diplomacy: the odds of a scout caught infiltrating a colony") {
    SaveGame s = scoutSave();
    s.difficulty = 4;
    Game g(std::move(s), diplomacyRules());
    VCR_CHECK(g.infiltrationRisk(1, 0) == 14); // 12, and 2 for Viceroy
    SaveGame seasoned = scoutSave();
    seasoned.units[1].profession = 0x16;
    Game h(std::move(seasoned), diplomacyRules());
    VCR_CHECK(h.infiltrationRisk(1, 0) == 4); // 6, and -2 for Discoverer
    // Caught or not, the scout's turn is over; caught, it is gone and the
    // colony has its horses.
    const bool caught = h.infiltrateColony(1, 0);
    const auto notices = h.takeNotices();
    if (caught) {
        VCR_CHECK(h.unitCount() == 1);
        VCR_CHECK(h.state().colonies[0].stock[8] == 100);
        VCR_CHECK(sections(notices) == std::vector<std::string>{"LOSTOURSCOUTS"});
        VCR_CHECK(notices[0].strings[1] == "Quebec");
    } else {
        VCR_CHECK(notices.empty());
        VCR_CHECK(h.movesLeft(1) <= 0);
    }
}

VCR_TEST("diplomacy: the King declares war on a nation the player has a treaty with") {
    SaveGame s = diplomacySave();
    s.difficulty = 4;
    s.turn = 140; // 6 x 140 = 840: late enough
    for (const int e : {kEnglish, kFrench}) {
        s.nations[kHuman].raw[52 + static_cast<std::size_t>(e)] = 0x60;
        s.nations[static_cast<std::size_t>(e)].raw[52 + kHuman] = 0x60;
    }
    s.tribesAndMore[kExtra + 60 + kFrench] = 30; // the French are stronger
    s.tribesAndMore[kExtra + 60 + kEnglish] = 30;
    s.tribesAndMore[kExtra + 60 + kHuman] = 6;
    {
        SaveGame early = s;
        early.turn = 133; // 6 x 133 = 798
        Game g(std::move(early), diplomacyRules());
        for (int i = 0; i < 200; ++i) {
            g.kingDeclaresWar(kHuman, 133);
        }
        VCR_CHECK(g.takeNotices().empty());
    }
    Game g(std::move(s), diplomacyRules());
    const long gold = g.state().nations[kHuman].gold;
    std::vector<GameNotice> notices;
    for (int i = 0; i < 500 && notices.empty(); ++i) {
        g.kingDeclaresWar(kHuman, 140);
        notices = g.takeNotices();
    }
    VCR_CHECK(sections(notices) == std::vector<std::string>{"KINGNEWWAR"});
    const int enemy = notices[0].adjectives[0].second;
    VCR_CHECK(enemy == kEnglish || enemy == kFrench);
    // 500 at most at Viceroy, and soldiers (30 - 6) / 8 + 1 = 4, at most 2.
    VCR_CHECK(notices[0].numbers == (std::vector<int>{500, 2}));
    VCR_CHECK(g.state().nations[kHuman].gold == gold + 500);
    VCR_CHECK(g.unitCount() == 3 && g.unit(2).profession == 0x15 && g.unit(1).type == 1);
    VCR_CHECK((g.relation(kHuman, enemy) & 0x60) == 0x20);
    VCR_CHECK((g.relation(enemy, kHuman) & 0x10) != 0);
    VCR_CHECK(g.lastTalk(enemy) == 140);
    // At war now: no second war.
    for (int i = 0; i < 200; ++i) {
        g.kingDeclaresWar(kHuman, 140);
    }
    VCR_CHECK(g.takeNotices().empty());
}

VCR_TEST("diplomacy: the War of the Spanish Succession hands the weakest nation to the next") {
    constexpr int kSpanish = 2;
    SaveGame s = diplomacySave();
    auto& block = s.tribesAndMore;
    block[kExtra + 40 + kEnglish] = 3; // ships: 3 x 3 = 9
    block[kExtra + 16 + kFrench] = 1;  // a colony: 2
    block[kExtra + 28 + kSpanish] = 3; // people: 3
    s.map.squares[8].seen = static_cast<std::uint8_t>(0x10 << kFrench);
    s.map.squares[9].region = static_cast<std::uint8_t>(0x03 | (kFrench << 4));
    SaveColony quebec;
    quebec.x = 5;
    quebec.y = 5;
    quebec.name = "Quebec";
    quebec.nation = kFrench;
    quebec.rebelDividend = 300;
    s.colonies.push_back(quebec);
    SaveUnit onMap;
    onMap.x = 5;
    onMap.y = 5;
    onMap.nation = kFrench;
    SaveUnit inEurope = onMap;
    inEurope.x = 0xFF;
    s.units.push_back(onMap);
    s.units.push_back(inEurope);
    SaveSettlement village;
    village.nation = 4;
    village.raw[5] = static_cast<std::uint8_t>(0x80 | kFrench);
    s.settlements.push_back(village);
    Game g(std::move(s), diplomacyRules());
    g.spanishSuccession();
    const auto notices = g.takeNotices();
    VCR_CHECK(sections(notices) == std::vector<std::string>{"SUCCESSION"});
    VCR_CHECK(notices[0].sentenceCountries == (std::vector<std::pair<int, int>>{{0, kFrench}}));
    VCR_CHECK(notices[0].adjectives == (std::vector<std::pair<int, int>>{{2, kSpanish}, {3, kFrench}}));
    const auto& after = g.state();
    VCR_CHECK(after.players[kFrench].control == 2);
    VCR_CHECK(after.header[98] == kFrench && after.header[99] == 0);
    VCR_CHECK(after.colonies[0].nation == kSpanish && after.colonies[0].rebelDividend == 0);
    VCR_CHECK(g.unitCount() == 2 && g.unit(1).nation == kSpanish);
    VCR_CHECK(after.map.squares[8].seenBy(kSpanish));
    VCR_CHECK(after.map.squares[9].region == (0x03 | (kSpanish << 4)));
    VCR_CHECK(after.settlements[0].raw[5] == (0x80 | kSpanish));
    // Once only.
    g.spanishSuccession();
    VCR_CHECK(g.takeNotices().empty());
}

VCR_TEST("diplomacy: a computer's attack in peace is a sneak attack, unless it talked lately") {
    SaveGame s = diplomacySave();
    s.nations[kHuman].raw[52 + kFrench] = 0x60;
    s.nations[kFrench].raw[52 + kHuman] = 0x60;
    s.nations[kFrench].raw[64 + kHuman] = 3; // talked lately
    Game g(std::move(s), diplomacyRules());
    VCR_CHECK(!g.europeanAttack(kFrench, kHuman));
    VCR_CHECK(g.takeNotices().empty() && g.atPeace(kFrench, kHuman));
    SaveGame t = diplomacySave();
    t.nations[kHuman].raw[52 + kFrench] = 0x60;
    t.nations[kFrench].raw[52 + kHuman] = 0x60;
    Game h(std::move(t), diplomacyRules());
    VCR_CHECK(h.europeanAttack(kFrench, kHuman));
    const auto notices = h.takeNotices();
    VCR_CHECK(sections(notices) == (std::vector<std::string>{"SNEAK", "DECLAREWAR"}));
    VCR_CHECK(!h.atPeace(kFrench, kHuman) && !h.atPeace(kHuman, kFrench));
    // The player breaking a treaty: the victim wants revenge.
    SaveGame u = diplomacySave();
    u.nations[kHuman].raw[52 + kFrench] = 0x60;
    u.nations[kFrench].raw[52 + kHuman] = 0x60;
    Game k(std::move(u), diplomacyRules());
    k.europeanAttack(kHuman, kFrench);
    VCR_CHECK(k.takeNotices().empty());
    VCR_CHECK((k.relation(kFrench, kHuman) & 0x62) == 0x22);
}

VCR_TEST("diplomacy: trade with a foreign colony needs a treaty and Jan de Witt") {
    SaveGame s = diplomacySave();
    SaveColony quebec;
    quebec.x = 5;
    quebec.y = 3;
    quebec.name = "Quebec";
    quebec.nation = kFrench;
    quebec.stock[11] = 300; // cloth
    quebec.stock[9] = 40;   // rum
    quebec.stock[0] = 500;  // food: never offered
    s.colonies.push_back(quebec);
    SaveUnit wagon;
    wagon.x = 4;
    wagon.y = 3;
    wagon.type = 12;
    wagon.nation = kHuman;
    wagon.holds = 1;
    setHold(wagon, 0, 4, 100); // 100 furs
    s.units.push_back(wagon);
    for (auto& p : s.nations[kFrench].prices) {
        p = 1;
    }
    s.nations[kFrench].prices[4] = 6;  // furs: bid 5
    s.nations[kFrench].prices[11] = 8; // cloth: bid 7
    s.nations[kFrench].prices[9] = 3;  // rum: bid 2
    {
        Game g(s, diplomacyRules());
        VCR_CHECK(g.startForeignTrade(1, 0) == 0);
        VCR_CHECK(sections(g.takeNotices()) == std::vector<std::string>{"TRADEATWAR"});
        VCR_CHECK(g.movesLeft(1) <= 0);
    }
    s.nations[kHuman].raw[52 + kFrench] = 0x60;
    s.nations[kFrench].raw[52 + kHuman] = 0x60;
    {
        Game g(s, diplomacyRules());
        VCR_CHECK(g.startForeignTrade(1, 0) == 0);
        const auto notices = g.takeNotices();
        VCR_CHECK(sections(notices) == std::vector<std::string>{"TRADEMERCANTILISM"});
        VCR_CHECK(notices[0].leader == kFrench);
    }
    s.nations[kHuman].raw[7] = 0x10; // Jan de Witt
    Game g(std::move(s), diplomacyRules());
    VCR_CHECK(g.startForeignTrade(1, 0) == 1);
    g.offerForeignTrade(1, 0, 0);
    const auto notices = g.takeNotices();
    VCR_CHECK(sections(notices) == std::vector<std::string>{"TRADEWITH"});
    // 500 worth: 71 cloth (497) beats 40 rum (80); the gold is 500 less a
    // margin of 10% to 12%.
    const auto& offer = *g.foreignTradeOffer();
    VCR_CHECK(offer.giveGood == 11 && offer.giveAmount == 71);
    VCR_CHECK(offer.gold >= 440 && offer.gold <= 450);
    g.answerForeignTrade(0);
    VCR_CHECK(holdGood(g.unit(1), 0) == 11 && holdAmount(g.unit(1), 0) == 71);
    VCR_CHECK(g.state().colonies[0].stock[4] == 100);
    VCR_CHECK(g.state().colonies[0].stock[11] == 300); // as in the original
}
