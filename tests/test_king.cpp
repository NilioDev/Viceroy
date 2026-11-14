// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/savegame.hpp"
#include "viceroy/game/colony.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"
#include "viceroy/game/sounds.hpp"

#include <algorithm>
#include <string>

using namespace viceroy;

namespace {

constexpr int kHuman = 3;
constexpr int kSugar = 1;
constexpr int kCoats = 12;

Ruleset kingRules() {
    Ruleset r;
    r.terrain.resize(26);
    r.units.resize(19);
    const char* names[] = {"Colonists", "Soldiers", "Pioneers", "Missionaries", "Dragoons", "Scouts", "Regulars",
                           "Cont. Cav.", "Cavalry", "Cont. Army", "Treasure", "Artillery", "Wagon Train", "Caravel",
                           "Merchantman", "Galleon", "Privateer", "Frigate", "Man-O-War"};
    for (std::size_t i = 0; i < 19; ++i) {
        r.units[i].name = names[i];
    }
    r.units[10].movement = 1;
    r.cargo.resize(16);
    r.cargo[kSugar].name = "Sugar";
    r.cargo[kCoats].name = "Coats";
    return r;
}

SaveGame kingSave() {
    SaveGame s;
    s.year = 1650;
    s.season = 0;
    s.turn = 200;
    s.humanNation = kHuman;
    s.map.width = 6;
    s.map.height = 6;
    s.map.squares.resize(36);
    for (int n = 0; n < 4; ++n) {
        s.players[static_cast<std::size_t>(n)].control = n == kHuman ? 0 : 2;
        auto& raw = s.nations[static_cast<std::size_t>(n)].raw;
        raw[0x12] = 0xFF; // nobody up for election
        raw[0x13] = 0xFF;
    }
    s.players[kHuman].name = "Peter Minuit";
    s.players[kHuman].country = "New Netherland";
    s.header[98] = 0xFF; // nobody has had half its people rebels yet
    s.header[99] = 0xFF;
    s.tribesAndMore.assign(static_cast<std::size_t>(kTribes) * kTribeRecordSize + 727, 0);
    s.tail.assign(1502, 0);
    SaveColony c;
    c.x = 2;
    c.y = 2;
    c.name = "Fort Orange";
    c.nation = kHuman;
    c.population = 1;
    c.rebelDivisor = 200;
    c.raw[28] = 0x40; // by the water
    c.stock[cargo::kFood] = 100;
    s.colonies.push_back(c);
    return s;
}

int boycottWord(const Game& g) {
    const auto& raw = g.state().nations[kHuman].raw;
    return raw[0x20] | (raw[0x21] << 8);
}

} // namespace

VCR_TEST("king: a rise in tax may be answered with a party") {
    SaveGame s = kingSave();
    s.colonies[0].stock[kCoats] = 150;
    s.nations[kHuman].tonsTraded[kCoats] = 1000;
    s.nations[kHuman].taxRate = 2;
    Game g(std::move(s), kingRules());
    GameNotice ask;
    ask.section = "KINGSTAMPACT";
    g.changeTax(kHuman, 6, ask);
    VCR_CHECK(g.state().nations[kHuman].taxRate == 8);
    auto notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1);
    VCR_CHECK(notices[0].value == Game::kTaxQuestion);
    VCR_CHECK(notices[0].numbers == (std::vector<int>{6, 8}));
    VCR_CHECK(notices[0].strings.size() == 4 && notices[0].strings[3] == "Fort Orange Coats");
    VCR_CHECK(notices[0].music == (std::vector<MusicCue>{{MusicCue::Kind::Sound, sounds::kKing}}));
    VCR_CHECK(g.taxRaiseWaiting());
    g.answerTaxRaise(true);
    VCR_CHECK(!g.taxRaiseWaiting());
    // The rise is undone, 100 coats go into the sea and into the rebels'
    // count, and coats are boycotted.
    VCR_CHECK(g.state().nations[kHuman].taxRate == 2);
    VCR_CHECK(g.state().colonies[0].stock[kCoats] == 50);
    VCR_CHECK(g.state().colonies[0].rebelDividend == 100);
    VCR_CHECK(g.isBoycotted(kHuman, kCoats));
    VCR_CHECK(!g.isBoycotted(kHuman, kSugar));
    notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices[0].section == "TEAPARTY");
    VCR_CHECK(notices[0].numbers == std::vector<int>{100});
    VCR_CHECK(notices[0].strings[0] == "Coats" && notices[0].strings[1] == "Fort Orange");
    VCR_CHECK(notices[0].music == (std::vector<MusicCue>{{MusicCue::Kind::Sound, sounds::kParty}}));
}

VCR_TEST("king: kissing the ring keeps the rise; no party without goods by the water") {
    SaveGame s = kingSave();
    s.colonies[0].stock[kCoats] = 150;
    s.nations[kHuman].tonsTraded[kCoats] = 1000;
    Game g(std::move(s), kingRules());
    g.changeTax(kHuman, 3, GameNotice{"KINGNAVACT", {}});
    g.answerTaxRaise(false);
    VCR_CHECK(g.state().nations[kHuman].taxRate == 3);
    VCR_CHECK(boycottWord(g) == 0);
    (void)g.takeNotices();
    // Inland goods are not thrown into the sea: no question.
    g.state().colonies[0].raw[28] = 0;
    g.changeTax(kHuman, 3, GameNotice{"KINGNAVACT", {}});
    const auto notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices[0].value != Game::kTaxQuestion);
    VCR_CHECK(!g.taxRaiseWaiting());
}

VCR_TEST("king: the tax stays within 0 and 75") {
    SaveGame s = kingSave();
    s.nations[kHuman].taxRate = 72;
    Game g(std::move(s), kingRules());
    g.changeTax(kHuman, 8, GameNotice{"KINGSTAMPACT", {}});
    VCR_CHECK(g.state().nations[kHuman].taxRate == 75);
    auto notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices[0].numbers == (std::vector<int>{3, 75}));
    g.changeTax(kHuman, -90, GameNotice{"KINGVICTORY", {}});
    VCR_CHECK(g.state().nations[kHuman].taxRate == 0);
    notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices[0].numbers == (std::vector<int>{75, 0}));
    // A fall comes with a happier tune.
    VCR_CHECK(notices[0].music == (std::vector<MusicCue>{{MusicCue::Kind::MoodEvent, 2}}));
}

VCR_TEST("king: back taxes end a boycott") {
    SaveGame s = kingSave();
    s.nations[kHuman].raw[0x20] = 0x02; // sugar boycotted
    s.nations[kHuman].prices[kSugar] = 4;
    s.nations[kHuman].gold = 1000;
    Ruleset r = kingRules();
    r.cargo[kSugar].burden = 2;
    Game g(std::move(s), r);
    VCR_CHECK(g.isBoycotted(kHuman, kSugar));
    // 500 times the price asked in Europe.
    VCR_CHECK(g.backTaxes(kSugar) == 500 * askPrice(g.rules(), g.state().nations[kHuman], kSugar));
    const int owed = g.backTaxes(kSugar);
    VCR_CHECK(owed > 1000);
    VCR_CHECK(!g.payBackTaxes(kSugar));
    auto notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices[0].section == "KISSSORRY" && notices[0].numbers == std::vector<int>{1000});
    g.state().nations[kHuman].gold = owed + 5;
    VCR_CHECK(g.payBackTaxes(kSugar));
    VCR_CHECK(!g.isBoycotted(kHuman, kSugar));
    VCR_CHECK(g.state().nations[kHuman].gold == 5);
}

VCR_TEST("king: the nation's rebels and the news of them") {
    SaveGame s = kingSave();
    s.colonies[0].population = 4;
    s.colonies[0].rebelDividend = 300; // 75%
    s.colonies[0].rebelDivisor = 400;
    SaveColony other = s.colonies[0];
    other.x = 4;
    other.population = 2; // 0%
    other.rebelDividend = 0;
    s.colonies.push_back(other);
    s.header[104] = 1;
    Game g(std::move(s), kingRules());
    // Weighted by people: (4 x 75 + 2 x 0) / 6.
    VCR_CHECK(g.nationRebelPercent(kHuman) == 50);
}

VCR_TEST("king: the Crown's army grows by 1800 at a time") {
    SaveGame s = kingSave();
    auto& raw = s.nations[kHuman].raw;
    raw[0x22] = static_cast<std::uint8_t>(1790 & 0xFF);
    raw[0x23] = static_cast<std::uint8_t>(1790 >> 8);
    s.header[106] = 4; // 4 regulars, 2 dragoons, 1 man-of-war, 1 artillery
    s.header[108] = 2;
    s.header[110] = 1;
    s.header[112] = 1;
    s.header[104] = 0;
    Game g(std::move(s), kingRules());
    g.endTurn();
    const auto notices = g.takeNotices();
    const auto it = std::find_if(notices.begin(), notices.end(),
                                 [](const GameNotice& n) { return n.section == "KINGBUY"; });
    VCR_CHECK(it != notices.end());
    // (4 + 2) / 3 = 2 dragoons is enough, a quarter of 4 regulars is 1
    // artillery and (4 + 2 + 1 + 5) / 10 = 1 man-of-war: regulars.
    VCR_CHECK(it != notices.end() && it->strings == std::vector<std::string>{"Regulars"});
    VCR_CHECK(g.state().header[106] == 5);
    // At Discoverer after 1600 the Crown puts 20 by every turn.
    const auto& after = g.state().nations[kHuman].raw;
    VCR_CHECK((after[0x22] | (after[0x23] << 8)) == 1790 + 20 - 1800);
}

VCR_TEST("king: rebel sentiment rising and falling is told by tens") {
    SaveGame s = kingSave();
    s.colonies[0].population = 4;
    s.colonies[0].rebelDividend = 0;
    s.colonies[0].rebelDivisor = 800;
    s.header[104] = 3; // last told: 30%
    Game g(std::move(s), kingRules());
    g.endTurn();
    const auto notices = g.takeNotices();
    const auto it =
        std::find_if(notices.begin(), notices.end(), [](const GameNotice& n) { return n.section == "REBELDOWN"; });
    VCR_CHECK(it != notices.end());
    VCR_CHECK(it != notices.end() && it->countries == (std::vector<std::pair<int, int>>{{0, kHuman}}));
    VCR_CHECK(g.state().header[104] == 0);
    VCR_CHECK(g.state().header[96] == 0);
}

VCR_TEST("king: the King speaks every so many turns") {
    // After 1600 at Discoverer the player hears him every 15 + 4 = 19
    // turns: the turn that comes is 228.
    SaveGame s = kingSave();
    s.turn = 227;
    s.year = 1653;
    s.season = 1;
    s.nations[kHuman].taxRate = 2;
    Game g(std::move(s), kingRules());
    g.endTurn();
    const auto notices = g.takeNotices();
    const auto it = std::find_if(notices.begin(), notices.end(), [](const GameNotice& n) {
        return n.section.rfind("KING", 0) == 0 && n.section != "KINGBUY";
    });
    VCR_CHECK(it != notices.end());
    VCR_CHECK(g.state().nations[kHuman].taxRate != 2);
    VCR_CHECK(it != notices.end() && it->strings.size() >= 2 && it->strings[1] == "Peter Minuit");
    VCR_CHECK(it != notices.end() && !it->lines.empty() && it->lines[0].section == "DIFFICULTY");
    // Not on the turn after.
    g.endTurn();
    const auto later = g.takeNotices();
    VCR_CHECK(std::none_of(later.begin(), later.end(), [](const GameNotice& n) {
        return n.section.rfind("KING", 0) == 0 && n.section != "KINGBUY";
    }));
}

VCR_TEST("king: mercenaries are offered now and then and land in a colony") {
    SaveGame s = kingSave();
    s.turn = 201; // not a turn of the King's
    s.nations[kHuman].gold = 100000;
    s.colonies[0].population = 3;
    Game g(std::move(s), kingRules());
    GameNotice offer;
    for (int turn = 0; turn < 400 && offer.section.empty(); ++turn) {
        g.endTurn();
        for (const auto& n : g.takeNotices()) {
            if (n.section == "MERCENARIES") {
                offer = n;
            }
        }
    }
    VCR_CHECK(offer.section == "MERCENARIES");
    VCR_CHECK(g.mercenariesWaiting());
    VCR_CHECK(offer.value == Game::kMercenaryQuestion);
    VCR_CHECK(offer.countries.size() == 1 && offer.countries[0] == std::make_pair(0, kHuman));
    // Discoverer: 800 to 1400 a dragoon, a gun twice that.
    const auto& force = offer.strings[1];
    const int dragoons = force[0] - '0';
    const auto comma = force.find(", ");
    const int guns = comma == std::string::npos ? 0 : (force[comma + 2] == 'A' ? 1 : force[comma + 2] - '0');
    VCR_CHECK(dragoons >= 1 && dragoons <= 4 && guns <= 2);
    VCR_CHECK((guns == 0) == (force.find("Artillery") == std::string::npos));
    const int units = dragoons + 2 * guns;
    VCR_CHECK(offer.numbers[0] % (100 * units) == 0);
    VCR_CHECK(offer.numbers[0] / units >= 800 && offer.numbers[0] / units <= 1400);
    const int gold = g.state().nations[kHuman].gold;
    const int before = g.unitCount();
    g.answerMercenaries(true);
    VCR_CHECK(!g.mercenariesWaiting());
    VCR_CHECK(g.state().nations[kHuman].gold == gold - offer.numbers[0]);
    VCR_CHECK(g.unitCount() == before + dragoons + guns);
    for (int i = before; i < g.unitCount(); ++i) {
        const auto& u = g.unit(i);
        VCR_CHECK(u.x == 2 && u.y == 2 && u.nation == kHuman && u.profession == 0x15);
        VCR_CHECK(u.type == (i - before < dragoons ? 4 : 11));
    }
    const auto news = g.takeNotices();
    VCR_CHECK(news.size() == 1 && news[0].section == "MERCS" && news[0].strings[0] == "Fort Orange");
}

VCR_TEST("king: no mercenaries for those who can't pay or refuse") {
    SaveGame s = kingSave();
    s.turn = 201;
    s.nations[kHuman].gold = 700;
    Game g(std::move(s), kingRules());
    for (int turn = 0; turn < 300; ++turn) {
        g.endTurn();
        for (const auto& n : g.takeNotices()) {
            VCR_CHECK(n.section != "MERCENARIES");
        }
    }
}

namespace {

// A treasure of 5000 next to the colony, walked in.
Game treasureGame(SaveGame s, bool galleon) {
    SaveUnit t;
    t.x = 1;
    t.y = 2;
    t.type = unit_type::kTreasure;
    t.nation = kHuman;
    t.profession = 50; // in hundreds
    s.units.push_back(t);
    if (galleon) {
        SaveUnit g;
        g.type = 15;
        g.nation = kHuman;
        s.units.push_back(g);
    }
    s.nations[kHuman].taxRate = 30;
    s.nations[kHuman].gold = 1000;
    return Game(std::move(s), kingRules());
}

} // namespace

VCR_TEST("king: the King offers to carry treasure home for a share") {
    Game g = treasureGame(kingSave(), false);
    VCR_CHECK(g.move(0, {1, 0}) == MoveResult::Moved);
    auto notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices[0].section == "KINGGALLEON2");
    VCR_CHECK(g.treasureWaiting());
    VCR_CHECK(!notices.empty() && notices[0].value == Game::kTreasureQuestion);
    g.answerTreasure(true);
    // Discoverer: twice the tax (60%) beats 50%.
    VCR_CHECK(g.unitCount() == 0);
    VCR_CHECK(g.state().nations[kHuman].gold == 1000 + 2000);
    notices = g.takeNotices();
    VCR_CHECK(notices.size() == 1 && notices[0].section == "LOOTCASH");
    VCR_CHECK(!notices.empty() && notices[0].numbers == std::vector<int>({5000, 60, 2000}));
    const auto& raw = g.state().nations[kHuman].raw;
    VCR_CHECK((raw[0x22] | (raw[0x23] << 8)) == 3000);
}

VCR_TEST("king: treasure stays when refused, or when the player has a galleon") {
    {
        Game g = treasureGame(kingSave(), false);
        g.move(0, {1, 0});
        g.takeNotices();
        g.answerTreasure(false);
        VCR_CHECK(g.unitCount() == 1 && g.state().nations[kHuman].gold == 1000);
    }
    {
        Game g = treasureGame(kingSave(), true);
        g.move(0, {1, 0});
        VCR_CHECK(g.takeNotices().empty() && !g.treasureWaiting());
    }
}

VCR_TEST("king: with Cortes the Crown takes only the tax; after independence it is sold") {
    {
        SaveGame s = kingSave();
        s.nations[kHuman].raw[8] |= 0x04; // Cortes (father 10)
        Game g = treasureGame(std::move(s), true);
        g.move(0, {1, 0});
        const auto notices = g.takeNotices();
        VCR_CHECK(notices.size() == 1 && notices[0].section == "KINGGALLEON3");
        g.answerTreasure(true);
        VCR_CHECK(g.state().nations[kHuman].gold == 1000 + 3500);
    }
    {
        SaveGame s = kingSave();
        s.header[18] |= 0x01;
        Game g = treasureGame(std::move(s), false);
        g.move(0, {1, 0});
        const auto notices = g.takeNotices();
        VCR_CHECK(notices.size() == 1 && notices[0].section == "CASHTREASURE");
        VCR_CHECK(g.unitCount() == 0 && g.state().nations[kHuman].gold == 6000);
    }
}

#include "viceroy/data/reader.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/render/dialog.hpp"
#include "viceroy/ui/play.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {

// For comparing with the original by eye: VICEROY_DUMP_KING=file writes
// the screen and its colours.
void dumpScreen(const Screen& screen, const Art& art, const char* env) {
    const char* file = std::getenv(env);
    if (file == nullptr) {
        return;
    }
    std::ofstream out(file, std::ios::binary);
    for (const auto p : screen.pixels()) {
        const Rgb c = screen.colour(art.palette, p);
        out.put(static_cast<char>(c.r)).put(static_cast<char>(c.g)).put(static_cast<char>(c.b));
    }
}

} // namespace

VCR_TEST("original files: the King raises the tax and the player may hold a party") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    const Bytes bytes(std::istreambuf_iterator<char>(in), {});
    SaveGame save = readSaveGame(bytes);
    const int human = save.humanNation;
    save.nations[static_cast<std::size_t>(human)].taxRate = 2;
    Game game(std::move(save), art.rules);
    GameNotice notice;
    notice.section = "KINGSTAMPACT";
    notice.strings = {"", game.state().players[static_cast<std::size_t>(human)].name,
                      game.state().players[static_cast<std::size_t>(human)].country};
    notice.lines = {{0, true, "DIFFICULTY", game.state().difficulty}};
    game.changeTax(human, 6, notice);
    VCR_CHECK(game.taxRaiseWaiting());
    PlayScreen play(std::move(game), {});
    play.setArt(&art);
    play.handle(Command::EndTurn); // the news comes up, the King first
    VCR_CHECK(play.dialog().has_value());
    if (!play.dialog()) {
        return;
    }
    const Dialog& d = *play.dialog();
    VCR_CHECK(d.portrait == kKingPortrait);
    VCR_CHECK(d.x == 105 && d.portraitX == 20 && d.portraitY == 18);
    VCR_CHECK(d.overlay == kKingArm && d.overlayFrame == 0);
    VCR_CHECK(art.portraits[kKingArm].sprites.size() == 8);
    VCR_CHECK(d.options.size() == 2);
    VCR_CHECK(!d.lines.empty() && d.lines[0].rfind("\"Discoverer ", 0) == 0);
    VCR_CHECK(makeDialog(art, "MERCENARIES", {"France", "3 Dragoons"}, {3300}).options.size() == 2);
    VCR_CHECK(makeDialog(art, "KINGGALLEON2", {"Discoverer", "Peter Minuit", "the Netherlands"}).options.size() == 2);
    VCR_CHECK(makeDialog(art, "KINGGALLEON3", {"Discoverer", "Peter Minuit", "the Netherlands"}, {30}).options.size() == 2);
    Screen screen;
    play.draw(screen, art);
    dumpScreen(screen, art, "VICEROY_DUMP_KING");
    // After a few seconds his arm rises and stays up.
    play.tick(4000);
    VCR_CHECK(play.dialog()->overlayFrame == 0);
    play.tick(300);
    VCR_CHECK(play.dialog()->overlayFrame == 2);
    play.tick(5000);
    VCR_CHECK(play.dialog()->overlayFrame == 7);
    play.draw(screen, art);
    dumpScreen(screen, art, "VICEROY_DUMP_KING_ARM");
    // Hold the party: the rise is undone and the news follows at once.
    play.handle(Command::MoveS);
    play.handle(Command::Confirm);
    VCR_CHECK(play.game().state().nations[static_cast<std::size_t>(human)].taxRate == 2);
    VCR_CHECK(play.dialog().has_value());
    if (play.dialog()) {
        VCR_CHECK(!play.dialog()->lines.empty() && play.dialog()->lines[0].find("Party}!") != std::string::npos);
        play.draw(screen, art);
        dumpScreen(screen, art, "VICEROY_DUMP_PARTY");
    }
}
