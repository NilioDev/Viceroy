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
    VCR_CHECK(play.dialog()->portrait == kNativePortraits + 4 * kIroquois);
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
    // Seen: Ask to Speak With Chief, Demand Tribute, Attack Village, Cancel Action.
    VCR_CHECK(play.dialog()->options.size() == 4);
    play.handle(Command::Confirm);
    VCR_CHECK(play.dialog().has_value()); // the chief's greeting
    VCR_CHECK(play.dialog()->portrait == kNativePortraits + 4 * kIroquois);
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
    VCR_CHECK(play.dialog()->options.size() == 3); // Establish Mission, Incite Indians, Cancel Action
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

VCR_TEST("original files: demanding tribute (measured)") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    SaveGame save = readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {}));
    constexpr int kDutch = 3, kIroquois = 3, kVillage = 4, kFood = 0;
    constexpr std::size_t kExtra = 8 * 78;

    {
        // The tribes' count in the save matches its braves: 8 for a brave,
        // 16 for an armed one, by region.
        Game game(save, art.rules);
        game.countForces();
        const auto& block = game.state().tribesAndMore;
        for (std::size_t at = kExtra + 565; at < kExtra + 589 + 128; ++at) {
            if (at < kExtra + 573 || at >= kExtra + 589) {
                VCR_CHECK(block[at] == save.tribesAndMore[at]);
            }
        }
    }

    // As measured: the Dutch strong in the region, the Iroquois with no
    // braves counted.
    save.nations[kDutch].raw[52 + 4 + kIroquois] = 0x60;
    save.tribesAndMore[kExtra + 300 + 16 * kDutch + 1] = 200;
    save.tribesAndMore[kExtra + 44 + 2 * kDutch] = 0;
    save.tribesAndMore[kExtra + 44 + 2 * kDutch + 1] = 0;
    save.tribesAndMore[kExtra + 565 + kIroquois] = 0;
    save.tribesAndMore[kExtra + 589 + 16 * kIroquois + 1] = 0;
    const auto& village = save.settlements[kVillage];
    int scout = -1;
    for (std::size_t i = 0; i < save.units.size(); ++i) {
        if (save.units[i].nation == kDutch && save.units[i].type == 0) {
            scout = static_cast<int>(i);
            break;
        }
    }
    auto& u = save.units[static_cast<std::size_t>(scout)];
    u.type = 5;
    u.profession = 0x1C;
    u.movesUsed = 0;
    u.orders = SaveUnit::kNoOrders;
    u.x = village.x - 1;
    u.y = village.y;

    PlayScreen play(Game(save, art.rules), {});
    play.setArt(&art);
    play.game().setActiveUnit(scout);
    play.handle(Command::MoveE);
    // Ask to Speak With Chief, Demand Tribute, Attack Village, Cancel Action.
    VCR_CHECK(play.dialog().has_value() && play.dialog()->options.size() == 4);
    play.handle(Command::Down);
    play.handle(Command::Confirm);
    // Seen: "Great Discoverer, ... the humble and peaceloving Iroquois shall
    // gladly deliver 30 Food to Roanoke ..."
    VCR_CHECK(play.dialog().has_value());
    std::string text;
    for (const auto& line : play.dialog()->lines) {
        text += line + " ";
    }
    VCR_CHECK(text.find("Great Discoverer") != std::string::npos);
    VCR_CHECK(play.dialog()->portrait == kNativePortraits + 4 * kIroquois); // calm
    VCR_CHECK(text.find("{30 Food} to {Roanoke}") != std::string::npos);
    const auto& after = play.game().state();
    int roanoke = -1;
    for (std::size_t c = 0; c < after.colonies.size(); ++c) {
        if (after.colonies[c].name == "Roanoke") {
            roanoke = static_cast<int>(c);
        }
    }
    VCR_CHECK(roanoke >= 0);
    VCR_CHECK(after.colonies[static_cast<std::size_t>(roanoke)].stock[kFood] ==
              save.colonies[static_cast<std::size_t>(roanoke)].stock[kFood] + 30);
    VCR_CHECK((after.settlements[kVillage].raw[3] & kSettlementTribute) != 0);
    VCR_CHECK(tribeTension(after, kIroquois, kDutch) == tribeTension(save, kIroquois, kDutch) + 2 * (save.difficulty + 1));
    VCR_CHECK(play.game().movesLeft(scout) <= 0);
    play.handle(Command::Confirm);
    VCR_CHECK(!play.dialog().has_value());

    // A second demand finds the village poor.
    Game again(after, art.rules);
    again.state().units[static_cast<std::size_t>(scout)].movesUsed = 0;
    VCR_CHECK(again.demandTribute(scout, kVillage).reply == Game::Tribute::Reply::Poor);

    // Seen with the tension at 80: "We laugh at your puny threats ...
    // Iroquois warriors ...", told by the furious chief.
    setTribeTension(save, kIroquois, kDutch, 80);
    PlayScreen angry(Game(save, art.rules), {});
    angry.setArt(&art);
    angry.game().setActiveUnit(scout);
    angry.handle(Command::MoveE);
    angry.handle(Command::Down);
    angry.handle(Command::Confirm);
    VCR_CHECK(angry.dialog().has_value() && angry.dialog()->portrait == kNativePortraits + 4 * kIroquois + 3);
    text.clear();
    for (const auto& line : angry.dialog()->lines) {
        text += line + " ";
    }
    VCR_CHECK(text.find("We laugh at your puny threats") != std::string::npos);
    VCR_CHECK(tribeTension(angry.game().state(), kIroquois, kDutch) == 80 + save.difficulty + 1);
}

VCR_TEST("original files: attacking a village (measured)") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    const SaveGame original = readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {}));
    // As measured: the Dutch unit 47 beside the Apache camp at (4, 18),
    // which has no brave on its square; brave 13 lives there.
    constexpr int kDutch = 3, kApache = 5, kCamp = 15, kUnit = 47;
    constexpr int kArtilleryType = 11, kScoutType = 5;
    const auto scenario = [&](int type, int population, int mission, int turn) {
        SaveGame save = original;
        save.turn = turn;
        auto& u = save.units[kUnit];
        u.type = type;
        u.profession = 0x1C;
        u.x = 3;
        u.y = 18;
        u.movesUsed = 0;
        u.orders = SaveUnit::kNoOrders;
        save.nations[kDutch].raw[52 + 4 + kApache] = 0x60;
        save.settlements[kCamp].population = population;
        save.settlements[kCamp].raw[4] = static_cast<std::uint8_t>(population);
        save.settlements[kCamp].raw[5] = static_cast<std::uint8_t>(mission);
        return save;
    };

    {
        // Seen: Attack Village first for artillery; Combat Analysis with
        // "Artillery 7, Attack Bonus +50%" against "Braves 1, Camp +50%".
        PlayScreen play(Game(scenario(kArtilleryType, 1, 3, 0), art.rules), {});
        play.setArt(&art);
        play.game().setActiveUnit(kUnit);
        play.handle(Command::MoveE);
        VCR_CHECK(play.dialog().has_value() && play.dialog()->options.size() == 3);
        const auto a = play.game().analyseVillageAttack(kUnit, kCamp);
        VCR_CHECK(a.attacker.base == 7 && a.attacker.bonuses.size() == 1 && a.attacker.bonuses[0].percent == 50);
        VCR_CHECK(a.defender.unit == -1 && a.defender.braveType == 19 && a.defender.base == 1);
        VCR_CHECK(a.defender.bonuses.size() == 1 && a.defender.bonuses[0].percent == 50 &&
                  a.defender.bonuses[0].settlementPicture == kCamp);
        // On the easiest level: (7 x 8 x 1.5 + 4) x 2 against 1 x 8 x 1.5,
        // halved for a brave against a soldier's weapons.
        const auto s = play.game().villageStrengths(kUnit, kCamp);
        VCR_CHECK(s.attack == 176 && s.defense == 6);
    }

    bool sawWin = false, sawBurn = false, sawLoss = false;
    for (int turn = 0; turn < 40 && !(sawWin && sawBurn && sawLoss); ++turn) {
        {
            // A camp of three with an expert Dutch mission: war, tension 10
            // up on the easiest level, the alarm's high byte one up; a win costs it a person and
            // may bring converts to the attacker's square.
            Game game(scenario(kArtilleryType, 3, 0x13, turn), art.rules);
            const int tensionBefore = tribeTension(game.state(), kApache, kDutch);
            game.attackVillageWar(kUnit, kCamp);
            VCR_CHECK((game.state().nations[kDutch].raw[52 + 4 + kApache] & 0x04) != 0);
            VCR_CHECK((game.state().tribesAndMore[kApache * 78 + 58 + kDutch] & 0x04) != 0);
            VCR_CHECK(tribeTension(game.state(), kApache, kDutch) == tensionBefore + 10);
            VCR_CHECK(game.state().settlements[kCamp].raw[17] == 1);
            const int units = game.unitCount();
            if (game.attackVillage(kUnit, kCamp) == CombatOutcome::AttackerWon && !sawWin) {
                sawWin = true;
                VCR_CHECK(game.state().settlements[kCamp].population == 2);
                VCR_CHECK(game.unit(kUnit).movesUsed == game.movementAllowance(kUnit));
                const auto notices = game.takeNotices();
                if (game.unitCount() == units + 1) {
                    const auto& convert = game.unit(units);
                    VCR_CHECK(convert.profession == 0x1B && convert.x == 3 && convert.y == 18 && convert.nation == kDutch);
                    VCR_CHECK(!notices.empty() && notices.front().section == "INDIANSLAVES");
                }
            }
        }
        {
            // The last of a camp: it burns, its brave with it; the Dutch
            // missionary and the treasure stand where it was. With Cortes
            // the camp always has some: 200 to 400, half as much again (the
            // original found 300).
            Game game(scenario(kArtilleryType, 1, 3, turn), art.rules);
            const auto count = game.state().settlements.size();
            const int burned = game.state().nations[kDutch].raw[24];
            if (game.attackVillage(kUnit, kCamp) == CombatOutcome::AttackerWon && !sawBurn) {
                sawBurn = true;
                const auto& st = game.state();
                VCR_CHECK(st.settlements.size() == count - 1);
                VCR_CHECK(!game.settlementAt(4, 18).has_value());
                VCR_CHECK(st.nations[kDutch].raw[24] == burned + 1);
                VCR_CHECK(st.tribesAndMore[8 * 78 + 581 + kApache] == original.tribesAndMore[8 * 78 + 581 + kApache] - 1);
                bool brave = false, missionary = false;
                int treasure = 0;
                for (const auto& u : st.units) {
                    brave |= u.nation == 4 + kApache && u.x == 3 && u.y == 18 && u.type == 19;
                    missionary |= u.type == 3 && u.x == 4 && u.y == 18 && u.nation == kDutch && u.profession == 0x1C;
                    if (u.type == 10 && u.x == 4 && u.y == 18) {
                        treasure = u.profession * 100;
                    }
                }
                VCR_CHECK(!brave && missionary);
                const auto notices = game.takeNotices();
                VCR_CHECK(!notices.empty());
                VCR_CHECK(treasure == 300 || treasure == 400 || treasure == 600);
                VCR_CHECK(notices.back().section == "LOOT" && notices.back().numbers.front() == treasure);
                VCR_CHECK(notices.back().value == 0); // "Camp"
            }
        }
        {
            // Measured: a scout beaten by the camp is gone without a word.
            Game game(scenario(kScoutType, 3, 0xFF, turn), art.rules);
            const int units = game.unitCount();
            if (game.attackVillage(kUnit, kCamp) == CombatOutcome::DefenderWon && !sawLoss) {
                sawLoss = true;
                VCR_CHECK(game.unitCount() == units - 1);
                VCR_CHECK(game.takeNotices().empty());
            }
        }
    }
    VCR_CHECK(sawWin && sawBurn && sawLoss);
}

VCR_TEST("original files: first contact with a tribe (measured)") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    const SaveGame original = readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {}));
    // As measured: the Dutch have not met the Iroquois; their unit 47 steps
    // from (16, 14) next to the Iroquois village at (18, 14).
    constexpr int kDutch = 3, kIroquois = 3, kUnit = 47;
    constexpr std::size_t kRelation = 52 + 4 + kIroquois;
    const std::size_t tribe = static_cast<std::size_t>(kIroquois) * 78;
    const auto start = [&]() {
        SaveGame save = original;
        auto& u = save.units[kUnit];
        u.x = 16;
        u.y = 14;
        u.movesUsed = 0;
        u.orders = SaveUnit::kNoOrders;
        return save;
    };
    const auto lines = [](const Dialog& d) {
        std::string all;
        for (const auto& l : d.lines) {
            all += l + " ";
        }
        return all;
    };
    for (const bool accept : {true, false}) {
        PlayScreen play(Game(start(), art.rules), {});
        play.setArt(&art);
        play.game().setActiveUnit(kUnit);
        VCR_CHECK(play.game().state().nations[kDutch].raw[kRelation] == 0);
        play.handle(Command::MoveE);
        // "The Iroquois tribe welcomes you. We are a glorious nation of 11
        // Villages." Yes or No.
        VCR_CHECK(play.dialog().has_value() && play.dialog()->options.size() == 2);
        VCR_CHECK(lines(*play.dialog()).find("Iroquois") != std::string::npos);
        VCR_CHECK(lines(*play.dialog()).find("11 Villages") != std::string::npos);
        const auto& met = play.game().state();
        VCR_CHECK(met.nations[kDutch].raw[kRelation] == 0x20);
        VCR_CHECK(met.tribesAndMore[tribe + 58 + kDutch] == 0x20);
        VCR_CHECK(met.tribesAndMore[tribe + 46 + 2 * kDutch] == 2);
        if (!accept) {
            play.handle(Command::Down);
        }
        play.handle(Command::Confirm);
        const auto& after = play.game().state();
        if (accept) {
            // Peace, then the invitation to visit and trade.
            VCR_CHECK(after.nations[kDutch].raw[kRelation] == 0x60);
            VCR_CHECK(after.tribesAndMore[tribe + 58 + kDutch] == 0x60);
            VCR_CHECK(tribeTension(after, kIroquois, kDutch) == 2);
            VCR_CHECK(play.dialog().has_value() && lines(*play.dialog()).find("brothers") != std::string::npos);
            VCR_CHECK(lines(*play.dialog()).find("Dutch") != std::string::npos);
            play.handle(Command::Confirm);
            VCR_CHECK(play.dialog().has_value() && lines(*play.dialog()).find("wagon") != std::string::npos);
        } else {
            // No treaty, and the tribe at 100 tension.
            VCR_CHECK(after.nations[kDutch].raw[kRelation] == 0x20);
            VCR_CHECK(tribeTension(after, kIroquois, kDutch) == 100);
            VCR_CHECK(play.dialog().has_value() && lines(*play.dialog()).find("WAR") != std::string::npos);
        }
        play.handle(Command::Confirm);
        VCR_CHECK(!play.dialog().has_value());
        // Met once: the next step brings no welcome.
        VCR_CHECK(!play.game().meetNeighbours(kUnit));
    }
}

VCR_TEST("original files: a missionary incites a tribe (measured)") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    const SaveGame original = readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {}));
    // As measured: a Dutch missionary (unit 47) beside the Apache camp at
    // (4, 18). The Apache have met the French but not the English.
    constexpr int kDutch = 3, kFrench = 1, kApache = 5, kCamp = 15, kUnit = 47;
    const auto start = [&](int tension, int defence) {
        SaveGame save = original;
        auto& u = save.units[kUnit];
        u.type = 3;
        u.profession = 0x1C;
        u.x = 3;
        u.y = 18;
        u.movesUsed = 0;
        u.orders = SaveUnit::kNoOrders;
        save.nations[kDutch].raw[52 + 4 + kApache] = 0x60;
        setTribeTension(save, kApache, kDutch, tension);
        save.tribesAndMore[8 * 78 + 565 + kApache] = static_cast<std::uint8_t>(defence);
        return save;
    };
    const auto lines = [](const Dialog& d) {
        std::string all;
        for (const auto& l : d.lines) {
            all += l + " ";
        }
        return all;
    };
    // (tension + 75) x (6 x settlements + defence / 4 + 2 x (muskets +
    // byte 8)): 126 x 52 and 75 x 62.
    VCR_CHECK(Game(start(51, 56), art.rules).incitePrice(kUnit, kCamp) == 6552);
    VCR_CHECK(Game(start(0, 100), art.rules).incitePrice(kUnit, kCamp) == 4650);
    {
        PlayScreen play(Game(start(51, 56), art.rules), {});
        play.setArt(&art);
        play.game().setActiveUnit(kUnit);
        play.handle(Command::MoveE);
        // Establish Mission, Incite Indians, Cancel Action.
        VCR_CHECK(play.dialog().has_value() && play.dialog()->options.size() == 3);
        play.handle(Command::Down);
        play.handle(Command::Confirm);
        // Whom to attack: English, French, Spanish.
        VCR_CHECK(play.dialog().has_value() && play.dialog()->options.size() == 3);
        VCR_CHECK(play.dialog()->options[0] == "English");
        play.handle(Command::Confirm);
        // "We have no contact with the English."
        VCR_CHECK(play.dialog().has_value() && lines(*play.dialog()).find("no contact") != std::string::npos);
    }
    {
        PlayScreen play(Game(start(51, 56), art.rules), {});
        play.setArt(&art);
        play.game().setActiveUnit(kUnit);
        const int gold = play.game().state().nations[kDutch].gold;
        play.handle(Command::MoveE);
        play.handle(Command::Down);
        play.handle(Command::Confirm);
        play.handle(Command::Down);
        play.handle(Command::Confirm);
        // "We will gladly drive the French ... in exchange for 6552."
        VCR_CHECK(play.dialog().has_value() && lines(*play.dialog()).find("6552") != std::string::npos);
        VCR_CHECK(play.dialog()->options.size() == 2);
        play.handle(Command::Confirm);
        // Paid: the Apache 50 more tense toward the French (100, halved for
        // them), and the War Council.
        const auto& after = play.game().state();
        VCR_CHECK(after.nations[kDutch].gold == gold - 6552);
        VCR_CHECK(tribeTension(after, kApache, kFrench) == 50);
        VCR_CHECK(play.dialog().has_value() && lines(*play.dialog()).find("War Council") != std::string::npos);
        play.handle(Command::Confirm);
        VCR_CHECK(!play.dialog().has_value());
    }
}

VCR_TEST("original files: the original's tension change") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    SaveGame save = readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {}));
    constexpr int kDutch = 3, kFrench = 1, kApache = 5;
    setTribeTension(save, kApache, kDutch, 0);
    setTribeTension(save, kApache, kFrench, 0);
    save.nations[kDutch].raw[52 + 4 + kApache] = 0x64; // at war
    Game game(save, art.rules);
    game.changeTension(kApache, kDutch, 30);
    VCR_CHECK(tribeTension(game.state(), kApache, kDutch) == 30);
    game.changeTension(kApache, kFrench, 30); // the French take rises at half
    VCR_CHECK(tribeTension(game.state(), kApache, kFrench) == 15);
    game.changeTension(kApache, kDutch, 200);
    VCR_CHECK(tribeTension(game.state(), kApache, kDutch) == 100);
    VCR_CHECK((game.state().nations[kDutch].raw[52 + 4 + kApache] & 0x04) != 0);
    // A fall ends the war.
    game.changeTension(kApache, kDutch, -200);
    VCR_CHECK(tribeTension(game.state(), kApache, kDutch) == 0);
    VCR_CHECK((game.state().nations[kDutch].raw[52 + 4 + kApache] & 0x04) == 0);
}

VCR_TEST("original files: a missionary denounces another nation's mission") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    SaveGame save = readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {}));
    // As seen in the original: a Dutch missionary beside the Iroquois
    // capital at (27, 14), which has a French mission.
    constexpr int kDutch = 3, kFrench = 1, kIroquois = 3, kCapital = 1, kUnit = 47;
    auto& u = save.units[kUnit];
    u.type = 3;
    u.profession = 0x1C;
    u.x = 28;
    u.y = 14;
    u.movesUsed = 0;
    u.orders = SaveUnit::kNoOrders;
    save.nations[kDutch].raw[52 + 4 + kIroquois] = 0x60;
    save.tribesAndMore[kIroquois * 78 + 58 + kDutch] = 0x60;
    {
        // The French colonies of Quebec and Montreal press on the capital
        // and two villages near them.
        const Game game(save, art.rules);
        int pressure = 0;
        VCR_CHECK(game.settlementPressure(kCapital, &pressure) == kFrench && pressure == 3);
        VCR_CHECK(game.settlementPressure(2, &pressure) == kFrench && pressure == 2);
        VCR_CHECK(game.settlementPressure(4, &pressure) == -1 && pressure == 0);
    }
    PlayScreen play(Game(save, art.rules), {});
    play.setArt(&art);
    play.game().setActiveUnit(kUnit);
    const auto units = play.game().state().units.size();
    play.handle(Command::MoveW);
    // Denounce Heresy of French Mission, Incite Indians, Cancel Action.
    VCR_CHECK(play.dialog().has_value() && play.dialog()->options.size() == 3);
    VCR_CHECK(play.dialog()->options[0] == "Denounce Heresy of French Mission");
    play.handle(Command::Confirm);
    VCR_CHECK(play.dialog().has_value());
    const auto& after = play.game().state();
    VCR_CHECK(after.units.size() == units - 1);
    if (missionOf(after.settlements[kCapital]) == kDutch) {
        // Seen twice: the converts burn the French mission; the Iroquois
        // a step calmer toward the Dutch (2 to 0) and 1 tenser toward the
        // French (2, halved).
        VCR_CHECK(tribeTension(after, kIroquois, kDutch) == 0);
        VCR_CHECK(tribeTension(after, kIroquois, kFrench) == 1);
    } else {
        VCR_CHECK(missionOf(after.settlements[kCapital]) == kFrench);
        VCR_CHECK(tribeTension(after, kIroquois, kDutch) == 4);
    }
}

VCR_TEST("original files: a tribe at 100 with a treaty burns the nation's missions") {
    const std::filesystem::path dir = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(dir)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const Art art = loadArt(dir);
    std::ifstream in(*findFileNoCase(dir, "COLONY00.SAV"), std::ios::binary);
    SaveGame save = readSaveGame(Bytes(std::istreambuf_iterator<char>(in), {}));
    constexpr int kDutch = 3, kFrench = 1, kApache = 5;
    std::vector<int> apache;
    for (int i = 0; i < static_cast<int>(save.settlements.size()); ++i) {
        if (tribeOf(save.settlements[static_cast<std::size_t>(i)]) == kApache) {
            apache.push_back(i);
        }
    }
    VCR_CHECK(apache.size() >= 2);
    if (apache.size() < 2) {
        return;
    }
    save.settlements[static_cast<std::size_t>(apache[0])].raw[5] = 0x10 | kDutch; // an expert's mission
    save.settlements[static_cast<std::size_t>(apache[1])].raw[5] = kFrench;
    const auto relation = static_cast<std::size_t>(kApache) * kTribeRecordSize + 58 + kDutch;
    const auto missions = [&](const Game& game, int nation) {
        int n = 0;
        for (int i : apache) {
            n += missionOf(game.state().settlements[static_cast<std::size_t>(i)]) == nation ? 1 : 0;
        }
        return n;
    };
    {
        // Without a treaty the missions stay, however tense the tribe.
        SaveGame copy = save;
        copy.tribesAndMore[relation] = 0x20;
        Game game(copy, art.rules);
        for (int i = 0; i < 40; ++i) {
            game.changeTension(kApache, kDutch, 100);
        }
        VCR_CHECK(missions(game, kDutch) == 1);
        VCR_CHECK(game.takeNotices().empty());
    }
    save.tribesAndMore[relation] = 0x60;
    save.humanNation = kDutch;
    Game game(save, art.rules);
    int tries = 0;
    while (missions(game, kDutch) > 0 && tries < 40) {
        game.changeTension(kApache, kDutch, 100);
        ++tries;
    }
    VCR_CHECK(missions(game, kDutch) == 0);
    VCR_CHECK(missions(game, kFrench) == 1);
    VCR_CHECK(game.state().settlements[static_cast<std::size_t>(apache[0])].raw[5] == 0xFF);
    const auto notices = game.takeNotices();
    VCR_CHECK(notices.size() == 1);
    VCR_CHECK(!notices.empty() && notices[0].section == "INDIANBURN");
    VCR_CHECK(!notices.empty() && notices[0].strings[0] == "Apache");
}
