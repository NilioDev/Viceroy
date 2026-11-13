// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/savegame.hpp"
#include "viceroy/game/natives.hpp"
#include "viceroy/game_files.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace viceroy;

VCR_TEST("natives: the original's random numbers") {
    // The C library the original was built with: seed 1 starts 41, 18467.
    OriginalRandom r(1);
    VCR_CHECK(r.next() == 41);
    VCR_CHECK(r.next() == 18467);
    OriginalRandom s(1);
    VCR_CHECK(s.between(1, 1000) == 2);
}

VCR_TEST("natives: tension gives the greeting") {
    SaveGame save;
    save.tribesAndMore.assign(8 * 78 + 727, 0);
    SaveSettlement village;
    village.nation = 4 + 6; // Sioux
    save.settlements.push_back(village);
    const auto setTension = [&](int value) {
        save.tribesAndMore[6 * 78 + 70 + 2 * 3] = static_cast<std::uint8_t>(value);
    };
    setTension(0);
    VCR_CHECK(villageGreeting(save, 0, 3) == "VILLAGEHAPPY");
    setTension(25);
    VCR_CHECK(villageGreeting(save, 0, 3) == "VILLAGEMEDIUM");
    setTension(50);
    VCR_CHECK(villageGreeting(save, 0, 3) == "VILLAGEBAD");
    setTension(75);
    VCR_CHECK(villageGreeting(save, 0, 3) == "VILLAGEWAR");
    VCR_CHECK(tensionLevel(74) == 2 && tensionLevel(24) == 0);
    save.settlements[0].nation = 4 + 2; // the Arawaks
    VCR_CHECK(villageGreeting(save, 0, 3) == "VILLAGESAVAGE");
}

VCR_TEST("original files: each village teaches what the original teaches (measured)") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    const auto file = findFileNoCase(dir, "COLONY00.SAV");
    if (!file) {
        VCR_SKIP("no game files in gamedata/");
    }
    std::ifstream in(*file, std::ios::binary);
    const Bytes bytes(std::istreambuf_iterator<char>(in), {});
    const SaveGame save = readSaveGame(bytes);
    // Settlement, and the skill the original offered there.
    constexpr int kFarmer = 0, kCotton = 3, kFurTrapper = 4, kOreMiner = 6, kFisherman = 8, kScout = 22;
    const std::pair<int, int> seen[] = {{20, kFurTrapper}, {24, kFurTrapper}, {25, kFurTrapper},
                                        {26, kFurTrapper}, {2, kScout},       {4, kFarmer},
                                        {7, kScout},       {11, kFisherman},  {12, kOreMiner},
                                        {13, kCotton},     {14, kCotton}};
    for (const auto& [settlement, skill] : seen) {
        VCR_CHECK(villageSkill(save, settlement) == skill);
    }
}

#include "viceroy/game/game.hpp"
#include "viceroy/render/dialog.hpp"
#include "viceroy/render/game_view.hpp"
#include "viceroy/ui/play.hpp"

VCR_TEST("original files: a colonist lives among the natives and learns their skill") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    SaveGame save = readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {}));
    constexpr int kDutch = 3, kIroquois = 3, kVillage = 4, kFarmer = 0, kFreeColonist = 0x1C, kCriminal = 0x1A;
    // The Dutch have met the Iroquois, who are calm.
    save.nations[kDutch].raw[52 + 4 + kIroquois] = 0x60;
    save.tribesAndMore[kIroquois * 78 + 70 + 2 * kDutch] = 0;
    save.tribesAndMore[kIroquois * 78 + 71 + 2 * kDutch] = 0;
    const auto& village = save.settlements[kVillage];
    VCR_CHECK(tribeOf(village) == kIroquois);
    // A free colonist on a land square next to the village.
    int colonist = -1;
    for (std::size_t i = 0; i < save.units.size(); ++i) {
        if (save.units[i].nation == kDutch && save.units[i].type == 0) {
            colonist = static_cast<int>(i);
            break;
        }
    }
    VCR_CHECK(colonist >= 0);
    struct Step { int dx, dy; Command command; };
    const Step steps[] = {{-1, 0, Command::MoveE}, {1, 0, Command::MoveW}, {0, -1, Command::MoveS}, {0, 1, Command::MoveN}};
    Command go = Command::MoveE;
    for (const auto& s : steps) {
        if (save.map.at(village.x + s.dx, village.y + s.dy).isLand()) {
            save.units[static_cast<std::size_t>(colonist)].x = village.x + s.dx;
            save.units[static_cast<std::size_t>(colonist)].y = village.y + s.dy;
            go = s.command;
            break;
        }
    }
    auto& u = save.units[static_cast<std::size_t>(colonist)];
    u.profession = kCriminal;
    u.movesUsed = 0;
    u.orders = SaveUnit::kNoOrders;

    {
        // A criminal is refused.
        Game game(save, art.rules);
        VCR_CHECK(game.canLiveAmongNatives(colonist, kVillage));
        VCR_CHECK(game.liveAmongNatives(colonist, kVillage).section == "LEARNCRIMINAL");
        VCR_CHECK(game.movesLeft(colonist) <= 0);
    }
    u.profession = kFarmer + 1; // an expert already
    VCR_CHECK(Game(save, art.rules).liveAmongNatives(colonist, kVillage).section == "LEARNMASTER");
    // Without contact only Cancel Action is offered.
    {
        SaveGame unmet = save;
        unmet.nations[kDutch].raw[52 + 4 + kIroquois] = 0;
        VCR_CHECK(!Game(unmet, art.rules).canLiveAmongNatives(colonist, kVillage));
    }

    u.profession = kFreeColonist;
    PlayScreen play(Game(save, art.rules), {});
    play.setArt(&art);
    play.game().setActiveUnit(colonist);
    play.handle(go);
    VCR_CHECK(play.dialog().has_value());
    VCR_CHECK(play.dialog()->options.size() == 2); // Live Among The Natives, Cancel Action
    const auto dump = [&](const char* name) {
        if (const char* to = std::getenv("VICEROY_DUMP_VILLAGE")) {
            Screen screen;
            play.draw(screen, art);
            std::ofstream out(std::string(to) + name, std::ios::binary);
            out.write(reinterpret_cast<const char*>(screen.pixels().data()),
                      static_cast<std::streamsize>(screen.pixels().size()));
            for (int i = 0; i < 256; ++i) {
                const auto c = screen.colour(art.palette, static_cast<std::uint8_t>(i));
                out.put(static_cast<char>(c.r)).put(static_cast<char>(c.g)).put(static_cast<char>(c.b));
            }
        }
    };
    dump("_greeting");
    play.handle(Command::Confirm);
    VCR_CHECK(play.dialog().has_value());
    VCR_CHECK(play.dialog()->portrait == kNativePortraits + kIroquois);
    VCR_CHECK(play.dialog()->options.size() == 2); // the offer
    dump("_offer");
    play.handle(Command::Confirm);
    VCR_CHECK(play.game().unit(colonist).profession == kFarmer);
    VCR_CHECK((play.game().state().settlements[kVillage].raw[3] & kSettlementTaught) != 0);
    VCR_CHECK(play.dialog().has_value()); // LEARNDONE
    play.handle(Command::Confirm);
    VCR_CHECK(!play.dialog().has_value());
    // The village has taught now and is not a capital.
    if ((play.game().state().settlements[kVillage].raw[3] & kSettlementCapital) == 0) {
        Game again(play.game().state(), art.rules);
        auto& v = again.state().units[static_cast<std::size_t>(colonist)];
        v.profession = kFreeColonist;
        VCR_CHECK(again.liveAmongNatives(colonist, kVillage).section == "LEARNALREADY");
    }
}

VCR_TEST("original files: the goods each chief asks for (measured)") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    const auto file = findFileNoCase(dir, "COLONY00.SAV");
    if (!file) {
        VCR_SKIP("no game files in gamedata/");
    }
    std::ifstream in(*file, std::ios::binary);
    const SaveGame save = readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {}));
    // What the chiefs said in the original, most wanted first.
    constexpr int kTobacco = 2, kHorses = 8, kRum = 9, kCigars = 10, kCloth = 11, kCoats = 12, kMuskets = 15;
    const std::pair<int, std::array<int, 3>> seen[] = {
        {2, {kTobacco, kCloth, kCoats}}, {4, {kTobacco, kCoats, kCigars}},  {15, {kRum, kCoats, kCloth}},
        {16, {kTobacco, kRum, kCoats}},  {20, {kTobacco, kCloth, kMuskets}}, {21, {kCloth, kTobacco, kHorses}}};
    for (const auto& [settlement, wants] : seen) {
        VCR_CHECK(villageWants(save, settlement) == wants);
    }
}

VCR_TEST("original files: a scout speaks with the chief") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    SaveGame save = readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {}));
    constexpr int kDutch = 3, kIroquois = 3, kCherokee = 4, kVillage = 4, kCherokeeVillage = 13;
    save.nations[kDutch].raw[52 + 4 + kIroquois] = 0x60;
    save.nations[kDutch].raw[52 + 4 + kCherokee] = 0x60;
    save.tribesAndMore[kIroquois * 78 + 70 + 2 * kDutch] = 0;
    save.tribesAndMore[kIroquois * 78 + 71 + 2 * kDutch] = 0;
    const auto& village = save.settlements[kVillage];
    int scout = -1;
    for (std::size_t i = 0; i < save.units.size(); ++i) {
        if (save.units[i].nation == kDutch && save.units[i].type == 0) {
            scout = static_cast<int>(i);
            break;
        }
    }
    VCR_CHECK(scout >= 0);
    auto& u = save.units[static_cast<std::size_t>(scout)];
    u.type = 5;
    u.profession = 0x1C;
    u.movesUsed = 0;
    u.orders = SaveUnit::kNoOrders;
    u.x = village.x - 1;
    u.y = village.y;
    VCR_CHECK(save.map.at(u.x, u.y).isLand());

    {
        // The Cherokee are furious with the Dutch: the scout breaks a taboo.
        Game game(save, art.rules);
        VCR_CHECK(tribeTension(save, kCherokee, kDutch) >= 75);
        const auto units = game.state().units.size();
        const auto talk = game.speakWithChief(scout, kCherokeeVillage);
        VCR_CHECK(talk.answer == Game::ChiefAnswer::Kill && !talk.welcome);
        VCR_CHECK(game.state().units.size() == units - 1);
    }
    {
        // A chief already spoken with only welcomes.
        SaveGame visited = save;
        visited.settlements[kVillage].raw[3] |= kSettlementVisited;
        Game game(visited, art.rules);
        const auto talk = game.speakWithChief(scout, kVillage);
        VCR_CHECK(talk.welcome && talk.answer == Game::ChiefAnswer::Bored);
        VCR_CHECK(talk.skill == 0); // Expert Farmers
        VCR_CHECK(game.movesLeft(scout) <= 0);
    }

    PlayScreen play(Game(save, art.rules), {});
    play.setArt(&art);
    const int goldBefore = play.game().state().nations[kDutch].gold;
    play.game().setActiveUnit(scout);
    play.handle(Command::MoveE);
    VCR_CHECK(play.dialog().has_value());
    VCR_CHECK(play.dialog()->options.size() == 2); // Ask to Speak With Chief, Cancel Action
    play.handle(Command::Confirm);
    VCR_CHECK(play.dialog().has_value()); // the chief's greeting
    VCR_CHECK(play.dialog()->portrait == kNativePortraits + kIroquois);
    play.handle(Command::Confirm);
    VCR_CHECK(play.dialog().has_value()); // what he says next
    play.handle(Command::Confirm);
    VCR_CHECK(!play.dialog().has_value());
    const auto& after = play.game().state();
    const auto& s = play.game().unit(scout);
    const bool gift = after.nations[kDutch].gold > goldBefore;
    const bool guides = s.profession == 22;
    VCR_CHECK(!(gift && guides));
    VCR_CHECK((after.settlements[kVillage].raw[3] & kSettlementVisited) != 0 || (!gift && !guides));
}

VCR_TEST("original files: a missionary founds a mission (measured)") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    SaveGame save = readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {}));
    constexpr int kDutch = 3, kApache = 5, kVillage = 15;
    save.nations[kDutch].raw[52 + 4 + kApache] = 0x60;
    const auto& village = save.settlements[kVillage];
    int missionary = -1;
    for (std::size_t i = 0; i < save.units.size(); ++i) {
        if (save.units[i].nation == kDutch && save.units[i].type == 0) {
            missionary = static_cast<int>(i);
            break;
        }
    }
    auto& u = save.units[static_cast<std::size_t>(missionary)];
    u.type = 3;
    u.profession = 0x1C;
    u.movesUsed = 0;
    u.orders = SaveUnit::kNoOrders;
    u.x = village.x - 1;
    u.y = village.y;
    VCR_CHECK(tribeTension(save, kApache, kDutch) == 51);

    PlayScreen play(Game(save, art.rules), {});
    play.setArt(&art);
    const auto units = save.units.size();
    play.game().setActiveUnit(missionary);
    play.handle(Command::MoveE);
    VCR_CHECK(play.dialog().has_value());
    VCR_CHECK(play.dialog()->options.size() == 2); // Establish Mission, Cancel Action
    play.handle(Command::Confirm);
    // Seen in the original: "Church of Curacao mission founded in Autumn,
    // 1653. Apache are offended.", and the tension went from 51 to 41.
    VCR_CHECK(play.dialog().has_value());
    VCR_CHECK(play.dialog()->y == 110);
    const auto& after = play.game().state();
    VCR_CHECK(missionOf(after.settlements[kVillage]) == kDutch);
    VCR_CHECK(tribeTension(after, kApache, kDutch) == 41);
    VCR_CHECK(after.units.size() == units - 1);
    std::string text;
    for (const auto& line : play.dialog()->lines) {
        text += line + " ";
    }
    VCR_CHECK(text.find("offended") != std::string::npos);
    VCR_CHECK(text.find("Church of " + after.colonies[static_cast<std::size_t>(nearestColony(after, village.x, village.y, kDutch))].name) !=
              std::string::npos);
    VCR_CHECK(after.colonies[static_cast<std::size_t>(nearestColony(after, village.x, village.y, kDutch))].name.rfind("Cura", 0) == 0);
    play.handle(Command::Confirm);
    VCR_CHECK(!play.dialog().has_value());
}

#include "viceroy/game/europe.hpp"

VCR_TEST("original files: trading with a village (measured)") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    SaveGame save = readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {}));
    constexpr int kDutch = 3, kIroquois = 3, kVillage = 4, kCotton = 3, kFurs = 4, kCigars = 10, kCloth = 11;
    constexpr int kTobacco = 2, kCoats = 12, kTradeGoods = 13;
    save.nations[kDutch].raw[52 + 4 + kIroquois] = 0x60;
    const auto& village = save.settlements[kVillage];
    int wagon = -1;
    for (std::size_t i = 0; i < save.units.size(); ++i) {
        if (save.units[i].nation == kDutch && save.units[i].type == 0) {
            wagon = static_cast<int>(i);
            break;
        }
    }
    auto& u = save.units[static_cast<std::size_t>(wagon)];
    u.type = 12;
    u.movesUsed = 0;
    u.orders = SaveUnit::kNoOrders;
    u.x = village.x - 1;
    u.y = village.y;
    u.holds = 1;
    setHold(u, 0, kCigars, 100);

    {
        // Seen: 434, 511 for a hundred cigars and 283 for sixty; the
        // offer moves with a roll of 1 to 5, 77 a step before halving.
        Game game(save, art.rules);
        const auto sale = game.offerToVillage(wagon, kVillage, 0);
        VCR_CHECK(sale.refusal.empty());
        const int luck = (2 * sale.price - 792 + 76) / 77;
        VCR_CHECK(luck >= 1 && luck <= 5 && sale.price == (792 + 77 * luck) / 2);
        VCR_CHECK(sale.ask == sale.price + 148 && sale.quality == 3);
        VCR_CHECK(game.movesLeft(wagon) <= 0);
    }
    {
        // Cloth: "We have enough Cloth ... We are in need of Tobacco and
        // Coats. Even Cigars would be of some use."
        SaveGame cloth = save;
        setHold(cloth.units[static_cast<std::size_t>(wagon)], 0, kCloth, 100);
        Game game(cloth, art.rules);
        const auto sale = game.offerToVillage(wagon, kVillage, 0);
        VCR_CHECK(sale.refusal == "BADCARGO");
        VCR_CHECK((sale.wants == std::array<int, 3>{kTobacco, kCoats, kCigars}));
    }

    setHold(u, 0, kCigars, 60);
    PlayScreen play(Game(save, art.rules), {});
    play.setArt(&art);
    const int goldBefore = save.nations[kDutch].gold;
    play.game().setActiveUnit(wagon);
    play.handle(Command::MoveE);
    VCR_CHECK(play.dialog().has_value() && play.dialog()->options.size() == 2); // Trade With Village, Cancel
    play.handle(Command::Confirm);
    VCR_CHECK(play.dialog().has_value() && play.dialog()->options.size() == 4); // TRADE0
    play.handle(Command::Confirm); // "We gratefully accept"
    const auto& sold = play.game().state();
    VCR_CHECK(sold.nations[kDutch].gold > goldBefore);
    VCR_CHECK(play.game().unit(wagon).holds == 0);
    // Seen next: "We are in need of Tobacco and Coats ... Even Trade Goods",
    // as cigars were just sold.
    VCR_CHECK((topWants(villageMarket(sold, kVillage).wants, sold.settlements[kVillage]) ==
               std::array<int, 3>{kTobacco, kCoats, kTradeGoods}));
    VCR_CHECK(play.dialog().has_value()); // BRING
    play.handle(Command::Confirm);
    // "We have Cotton, Furs, and Cloth available": four options.
    VCR_CHECK((play.game().villageGoodsForSale(kVillage) == std::vector<int>{kCotton, kFurs, kCloth}));
    VCR_CHECK(play.dialog().has_value() && play.dialog()->options.size() == 4);
    play.handle(Command::Confirm); // Cotton
    VCR_CHECK(play.dialog().has_value() && play.dialog()->options.size() == 3); // BUY0
    play.handle(Command::Confirm); // pay
    VCR_CHECK(!play.dialog().has_value());
    const auto& w = play.game().unit(wagon);
    VCR_CHECK(w.holds == 1 && holdGood(w, 0) == kCotton && holdAmount(w, 0) == 60);
}
