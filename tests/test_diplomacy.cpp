// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/savegame.hpp"
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
