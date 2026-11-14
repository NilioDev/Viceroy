// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/ui/play.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/game/colony.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game/natives.hpp"
#include "viceroy/game/sounds.hpp"
#include "viceroy/render/report_view.hpp"
#include "viceroy/data/text_table.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/sound/background_music.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <utility>

namespace viceroy {

namespace {

constexpr std::uint64_t kBlinkMs = 250;
// How long a unit takes to glide one square, and the pause before the next
// unit once one has used its moves. Chosen by eye against the original.
constexpr std::uint64_t kSlideMs = 160;
constexpr std::uint64_t kPauseMs = 250;
constexpr std::uint8_t kGreen = 0x44;
constexpr std::uint8_t kGold = 0x95;
constexpr std::uint8_t kBlack = 0x00;

std::optional<Step> stepOf(Command c) {
    switch (c) {
    case Command::MoveN:
        return kSteps[0];
    case Command::MoveNE:
        return kSteps[1];
    case Command::MoveE:
        return kSteps[2];
    case Command::MoveSE:
        return kSteps[3];
    case Command::MoveS:
        return kSteps[4];
    case Command::MoveSW:
        return kSteps[5];
    case Command::MoveW:
        return kSteps[6];
    case Command::MoveNW:
        return kSteps[7];
    default:
        return std::nullopt;
    }
}

Bytes readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), {});
}

} // namespace

PlayScreen::PlayScreen(Game game, std::filesystem::path savePath)
    : game_(std::move(game)), savePath_(std::move(savePath)) {
    // Start with the unit the save had active if it still needs orders.
    const int active = game_.activeUnit();
    if (active >= 0 && active < game_.unitCount() && game_.onMap(active)) {
        // Seen with nothing to move: the view and the panel are on it.
        view_.lookX = game_.unit(active).x;
        view_.lookY = game_.unit(active).y;
        centreOn(view_, game_, view_.lookX, view_.lookY);
    }
    if (active >= 0 && active < game_.unitCount() && game_.needsOrders(active)) {
        activate(active);
    } else {
        activate(game_.nextUnitNeedingOrders(-1));
    }
    if (game_.activeUnit() >= 0) {
        // Seen after loading: the view is centred on the active unit.
        centreOn(view_, game_, game_.unit(game_.activeUnit()).x, game_.unit(game_.activeUnit()).y);
    } else {
        // Nothing to move: look at the first colony.
        for (const auto& c : game_.state().colonies) {
            if (c.nation == game_.human()) {
                centreOn(view_, game_, c.x, c.y);
                break;
            }
        }
    }
}

void PlayScreen::say(std::string text) {
    view_.message = std::move(text);
}

void PlayScreen::activate(int index) {
    const int last = game_.activeUnit();
    game_.setActiveUnit(index);
    view_.blinkOn = true;
    clock_ = 0;
    if (index >= 0) {
        const auto& u = game_.unit(index);
        // Keep the map still while the unit is well inside the window.
        const bool inside = u.x > view_.originX && u.y > view_.originY && u.x < view_.originX + kViewColumns - 1 &&
                            u.y < view_.originY + kViewRows - 1;
        if (!inside) {
            centreOn(view_, game_, u.x, u.y);
        }
        view_.endOfTurn = false;
    } else {
        // Seen: the cursor stays on the square of the last unit moved, and
        // the panel tells about it, "End of Turn" below.
        if (last >= 0 && last < game_.unitCount() && game_.onMap(last)) {
            view_.lookX = game_.unit(last).x;
            view_.lookY = game_.unit(last).y;
        }
        view_.endOfTurn = true;
    }
}

void PlayScreen::advance() {
    const int current = game_.activeUnit();
    int next = game_.nextUnitNeedingOrders(current);
    // Units put off with Wait come last.
    if (next >= 0 && std::find(waiting_.begin(), waiting_.end(), next) != waiting_.end()) {
        int candidate = next;
        do {
            if (std::find(waiting_.begin(), waiting_.end(), candidate) == waiting_.end()) {
                break;
            }
            candidate = game_.nextUnitNeedingOrders(candidate);
        } while (candidate >= 0 && candidate != next);
        if (candidate >= 0 && std::find(waiting_.begin(), waiting_.end(), candidate) == waiting_.end()) {
            next = candidate;
        } else {
            waiting_.clear();
        }
    }
    activate(next);
}

void PlayScreen::moveActive(Step step) {
    const int index = game_.activeUnit();
    if (index < 0) {
        return;
    }
    const auto result = game_.move(index, step);
    // First contact: the tribe's welcome comes once the unit has moved
    // (seen: the next unit is up after it).
    if (auto met = game_.takeNotices(); !met.empty()) {
        for (auto& notice : met) {
            notices_.push_back(std::move(notice));
        }
        advanceAfterNotices_ = true;
    }
    if (animations_ && (result == MoveResult::Moved || result == MoveResult::Boarded || result == MoveResult::HighSeas)) {
        slide_ = Slide{index, step.dx, step.dy, 0,
                       result == MoveResult::Boarded || (result == MoveResult::Moved && game_.movesLeft(index) <= 0)};
        // Keep the unit in sight while it moves.
        const auto& u = game_.unit(index);
        const bool inside = u.x > view_.originX && u.y > view_.originY && u.x < view_.originX + kViewColumns - 1 &&
                            u.y < view_.originY + kViewRows - 1;
        if (!inside) {
            centreOn(view_, game_, u.x, u.y);
        }
        view_.slideUnit = index;
        view_.slideX = -step.dx * kSquare;
        view_.slideY = -step.dy * kSquare;
        if (result != MoveResult::HighSeas) {
            say(result == MoveResult::Boarded ? "Aboard ship." : "");
            return;
        }
    }
    switch (result) {
    case MoveResult::Moved:
        say("");
        if (!notices_.empty()) {
            showNotices(); // a tribe's welcome
        } else if (game_.movesLeft(index) <= 0) {
            advance();
        } else {
            activate(index);
        }
        break;
    case MoveResult::Boarded:
        say("Aboard ship.");
        advance();
        break;
    case MoveResult::Lost:
        // "Your expedition has vanished without a trace!"
        say("");
        if (!notices_.empty()) {
            showNotices();
        } else {
            advance();
        }
        break;
    case MoveResult::NoMovesLeft:
        advance();
        break;
    case MoveResult::Blocked: {
        // A ship steering onto land makes landfall: the units aboard wake
        // up, one at a time, to go ashore.
        const auto& u = game_.unit(index);
        const auto& map = game_.state().map;
        const int tx = u.x + step.dx;
        const int ty = u.y + step.dy;
        const auto aboard = game_.passengers(index);
        // A ship sailing into a coastal settlement trades with it.
        if (const auto settlement = game_.settlementAt(tx, ty); settlement && game_.isShip(index)) {
            enterVillage(index, *settlement);
            break;
        }
        if (game_.isShip(index) && !aboard.empty() && map.inside(tx, ty) && map.at(tx, ty).isLand()) {
            for (const int p : aboard) {
                game_.setOrders(p, SaveUnit::kNoOrders);
            }
            say("Landfall!");
            activate(aboard.front());
            break;
        }
        say(game_.isShip(index) ? "Ships can't go on land." : "Can't go there.");
        break;
    }
    case MoveResult::Occupied: {
        const auto& u = game_.unit(index);
        const int tx = u.x + step.dx;
        const int ty = u.y + step.dy;
        const int defender = game_.canAttack(index) ? game_.defenderAt(tx, ty, index) : -1;
        const auto colony = game_.colonyAt(tx, ty);
        const auto settlement = game_.settlementAt(tx, ty);
        if (settlement) {
            enterVillage(index, *settlement);
        } else if (colony && game_.unit(index).type == unit_type::kScout &&
                   game_.state().colonies[static_cast<std::size_t>(*colony)].nation < kEuropeanNations) {
            // Read from the original: a scout asks what to do there.
            scoutColony_ = *colony;
            ask("SCOUTCOLONY", index, {game_.state().colonies[static_cast<std::size_t>(*colony)].name});
            if (dialog_) {
                placeFrontiersmanDialog(*dialog_);
            }
        } else if (defender >= 0 && !settlement) {
            startAttack(index, defender);
        } else if (colony && !game_.isShip(index) && game_.canAttack(index) && game_.colonyUndefended(*colony)) {
            startAttack(index, -1, *colony);
        } else {
            say("Someone else is there.");
        }
        break;
    }
    case MoveResult::HighSeas:
        say("");
        ask("SAILHOME", index);
        if (dialog_) {
            placeNavalDialog(*dialog_);
        }
        break;
    }
}

void PlayScreen::openColony(int index) {
    if (index < 0 || index >= static_cast<int>(game_.state().colonies.size())) {
        return;
    }
    ColonyViewState state;
    state.colony = index;
    state.selectedColonist = -1;
    colonyScreen_ = state;
    lastColony_ = index;
}

void PlayScreen::openEurope() {
    EuropeViewState state;
    const auto ships = unitsInEurope(game_.state(), game_.rules(), game_.human(), EuropePlace::InPort, true);
    state.ship = ships.empty() ? -1 : ships.front();
    europeScreen_ = state;
}

void PlayScreen::enterVillage(int unit, int settlement) {
    if (art_ == nullptr) {
        return;
    }
    say("");
    const auto& save = game_.state();
    const int tribe = tribeOf(save.settlements[static_cast<std::size_t>(settlement)]);
    const int nation = game_.unit(unit).nation;
    // "a Camp of Sioux": the settlement's word for the tribe's level.
    std::string place;
    if (const auto* levels = art_->names.find("LEVELS")) {
        const auto row = static_cast<std::size_t>(tribeLevel(save, tribe));
        const auto fields = row < levels->lines.size() ? splitFields(levels->lines[row]) : std::vector<std::string>{};
        place = fields.size() > 1 ? fields[1] : "";
    }
    const auto& tribes = game_.rules().tribes;
    const std::string people =
        static_cast<std::size_t>(tribe) < tribes.size() ? tribes[static_cast<std::size_t>(tribe)].name : "";
    ask(villageGreeting(save, settlement, nation), unit, {place, people});
    if (!dialog_) {
        return;
    }
    village_ = settlement;
    villageOptions_.clear();
    // The options read from the original's @ACTIONS: Live Among The Natives
    // (row 4) for colonists the tribe knows, and Cancel Action (row 9).
    // Wagon trains and ships: Trade With Village (row 0).
    if (game_.canTradeWithVillage(unit, settlement)) {
        addOption(*dialog_, textLine(art_->names, "ACTIONS", 0, "Trade With Village"));
        villageOptions_.push_back(kVillageTrade);
    }
    // Scouts: Ask to Speak With Chief (row 5).
    if (game_.canSpeakWithChief(unit, settlement)) {
        addOption(*dialog_, textLine(art_->names, "ACTIONS", 5, "Ask to Speak With Chief"));
        villageOptions_.push_back(kVillageSpeak);
    }
    // Attack Village (row 8): first for soldiers and the stronger, last
    // for a scout (read from the original's list, and seen).
    const auto& types = game_.rules().units;
    const int type = game_.unit(unit).type;
    const bool strong = type >= 0 && type < static_cast<int>(types.size()) &&
                        types[static_cast<std::size_t>(type)].attack > 1;
    const auto offerAttack = [&] {
        addOption(*dialog_, textLine(art_->names, "ACTIONS", 8, "Attack Village"));
        villageOptions_.push_back(kVillageAttack);
    };
    const bool canAttack = game_.canAttackVillage(unit);
    if (canAttack && strong) {
        offerAttack();
    }
    if (game_.canLiveAmongNatives(unit, settlement)) {
        addOption(*dialog_, textLine(art_->names, "ACTIONS", 4, "Live Among The Natives"));
        villageOptions_.push_back(kVillageLive);
    }
    // Missionaries: Establish Mission (row 2).
    if (game_.canEstablishMission(unit, settlement)) {
        addOption(*dialog_, textLine(art_->names, "ACTIONS", 2, "Establish Mission"));
        villageOptions_.push_back(kVillageMission);
    }
    // Missionaries at another nation's mission: Denounce Heresy of French
    // Mission (row 3).
    if (game_.canDenounce(unit, settlement)) {
        std::string line = textLine(art_->names, "ACTIONS", 3, "Denounce Heresy of %Fs Mission");
        if (const auto at = line.find("%Fs"); at != std::string::npos) {
            line.replace(at, 3, adjective(missionOf(game_.state().settlements[static_cast<std::size_t>(settlement)])));
        }
        addOption(*dialog_, line);
        villageOptions_.push_back(kVillageDenounce);
    }
    // Missionaries: Incite Indians (row 6).
    if (game_.canIncite(unit, settlement)) {
        addOption(*dialog_, textLine(art_->names, "ACTIONS", 6, "Incite Indians"));
        villageOptions_.push_back(kVillageIncite);
    }
    // Units that can attack: Demand Tribute (row 7).
    if (game_.canDemandTribute(unit, settlement)) {
        addOption(*dialog_, textLine(art_->names, "ACTIONS", 7, "Demand Tribute"));
        villageOptions_.push_back(kVillageTribute);
    }
    if (canAttack && !strong) {
        offerAttack();
    }
    addOption(*dialog_, textLine(art_->names, "ACTIONS", 9, "Cancel Action"));
    villageOptions_.push_back(kVillageCancel);
    placeCentredDialog(*dialog_);
}

void PlayScreen::showLesson(int unit, const std::string& section, const std::vector<std::string>& strings) {
    const auto& save = game_.state();
    const int tribe = village_ >= 0 && village_ < static_cast<int>(save.settlements.size())
                          ? tribeOf(save.settlements[static_cast<std::size_t>(village_)])
                          : 0;
    ask(section, unit, strings);
    if (dialog_) {
        placeNative(tribe);
    }
}

void PlayScreen::placeNative(int tribe, int mood) {
    // Read from the original: the figure's mood is the step of the tribe's
    // tension toward the player.
    if (dialog_) {
        const int pose = std::clamp(
            mood >= 0 ? mood : tensionLevel(tribeTension(game_.state(), tribe, game_.human())), 0, 3);
        int width = 0;
        int height = 0;
        const auto sheet = static_cast<std::size_t>(kNativePortraits + 4 * tribe + pose);
        if (art_ != nullptr && sheet < art_->portraits.size() && !art_->portraits[sheet].sprites.empty()) {
            width = art_->portraits[sheet].sprites.front().image.width;
            height = art_->portraits[sheet].sprites.front().image.height;
        }
        placeNativeDialog(*dialog_, tribe, pose, width, height);
    }
}

void PlayScreen::speakWithChief(int unit) {
    const auto& save = game_.state();
    const int tribe = tribeOf(save.settlements[static_cast<std::size_t>(village_)]);
    const int nation = game_.unit(unit).nation;
    const auto talk = game_.speakWithChief(unit, village_);
    const auto& rules = game_.rules();
    const auto name = [](const auto& list, int row) {
        return row >= 0 && static_cast<std::size_t>(row) < list.size() ? list[static_cast<std::size_t>(row)].name
                                                                       : std::string();
    };
    // "The Arawak tribe": the tribe's adjective.
    const std::string people = static_cast<std::size_t>(tribe) < rules.tribes.size()
                                   ? rules.tribes[static_cast<std::size_t>(tribe)].adjective
                                   : "";
    chief_ = PendingChief{};
    chief_->unit = talk.answer == Game::ChiefAnswer::Kill ? -1 : unit;
    switch (talk.answer) {
    case Game::ChiefAnswer::Kill: chief_->section = "CHIEFKILL"; chief_->strings = {people}; break;
    case Game::ChiefAnswer::Bored: chief_->section = "CHIEFBORED"; chief_->strings = {people, adjective(nation)}; break;
    case Game::ChiefAnswer::Guides: {
        // "welcome you to our Camp": the second column of @LEVELS.
        std::string place;
        if (const auto* levels = art_->names.find("LEVELS")) {
            const auto row = static_cast<std::size_t>(tribeLevel(save, tribe));
            const auto fields = row < levels->lines.size() ? splitFields(levels->lines[row]) : std::vector<std::string>{};
            place = fields.size() > 1 ? fields[1] : "";
        }
        chief_->section = "CHIEFGUIDES";
        chief_->strings = {people, place};
        break;
    }
    case Game::ChiefAnswer::Tales: chief_->section = "CHIEFAREA"; chief_->strings = {people}; break;
    case Game::ChiefAnswer::Gift:
        chief_->section = "CHIEFGIFT";
        chief_->strings = {people, adjective(nation)};
        chief_->numbers = {talk.gold};
        break;
    }
    if (talk.welcome) {
        const auto& jobs = rules.jobs;
        const std::string experts = talk.skill >= 0 && static_cast<std::size_t>(talk.skill) < jobs.size()
                                        ? jobs[static_cast<std::size_t>(talk.skill)].expertName
                                        : "";
        showLesson(unit, "CHIEFHOWDY",
                   {experts, name(rules.cargo, talk.wants[0]), name(rules.cargo, talk.wants[1]),
                    name(rules.cargo, talk.wants[2])});
    } else {
        showChiefAnswer();
    }
}

void PlayScreen::foundMission(int unit) {
    const auto& save = game_.state();
    const auto& s = save.settlements[static_cast<std::size_t>(village_)];
    const int tribe = tribeOf(s);
    const int nation = game_.unit(unit).nation;
    const auto news = game_.establishMission(unit, village_);
    // "Church of Curacao mission founded in Autumn, 1653. Apache are
    // offended." Without a colony the home port names it (not measured).
    const std::string church = textLine(art_->names, "MISSION", nation, "");
    const std::string place = news.colony >= 0 ? save.colonies[static_cast<std::size_t>(news.colony)].name
                                               : textLine(art_->names, "HOMEPORT", nation, "");
    const auto& tribes = game_.rules().tribes;
    const std::string people =
        static_cast<std::size_t>(tribe) < tribes.size() ? tribes[static_cast<std::size_t>(tribe)].adjective : "";
    playMusic({{MusicCue::Kind::Sound, sounds::kChurch}}); // read from the original
    ask("MISSION" + std::to_string(news.reaction), -1, {church, place, seasonName(*art_, save.season), people}, -1,
        {save.year});
    if (dialog_) {
        placeReligiousDialog(*dialog_);
    }
    question_ = "LESSONDONE";
}

void PlayScreen::demandTribute(int unit) {
    const auto& save = game_.state();
    const int tribe = tribeOf(save.settlements[static_cast<std::size_t>(village_)]);
    const int nation = game_.unit(unit).nation;
    // The chief answers before the tribe's tension moves.
    const int mood = tensionLevel(tribeTension(save, tribe, game_.human()));
    const auto answer = game_.demandTribute(unit, village_);
    const auto& tribes = game_.rules().tribes;
    const auto t = static_cast<std::size_t>(tribe);
    const std::string people = t < tribes.size() ? tribes[t].name : "";
    // "Great Discoverer": the title is the game's difficulty.
    const std::string title = textLine(art_->names, "DIFFICULTY", save.difficulty, "");
    switch (answer.reply) {
    case Game::Tribute::Reply::Laugh:
        ask("EXTORTLAUGH", unit, {t < tribes.size() ? tribes[t].adjective : ""});
        break;
    case Game::Tribute::Reply::Refuse: {
        const std::string leader =
            nation >= 0 && nation < kEuropeanNations ? save.players[static_cast<std::size_t>(nation)].name : "";
        ask("EXTORTNO", unit, {title, leader, people});
        break;
    }
    case Game::Tribute::Reply::Poor: ask("EXTORTPOOR", unit, {title, people}); break;
    case Game::Tribute::Reply::Goods: {
        const std::string colony =
            answer.colony >= 0 ? save.colonies[static_cast<std::size_t>(answer.colony)].name : "";
        ask("EXTORTSTUFF", unit, {title, people, goodName(answer.good), colony}, -1, {answer.amount});
        break;
    }
    }
    placeNative(tribe, mood);
    question_ = "LESSONDONE";
}

void PlayScreen::startIncite(int unit) {
    const auto& save = game_.state();
    const int tribe = tribeOf(save.settlements[static_cast<std::size_t>(village_)]);
    const auto& tribes = game_.rules().tribes;
    const auto t = static_cast<std::size_t>(tribe);
    // "The Apache tribe is ready to go on the warpath. Whom would you like
    // us to attack?" The other nations, by name.
    ask("INDIANWARPATH", unit, {t < tribes.size() ? tribes[t].adjective : ""});
    for (const int nation : game_.inciteTargets(unit)) {
        addOption(*dialog_, adjective(nation));
        choices_.push_back(nation);
    }
    placeNative(tribe);
}

void PlayScreen::scoutColony(int unit, int colony, int choice) {
    // Meet With Mayor, Infiltrate Colony, Attack Colony, Nothing.
    if (colony < 0) {
        return;
    }
    if (choice == 2) {
        const auto& c = game_.state().colonies[static_cast<std::size_t>(colony)];
        const int defender = game_.canAttack(unit) ? game_.defenderAt(c.x, c.y, unit) : -1;
        if (defender >= 0) {
            startAttack(unit, defender);
        } else if (game_.canAttack(unit) && game_.colonyUndefended(colony)) {
            startAttack(unit, -1, colony);
        }
        return;
    }
    if (choice == 0) {
        game_.scoutMeetMayor(unit, colony);
    } else if (choice == 1) {
        if (!game_.infiltrateColony(unit, colony)) {
            // A look round the colony, without a say in it.
            ColonyViewState state;
            state.colony = colony;
            state.selectedColonist = -1;
            state.foreign = true;
            colonyScreen_ = state;
        }
    } else {
        game_.skipUnit(unit);
    }
    for (auto& notice : game_.takeNotices()) {
        notices_.push_back(std::move(notice));
    }
    advanceAfterNotices_ = true;
}

void PlayScreen::endIncite(int unit) {
    // Seen: the missionary's turn is over.
    game_.skipUnit(unit);
    village_ = -1;
    inciteTarget_ = -1;
    for (auto& notice : game_.takeNotices()) {
        notices_.push_back(std::move(notice));
    }
    advanceAfterNotices_ = true;
}

std::string PlayScreen::goodName(int good) const {
    const auto& cargo = game_.rules().cargo;
    return good >= 0 && static_cast<std::size_t>(good) < cargo.size() ? cargo[static_cast<std::size_t>(good)].name : "";
}

void PlayScreen::startTrade(int unit) {
    trade_ = VillageTrade{};
    trade_->unit = unit;
    const auto& u = game_.unit(unit);
    if (u.holds == 0) {
        afterSale();
    } else if (u.holds == 1) {
        offerHold(0);
    } else {
        // "Which cargo shall we offer to trade, Your Excellency?"
        ask("TRADEWHICH", unit);
        if (!dialog_) {
            return;
        }
        for (int k = 0; k < u.holds; ++k) {
            addOption(*dialog_, std::to_string(holdAmount(u, k)) + " " + goodName(holdGood(u, k)));
            trade_->holds.push_back(k);
        }
        addOption(*dialog_, textLine(art_->labelText, "MISC", 32, "Nothing"));
        placeCentredDialog(*dialog_);
    }
}

void PlayScreen::offerHold(int hold) {
    trade_->sale = game_.offerToVillage(trade_->unit, village_, hold);
    trade_->sold = trade_->sale.good;
    trade_->amount = trade_->sale.amount;
    const auto& sale = trade_->sale;
    if (sale.refusal == "BADCARGO") {
        endTrade(sale.refusal, {goodName(sale.good), goodName(sale.wants[0]), goodName(sale.wants[1]), goodName(sale.wants[2])});
    } else if (!sale.refusal.empty()) {
        endTrade(sale.refusal, {goodName(sale.good)});
    } else {
        showSale();
    }
}

void PlayScreen::showSale() {
    const auto& sale = trade_->sale;
    const auto& save = game_.state();
    ask(sale.haggles == 0 ? "TRADE0" : "TRADE1", trade_->unit,
        {textLine(art_->names, "VALUES", sale.quality, ""), goodName(sale.good)}, -1, {sale.price, sale.ask});
    if (dialog_) {
        placeNative(tribeOf(save.settlements[static_cast<std::size_t>(village_)]));
    }
}

void PlayScreen::afterSale() {
    // The original then names what the village needs, unless the good
    // offered was one of the two it wants most.
    if (!game_.hasRoomToBuy(trade_->unit)) {
        endTrade("", {});
        return;
    }
    const auto& save = game_.state();
    const auto& s = save.settlements[static_cast<std::size_t>(village_)];
    const auto wants = topWants(villageMarket(save, village_).wants, s);
    if (trade_->sold != wants[0] && trade_->sold != wants[1]) {
        showLesson(trade_->unit, "BRING", {goodName(wants[0]), goodName(wants[1]), goodName(wants[2])});
        return;
    }
    offerToBuy();
}

void PlayScreen::offerToBuy() {
    if (game_.refusesToSell(village_)) {
        endTrade("BADHAGGLE3", {});
        return;
    }
    if (trade_->sold < 0) {
        endTrade("DEFICIT", {});
        return;
    }
    trade_->forSale = game_.villageGoodsForSale(village_);
    std::vector<std::string> names;
    for (const int g : trade_->forSale) {
        names.push_back(goodName(g));
    }
    while (names.size() < 3) {
        names.emplace_back();
    }
    showLesson(trade_->unit, "BUYWHICH", names);
    if (dialog_ && dialog_->options.size() > trade_->forSale.size() + 1) {
        // Fewer goods than the three lines of the question.
        const auto keep = trade_->forSale.size();
        const auto last = dialog_->options.size() - 1;
        for (auto* list : {&dialog_->options, &dialog_->values}) {
            if (list->size() > last) {
                (*list)[keep] = (*list)[last];
                list->resize(keep + 1);
            }
        }
        dialog_->disabled.resize(keep + 1);
        dialog_->selected = 0;
    }
}

void PlayScreen::showPurchase() {
    const auto& p = trade_->purchase;
    const auto& save = game_.state();
    const int nation = game_.unit(trade_->unit).nation;
    // "your wagons" or "your holds" (LABELS.TXT).
    const std::string carrier = game_.isShip(trade_->unit) ? textLine(art_->labelText, "MISC", 42, "holds")
                                                           : textLine(art_->labelText, "MISC", 41, "wagons");
    const std::string section = p.haggles == 0 ? "BUY0" : "BUY1";
    dialog_ = makeDialog(*art_, section, {goodName(p.good), carrier},
                         {p.price, std::max(10, p.price / 2), p.amount, save.nations[static_cast<std::size_t>(nation)].gold});
    question_ = section;
    questionUnit_ = trade_->unit;
    choices_.clear();
    placeNative(tribeOf(save.settlements[static_cast<std::size_t>(village_)]));
}

void PlayScreen::endTrade(const std::string& section, std::vector<std::string> strings, std::vector<int> numbers) {
    const int unit = trade_ ? trade_->unit : -1;
    trade_.reset();
    if (section.empty()) {
        village_ = -1;
        advance();
        return;
    }
    const auto& save = game_.state();
    ask(section, unit, std::move(strings), -1, std::move(numbers));
    if (dialog_) {
        placeNative(tribeOf(save.settlements[static_cast<std::size_t>(village_)]));
    }
    question_ = "LESSONDONE";
}

void PlayScreen::showChiefAnswer() {
    if (!chief_) {
        return;
    }
    const auto c = std::move(*chief_);
    chief_.reset();
    const auto& save = game_.state();
    const int tribe = village_ >= 0 && village_ < static_cast<int>(save.settlements.size())
                          ? tribeOf(save.settlements[static_cast<std::size_t>(village_)])
                          : 0;
    if (c.section == "CHIEFKILL") {
        playMusic({{MusicCue::Kind::Sound, sounds::kChiefKill}}); // read from the original
    }
    ask(c.section, c.unit, c.strings, -1, c.numbers);
    if (dialog_) {
        placeNative(tribe);
    }
    question_ = "LESSONDONE";
}

void PlayScreen::ask(std::string_view section, int unit, std::vector<std::string> strings, int nation,
                     std::vector<int> numbers) {
    if (art_ == nullptr) {
        return;
    }
    dialog_ = makeDialog(*art_, section, strings, numbers, nation);
    choices_.clear();
    question_ = std::string(section);
    questionUnit_ = unit;
}

bool PlayScreen::handleDialog(Command command) {
    auto& d = *dialog_;
    if (d.checkbox) {
        return handleCheckboxes(command);
    }
    const int count = std::max(1, static_cast<int>(d.options.size()));
    const bool list = !choices_.empty();
    switch (command) {
    case Command::MoveN:
    case Command::Up:
        stepSelection(d, -1);
        return true;
    case Command::MoveS:
    case Command::Down:
        stepSelection(d, 1);
        return true;
    case Command::Confirm:
    case Command::Skip:
        break;
    case Command::Cancel:
        if (question_ == "WHICHFREEDOM") {
            // Read from the original: the question comes back until a
            // father is chosen.
            return true;
        }
        if (question_ == "TAXOPTIONS" || question_ == "KISSUP" || question_ == "MERCENARIES") {
            // Escape kisses the ring, or refuses to pay: only the second
            // choice holds a party or pays.
            d.selected = 0;
            break;
        }
        if (question_.rfind("KINGGALLEON", 0) == 0) {
            d.selected = 1; // the ring again: the treasure stays
            break;
        }
        if (question_ == "MEETING") {
            d.selected = -1; // no answer: the leader takes it as none of his
            break;
        }
        if (question_ == "SAILPORT" || question_ == "TRAVELPLACE" || question_.rfind("PICK", 0) == 0) {
            // Go To has no "None": Escape leaves the unit as it was.
            // Pick Music and its lists close without a pick.
            dialog_.reset();
            choices_.clear();
            question_.clear();
            return true;
        }
        // The first choice of a list is "None"; elsewhere the last is "no".
        d.selected = list ? 0 : count - 1;
        break;
    default:
        return true;
    }
    const int choice = d.selected;
    const std::string question = question_;
    const int unit = questionUnit_;
    dialog_.reset();
    answer(question, unit, choice);
    showNotices();
    return true;
}

std::string PlayScreen::adjective(int nation) const {
    if (art_ != nullptr) {
        if (const auto* s = art_->names.find("NATIONALITY")) {
            if (nation >= 0 && static_cast<std::size_t>(nation) < s->lines.size()) {
                return s->lines[static_cast<std::size_t>(nation)];
            }
        }
    }
    return "";
}

int PlayScreen::defenderNation(const PendingAttack& a) const {
    if (a.colony >= 0) {
        return game_.state().colonies[static_cast<std::size_t>(a.colony)].nation;
    }
    return game_.unit(a.defender).nation;
}

void PlayScreen::startAttack(int attacker, int defender, int colony) {
    attack_ = PendingAttack{attacker, defender, false, colony, colony >= 0 ? game_.pickColonyDefender(colony) : -1};
    const int us = game_.unit(attacker).nation;
    const int them = defenderNation(*attack_);
    if (game_.atPeace(us, them)) {
        // "We have signed a peace treaty with the French, Your Excellency."
        ask("HAVETREATY", attacker, {adjective(them)});
        if (dialog_) {
            placeMapDialog(*dialog_);
        }
        return;
    }
    continueAttack();
}

void PlayScreen::attackVillage(int unit) {
    // Read from the original: choosing to attack makes war on the tribe
    // at once; the fight follows as against any defender.
    game_.attackVillageWar(unit, village_);
    attack_ = PendingAttack{unit, game_.villageDefenderUnit(village_, unit), false, -1, -1, village_};
    continueAttack();
}

void PlayScreen::continueAttack() {
    if (!attack_) {
        return;
    }
    const int left = game_.movesLeft(attack_->attacker);
    if (left < 3 && !attack_->tiredAsked) {
        // "If we force them to attack this turn, they will fight at 1/3
        // strength."
        ask("HALF", attack_->attacker, {}, -1, {left});
        if (dialog_) {
            placeMapDialog(*dialog_);
        }
        return;
    }
    if (game_.combatAnalysisShown()) {
        analysisUp_ = true;
        return;
    }
    resolveAttack();
}

void PlayScreen::resolveAttack() {
    analysisUp_ = false;
    if (!attack_) {
        return;
    }
    const auto fight = *attack_;
    attack_.reset();
    if (fight.settlement >= 0) {
        game_.attackVillage(fight.attacker, fight.settlement);
    } else if (fight.colony >= 0) {
        // Seen: once the news is read the colony's screen opens.
        if (game_.attackColony(fight.attacker, fight.colony, fight.colonist) == CombatOutcome::AttackerWon) {
            colonyAfterNotices_ = fight.colony;
        }
    } else {
        game_.attack(fight.attacker, fight.defender);
    }
    for (auto& notice : game_.takeNotices()) {
        tell(std::move(notice));
    }
    // Seen: the map stays on the battle while its news is up.
    if (dialog_) {
        advanceAfterNotices_ = true;
    } else {
        advance();
    }
}

namespace {
// The question of every report about a colony: "Continue turn." or "Zoom
// to colony.".
constexpr const char* kColonyReport = "COLONYREPORT";
} // namespace

void PlayScreen::tell(GameNotice notice) {
    notices_.push_back(std::move(notice));
    showNotices();
}

void PlayScreen::showNotices() {
    if (!dialog_ && notices_.empty()) {
        view_.shownYear = -1; // the new turn's date
        if (advanceAfterNotices_) {
            advanceAfterNotices_ = false;
            advance();
        }
        if (colonyAfterNotices_ >= 0) {
            openColony(std::exchange(colonyAfterNotices_, -1));
        }
    }
    if (dialog_ || notices_.empty() || art_ == nullptr) {
        return;
    }
    GameNotice notice = notices_.front();
    notices_.erase(notices_.begin());
    // What happened before the news first (the sounds of a fight).
    playMusic(game_.takeMusicCues());
    playMusic(notice.music);
    for (const auto& [string, nation] : notice.adjectives) {
        if (string >= 0) {
            if (notice.strings.size() <= static_cast<std::size_t>(string)) {
                notice.strings.resize(static_cast<std::size_t>(string) + 1);
            }
            notice.strings[static_cast<std::size_t>(string)] = adjective(nation);
        }
    }
    for (const auto& [string, nation] : notice.countries) {
        // "England", or "the Netherlands" (read from the original).
        if (string >= 0) {
            if (notice.strings.size() <= static_cast<std::size_t>(string)) {
                notice.strings.resize(static_cast<std::size_t>(string) + 1);
            }
            const std::string country = textLine(art_->names, "COUNTRY", nation, "");
            notice.strings[static_cast<std::size_t>(string)] = nation == 3 ? "the " + country : country;
        }
    }
    for (const auto& [string, nation] : notice.ports) {
        if (string >= 0) {
            if (notice.strings.size() <= static_cast<std::size_t>(string)) {
                notice.strings.resize(static_cast<std::size_t>(string) + 1);
            }
            notice.strings[static_cast<std::size_t>(string)] = textLine(art_->names, "HOMEPORT", nation, "");
        }
    }
    for (const auto& line : notice.lines) {
        // A line of NAMES.TXT or GAME.TXT: "Viceroy", "the Ottoman Turks".
        if (line.string >= 0) {
            if (notice.strings.size() <= static_cast<std::size_t>(line.string)) {
                notice.strings.resize(static_cast<std::size_t>(line.string) + 1);
            }
            // A line of GAME.TXT is taken whole ("His Most Christian
            // Majesty, King Louis"); NAMES.TXT gives its first field.
            std::string text;
            if (line.names) {
                text = textLine(art_->names, line.section.c_str(), line.row, "");
            } else if (const auto* s = art_->gameText.find(line.section);
                       s != nullptr && line.row >= 0 && static_cast<std::size_t>(line.row) < s->lines.size()) {
                text = s->lines[static_cast<std::size_t>(line.row)];
                while (!text.empty() && (text.back() == ' ' || text.back() == '\r')) {
                    text.pop_back();
                }
            }
            notice.strings[static_cast<std::size_t>(line.string)] = text + line.after;
        }
    }
    for (const auto& [string, row] : notice.labels) {
        if (string >= 0) {
            if (notice.strings.size() <= static_cast<std::size_t>(string)) {
                notice.strings.resize(static_cast<std::size_t>(string) + 1);
            }
            notice.strings[static_cast<std::size_t>(string)] = textLine(art_->labelText, "MISC", row, "");
        }
    }
    if ((notice.section == "LOOT" || notice.section == "LOOT2") && notice.strings.size() >= 3) {
        // "Dutch loot and burn Apache Camp!": the second column of @LEVELS,
        // by the tribe's level, or the capital's row.
        if (const auto* levels = art_->names.find("LEVELS")) {
            const auto row = static_cast<std::size_t>(std::max(0, notice.value));
            const auto fields = row < levels->lines.size() ? splitFields(levels->lines[row]) : std::vector<std::string>{};
            notice.strings[2] = fields.size() > 1 ? fields[1] : "";
        }
    }
    if (notice.section == "INDIANWELCOME" && notice.strings.size() >= 2) {
        // "a glorious nation of 11 Villages": the third column of @LEVELS
        // by the tribe's level.
        if (const auto* levels = art_->names.find("LEVELS")) {
            const auto row = static_cast<std::size_t>(tribeLevel(game_.state(), notice.value));
            const auto fields = row < levels->lines.size() ? splitFields(levels->lines[row]) : std::vector<std::string>{};
            notice.strings[1] = fields.size() > 2 ? fields[2] : "";
        }
        welcomeTribe_ = notice.value;
        // Read from the original: the map centres on the unit first.
        if (notice.unit >= 0 && notice.unit < game_.unitCount()) {
            const auto& u = game_.unit(notice.unit);
            centreOn(view_, game_, u.x, u.y);
        }
    }
    if (notice.leader >= 0) {
        // A foreign leader speaks, his portrait over the box (measured).
        ask(notice.section, -1, notice.strings, -1, notice.numbers);
        if (dialog_) {
            if (notice.section == "MILITARY") {
                // "You must attack the infidel . . ." and whom.
                for (const auto& [who, name] : game_.allianceChoices()) {
                    addOption(*dialog_, who < kEuropeanNations ? adjective(who) : name);
                }
                dialog_->selected = 0;
            }
            if (notice.value == Game::kMeetingQuestion) {
                question_ = "MEETING";
            }
            placeLeaderDialog(*art_, *dialog_, notice.leader);
        }
        return;
    }
    if (notice.value == Game::kForeignNews) {
        // Treaties and wars between other nations: the man of the home port
        // tells them.
        ask(notice.section, -1, notice.strings, -1, notice.numbers);
        if (dialog_) {
            placeForeignNewsDialog(*dialog_);
        }
        return;
    }
    if (notice.section.rfind("KING", 0) == 0 && notice.section != "KINGBUY") {
        // The King speaks, standing left of the box; a rise in tax may be
        // answered with a party (read from the original).
        ask(notice.section, -1, notice.strings, -1, notice.numbers);
        if (dialog_) {
            if (notice.value == Game::kTaxQuestion) {
                addOptionsFrom(*art_, *dialog_, "TAXOPTIONS", notice.strings, notice.numbers);
                question_ = "TAXOPTIONS";
            }
            placeKingDialog(*dialog_);
        }
        return;
    }
    if (notice.section == "CONGRESSHALL") {
        // Read from the original: the new father stands in the hall with
        // the others until a key is pressed.
        congressHall_ = std::max(0, notice.value);
        return;
    }
    if (notice.section == "FREEDOM") {
        // Seen: a plain box in the middle of the screen, nobody behind it.
        ask(notice.section, -1, notice.strings);
        return;
    }
    if (notice.section == "WHICHFREEDOM") {
        // "Which Founding Father shall we appoint ...?": a candidate of
        // each category, as "Peter Minuit (Trade Adviser)" (seen).
        Dialog d = makeDialog(*art_, "WHICHFREEDOM");
        choices_.clear();
        const auto& candidates = game_.fatherCandidates();
        const auto& fathers = game_.rules().fathers;
        const std::string adviser = textLine(art_->labelText, "MISC", 103, "Adviser");
        for (int c = 0; c < Game::kFatherCategories; ++c) {
            const int f = candidates[static_cast<std::size_t>(c)];
            if (f < 0 || static_cast<std::size_t>(f) >= fathers.size()) {
                continue;
            }
            addOption(d, fathers[static_cast<std::size_t>(f)].name + " (" + textLine(art_->names, "FOUNDING", c, "") +
                             " " + adviser + ")");
            choices_.push_back(c);
        }
        d.selected = 0;
        d.help = true;
        dialog_ = d;
        question_ = "WHICHFREEDOM";
        questionUnit_ = -1;
        return;
    }
    if (notice.section == "LOSTCITY0") {
        // A Fountain of Youth: "Which of the following individuals shall
        // we recruit?", one of the three on the docks, for free.
        const auto& nation = game_.state().nations[static_cast<std::size_t>(game_.human())];
        Dialog d = makeDialog(*art_, "LOSTCITY0");
        choices_.clear();
        for (std::size_t slot = 0; slot < nation.recruits.size(); ++slot) {
            const auto job = static_cast<std::size_t>(professionJob(nation.recruits[slot]));
            addOption(d, job < game_.rules().jobs.size() ? game_.rules().jobs[job].expertName : "?");
            choices_.push_back(static_cast<int>(slot));
        }
        d.selected = 0;
        placeFrontiersmanDialog(d);
        dialog_ = d;
        question_ = "LOSTCITY0";
        questionUnit_ = -1;
        return;
    }
    const bool visit = notice.section == "INDIANBEGFOOD" || notice.section == "INDIANCITY" ||
                       notice.section == "INDIANWAGONS" || notice.section == "INDIANCOMMENT" ||
                       notice.section == "INDIANGIVEFOOD" || notice.section == "INDIANGIVESTUFF" ||
                       notice.section == "INDIANSCONVERT";
    if (visit) {
        // A brave's visit: the map centres on the brave and its chief
        // speaks.
        if (notice.unit >= 0 && notice.unit < game_.unitCount()) {
            const auto& u = game_.unit(notice.unit);
            centreOn(view_, game_, u.x, u.y);
        }
        ask(notice.section, -1, notice.strings, -1, notice.numbers);
        if (dialog_) {
            placeNative(notice.value);
        }
        return;
    }
    if (notice.section == "INDIANWELCOME" || notice.section == "INDIANPEACE" || notice.section == "INDIANCOME" ||
        notice.section == "INDIANSHUN" || notice.section == "NOCONTACT" || notice.section == "UNFORTUNATE" ||
        notice.section == "ALREADYSMITE") {
        // Seen: the chief speaks, in the mood of the tribe's tension.
        ask(notice.section, -1, notice.strings, -1, notice.numbers);
        if (dialog_) {
            placeNative(notice.value);
        }
        return;
    }
    if (notice.section == "INDIANBOW") {
        // Read from the original: the chief speaks it.
        ask(notice.section, -1, notice.strings);
        if (dialog_) {
            placeNative(notice.value);
        }
        return;
    }
    if (notice.section == "HERESY0" || notice.section == "HERESY1") {
        // Seen: the religious advisor tells it.
        ask(notice.section, -1, notice.strings);
        if (dialog_) {
            placeReligiousDialog(*dialog_);
        }
        return;
    }
    if (notice.section == "UNREST") {
        // "Colonists (%STRING1) now available in %STRING0."
        const auto& jobs = game_.rules().jobs;
        constexpr std::size_t kColonistRow = 19;
        const auto row = notice.value >= 0 && notice.value < 28 ? static_cast<std::size_t>(notice.value) : kColonistRow;
        notice.strings = {textLine(art_->names, "HOMEPORT", game_.human(), ""),
                          row < jobs.size() ? jobs[row].expertName : ""};
        ask(notice.section, -1, notice.strings, game_.human());
        if (dialog_) {
            placeReligiousDialog(*dialog_);
        }
        return;
    }
    if (notice.section == "EVASIVE" || notice.section == "SHIPDAMAGE" || notice.section == "SHIPSUNK" ||
        notice.section == "REFIT") {
        // Sea battles and repairs: a ship sent to Europe names the home
        // port. Observed: the naval advisor tells them.
        if (notice.value >= 0) {
            const std::size_t port = notice.section == "REFIT" ? 1 : 2;
            if (notice.strings.size() <= port) {
                notice.strings.resize(port + 1);
            }
            notice.strings[port] = textLine(art_->names, "HOMEPORT", notice.value, "");
        }
        ask(notice.section, -1, notice.strings, -1, notice.numbers);
        if (dialog_) {
            placeNavalDialog(*dialog_);
        }
        return;
    }
    if (notice.colony >= 0 && notice.colony < static_cast<int>(game_.state().colonies.size())) {
        // A report about a colony: the map shows it, and the box offers
        // to go there.
        const auto& colony = game_.state().colonies[static_cast<std::size_t>(notice.colony)];
        // The liberty reports name the country as their second string.
        if (notice.strings.size() < 2) {
            notice.strings.resize(2);
            notice.strings[1] = countryName(*art_, game_.human());
        }
        centreOn(view_, game_, colony.x, colony.y);
        ask(notice.section, notice.colony, notice.strings, game_.human(), notice.numbers);
        if (dialog_) {
            addOption(*dialog_, textLine(art_->labelText, "MISC", 34, "Continue turn."));
            addOption(*dialog_, textLine(art_->labelText, "MISC", 35, "Zoom to colony."));
            // Measured: the warehouse and food reports come from the woman
            // in the white bonnet, a new colonist from the priest, the rest
            // from the colonial advisor.
            const auto& section = notice.section;
            const auto startsWith = [&](std::string_view prefix) { return section.rfind(prefix, 0) == 0; };
            constexpr std::string_view kRunOuts[] = {"CANESUGAR", "TOBACCO", "COTTON", "FURS", "LUMBER", "ORE", "TOOLS"};
            const bool runOut = std::find(std::begin(kRunOuts), std::end(kRunOuts), section) != std::end(kRunOuts);
            if (runOut || startsWith("CARGOREADY") || startsWith("SPOIL") || startsWith("FOOD") || startsWith("STARVE")) {
                placeTradeDialog(*dialog_);
            } else if (section == "NEWCOLONIST") {
                placeReligiousDialog(*dialog_);
            } else {
                placeColonialDialog(*dialog_);
            }
            question_ = kColonyReport;
        }
        return;
    }
    if (notice.section.rfind("TUTORIAL", 0) == 0) {
        // %STRING2 of the cargo hint is the home port.
        if (notice.strings.size() < 3) {
            notice.strings.resize(3);
        }
        notice.strings[2] = textLine(art_->names, "HOMEPORT", game_.human(), "");
        ask(notice.section, -1, notice.strings, game_.human(), notice.numbers);
        if (dialog_) {
            placeTutorialDialog(*dialog_);
        }
        return;
    }
    ask(notice.section, -1, notice.strings, -1, notice.numbers);
    if (dialog_) {
        // Measured: battles are told by the colonial advisor, the work of
        // pioneers by the frontiersman.
        constexpr std::string_view kBattles[] = {"DEMOTE",     "COLONISTCAPTURE", "COLONISTCAPTURE2", "WAGONCAPTURE",
                                                 "LOOTCAPTURE", "ARTILLERY",      "ARTILLERY2",       "VETERAN",
                                                 "VALOR",       "CAPTURED",       "LOOT",             "LOOT2",
                                                 "INDIANSLAVES", "INDIANWARFARE", "INDIANBURN", "TEAPARTY",
                                                 "KINGBUY",     "REBELUP",        "REBELUP50",        "REBELDOWN",
                                                 "MERCENARIES", "MERCS", "NOMAYORSDURINGREV"};
        if (std::find(std::begin(kBattles), std::end(kBattles), notice.section) != std::end(kBattles)) {
            placeMapDialog(*dialog_);
        } else if (notice.section == "LOOTCASH" || notice.section == "CASHTREASURE") {
            placeTradeDialog(*dialog_); // read from the original: the trade adviser
        } else {
            placeFrontiersmanDialog(*dialog_);
        }
    }
}

void PlayScreen::work(int orders) {
    const int active = game_.activeUnit();
    if (active < 0) {
        return;
    }
    switch (game_.startWork(active, orders)) {
    case WorkResult::Started:
    case WorkResult::Finished:
        for (auto& notice : game_.takeNotices()) {
            tell(std::move(notice));
        }
        advance();
        break;
    case WorkResult::AlreadyPlowed:
        tell({"NOPLOW", {}});
        break;
    case WorkResult::AlreadyRoad:
        tell({"NOROAD", {}});
        break;
    case WorkResult::NotAPioneer:
    case WorkResult::NotHere:
        break;
    }
}

namespace {

constexpr int kGoToMore = -2;
constexpr int kGoToEurope = -1;
constexpr std::size_t kGoToPageSize = 10;

} // namespace

void PlayScreen::openGoTo(int page) {
    // Seen in the original: "Select a port to sail to:" for a ship, with
    // the home port first ("Amsterdam (Netherlands)"), or "Select a colony
    // to travel to:", centred on the screen. Past ten entries a page ends
    // with "(More)" and the next starts with it.
    const int active = game_.activeUnit();
    if (active < 0) {
        return;
    }
    const bool ship = game_.isShip(active);
    std::vector<int> entries;
    if (ship) {
        entries.push_back(kGoToEurope);
    }
    for (const int c : game_.goToColonies(active)) {
        entries.push_back(c);
    }
    const std::size_t pages = std::max<std::size_t>(1, (entries.size() + kGoToPageSize - 1) / kGoToPageSize);
    goToPage_ = static_cast<int>(static_cast<std::size_t>(page) % pages);
    const std::size_t first = static_cast<std::size_t>(goToPage_) * kGoToPageSize;
    const std::size_t last = std::min(entries.size(), first + kGoToPageSize);
    const std::string section = ship ? "SAILPORT" : "TRAVELPLACE";
    Dialog d = makeDialog(*art_, section);
    choices_.clear();
    if (goToPage_ > 0) {
        addOption(d, "(More)");
        choices_.push_back(kGoToMore);
    }
    for (std::size_t i = first; i < last; ++i) {
        const int e = entries[i];
        if (e == kGoToEurope) {
            const int nation = game_.human();
            addOption(d, textLine(art_->names, "HOMEPORT", nation, "") + " (" +
                             textLine(art_->names, "COUNTRY", nation, "") + ")");
        } else {
            addOption(d, game_.state().colonies[static_cast<std::size_t>(e)].name);
        }
        choices_.push_back(e);
    }
    if (pages > 1 && goToPage_ == 0) {
        addOption(d, "(More)");
        choices_.push_back(kGoToMore);
    }
    // Measured: the entries stand 10 pixels in from the title, and the
    // bar starts on the first line of every page.
    d.optionLeft = 15;
    d.selected = 0;
    placeCentredDialog(d);
    dialog_ = d;
    question_ = section;
    questionUnit_ = active;
}

void PlayScreen::goTo(int choice) {
    const int active = game_.activeUnit();
    if (active < 0 || choice < kGoToMore) {
        return;
    }
    if (choice == kGoToMore) {
        openGoTo(goToPage_ + 1);
        return;
    }
    if (choice == kGoToEurope) {
        if (!game_.goToEurope(active)) {
            return;
        }
    } else {
        const auto& c = game_.state().colonies[static_cast<std::size_t>(choice)];
        game_.setGoTo(active, c.x, c.y);
    }
    // Seen: the unit sets off at once.
    game_.moveGoToUnits();
    for (auto& notice : game_.takeNotices()) {
        notices_.push_back(std::move(notice));
    }
    if (!game_.needsOrders(active)) {
        advance();
    }
}

void PlayScreen::answer(const std::string& question, int unit, int choice) {
    const bool yes = choice == 0;
    const auto picked = [&]() {
        return choice >= 0 && static_cast<std::size_t>(choice) < choices_.size() ? choices_[static_cast<std::size_t>(choice)]
                                                                                 : -1;
    }();
    choices_.clear();
    if (question == "PICKMUSIC") {
        // The twelve tunes, then three more lists.
        static constexpr const char* kLists[3] = {"PICKINDEPENDENCE", "PICKMILITARY", "PICKINDIAN"};
        static constexpr int kFirstTune[3] = {13, 19, 23};
        if (picked >= 0 && picked < 12) {
            pickTune(picked + 1);
        } else if (picked >= 12 && picked < 15) {
            const int list = picked - 12;
            ask(kLists[list], -1);
            if (dialog_) {
                const int playing = music_ != nullptr ? music_->current() : 0;
                for (int i = 0; i < static_cast<int>(dialog_->options.size()); ++i) {
                    choices_.push_back(kFirstTune[list] + i);
                    if (sound::tuneCommand(kFirstTune[list] + i) == playing) {
                        dialog_->selected = i;
                        dialog_->marked = i;
                    }
                }
            }
        }
        return;
    }
    if (question.rfind("PICK", 0) == 0 && question != "PICKMUSIC") {
        if (picked > 0) {
            pickTune(picked);
        }
        return;
    }
    const auto showOnDocks = [&](int made) {
        if (made >= 0 && europeScreen_) {
            europeScreen_->focus = EuropeFocus::Docks;
            europeScreen_->dock = 0;
            const auto ships = unitsInEurope(game_.state(), game_.rules(), game_.human(), EuropePlace::InPort, true);
            if (europeScreen_->ship < 0 && !ships.empty()) {
                europeScreen_->ship = ships.front();
            }
        }
    };
    const bool villageQuestion = question.rfind("VILLAGE", 0) == 0;
    if (villageQuestion) {
        const int option = choice >= 0 && choice < static_cast<int>(villageOptions_.size())
                               ? villageOptions_[static_cast<std::size_t>(choice)]
                               : kVillageCancel;
        villageOptions_.clear();
        if (option == kVillageLive && village_ >= 0) {
            const auto lesson = game_.liveAmongNatives(unit, village_);
            showLesson(unit, lesson.section, lesson.strings);
            if (lesson.section != "LEARNSTAY") {
                question_ = "LESSONDONE";
            }
        } else if (option == kVillageSpeak && village_ >= 0) {
            speakWithChief(unit);
        } else if (option == kVillageMission && village_ >= 0) {
            foundMission(unit);
        } else if (option == kVillageTrade && village_ >= 0) {
            startTrade(unit);
        } else if (option == kVillageDenounce && village_ >= 0) {
            game_.denounceHeresy(unit, village_);
            village_ = -1;
            for (auto& notice : game_.takeNotices()) {
                notices_.push_back(std::move(notice));
            }
            advanceAfterNotices_ = true;
        } else if (option == kVillageIncite && village_ >= 0) {
            startIncite(unit);
        } else if (option == kVillageTribute && village_ >= 0) {
            demandTribute(unit);
        } else if (option == kVillageAttack && village_ >= 0) {
            attackVillage(unit);
        }
    } else if (question == "SCOUTCOLONY") {
        scoutColony(unit, std::exchange(scoutColony_, -1), choice);
    } else if (question == "SAILPORT" || question == "TRAVELPLACE") {
        goTo(picked);
    } else if (question == "LOSTCITY0") {
        game_.recruitFree(picked);
    } else if (question == "WHICHFREEDOM") {
        // A father who joins at once is announced before the rest of the
        // news, as in the original.
        game_.chooseFather(picked);
        auto news = game_.takeNotices();
        notices_.insert(notices_.begin(), std::make_move_iterator(news.begin()), std::make_move_iterator(news.end()));
    } else if (question == "LOSTCITY4") {
        // "Let us search for treasure!" or "Stay clear of those!"
        game_.answerBurial(yes);
        for (auto& notice : game_.takeNotices()) {
            notices_.push_back(std::move(notice));
        }
    } else if (question == "INDIANBEGFOOD" || question == "INDIANCITY" || question == "INDIANWAGONS") {
        // Begging: "I'm sorry ..." or "We offer you ...". A demand on a
        // colony: "Man the stockade." or "Hand them over."; on a wagon
        // train: "Hand them over." or "Circle the wagons."
        game_.answerVisit(question == "INDIANWAGONS" ? choice == 0 : choice == 1);
        for (auto& notice : game_.takeNotices()) {
            notices_.push_back(std::move(notice));
        }
    } else if (question == "INDIANWELCOME") {
        // "Will you accept our treaty ...?" Yes or No.
        game_.answerWelcome(std::exchange(welcomeTribe_, -1), yes);
        for (auto& notice : game_.takeNotices()) {
            notices_.push_back(std::move(notice));
        }
    } else if (question == "INDIANWARPATH") {
        // Whom to attack: no contact ends it here, else the price.
        inciteTarget_ = picked;
        if (village_ >= 0 && game_.incite(unit, village_, picked, false) == Game::Incite::Done) {
            const auto& save = game_.state();
            const int tribe = tribeOf(save.settlements[static_cast<std::size_t>(village_)]);
            ask("INDIANWARPATH2", unit, {adjective(picked)}, -1, {game_.incitePrice(unit, village_)});
            placeNative(tribe);
        } else {
            endIncite(unit);
        }
    } else if (question == "INDIANWARPATH2") {
        // "Pay 6552." or "Never mind."
        if (yes && village_ >= 0) {
            game_.incite(unit, village_, inciteTarget_, true);
        }
        endIncite(unit);
    } else if (question == "LEARNSTAY") {
        // "Then I shall become a master ..." or "Not right now, thanks."
        if (yes && village_ >= 0) {
            game_.learnFromNatives(unit, village_);
            const auto& jobs = game_.rules().jobs;
            const int job = game_.unit(unit).profession;
            const auto& tribes = game_.rules().tribes;
            const int tribe = tribeOf(game_.state().settlements[static_cast<std::size_t>(village_)]);
            showLesson(unit, "LEARNDONE",
                       {static_cast<std::size_t>(tribe) < tribes.size() ? tribes[static_cast<std::size_t>(tribe)].name : "",
                        job >= 0 && static_cast<std::size_t>(job) < jobs.size() ? jobs[static_cast<std::size_t>(job)].name
                                                                               : ""});
        } else {
            showLesson(unit, "LEARNLATER", {});
        }
        question_ = "LESSONDONE";
    } else if (question == "TRADEWHICH") {
        if (trade_ && choice >= 0 && choice < static_cast<int>(trade_->holds.size())) {
            offerHold(trade_->holds[static_cast<std::size_t>(choice)]);
        } else {
            trade_.reset();
            village_ = -1;
            advance();
        }
    } else if (question == "TRADE0" || question == "TRADE1") {
        // Accept, ask a fairer price, (TRADE0 only) give it, never mind.
        const bool first = question == "TRADE0";
        if (!trade_) {
            advance();
        } else if (choice == 0) {
            game_.sellToVillage(trade_->unit, village_, trade_->sale);
            afterSale();
        } else if (choice == 1) {
            if (game_.haggleSale(trade_->unit, village_, trade_->sale)) {
                showSale();
            } else {
                endTrade("BADHAGGLE0", {"", goodName(trade_->sale.good)});
            }
        } else if (first && choice == 2) {
            game_.giveToVillage(trade_->unit, village_, trade_->sale);
            afterSale();
        } else {
            endTrade("", {});
        }
    } else if (question == "BRING") {
        offerToBuy();
    } else if (question == "BUYWHICH") {
        if (trade_ && choice >= 0 && choice < static_cast<int>(trade_->forSale.size())) {
            trade_->purchase = game_.offerFromVillage(trade_->unit, village_, trade_->forSale[static_cast<std::size_t>(choice)],
                                                      trade_->amount);
            showPurchase();
        } else {
            endTrade("", {});
        }
    } else if (question == "BUY0" || question == "BUY1") {
        if (!trade_) {
            advance();
        } else if (choice == 0) {
            if (game_.buyFromVillage(trade_->unit, village_, trade_->purchase)) {
                endTrade("", {});
            } else {
                endTrade("NOTENOUGH", {}, {game_.state().nations[static_cast<std::size_t>(game_.unit(trade_->unit).nation)].gold});
            }
        } else if (choice == 1) {
            if (game_.hagglePurchase(trade_->unit, village_, trade_->purchase)) {
                showPurchase();
            } else {
                endTrade("BADHAGGLE2", {});
            }
        } else {
            endTrade("", {});
        }
    } else if (question == "CHIEFHOWDY") {
        showChiefAnswer();
    } else if (question == "LESSONDONE") {
        village_ = -1;
        advance();
    } else if (question == kColonyReport) {
        if (choice == 1) {
            openColony(unit);
        }
    } else if (question == "HAVETREATY") {
        // "Cancel Action." or "Break Treaty."
        if (choice == 1 && attack_) {
            const int us = game_.unit(attack_->attacker).nation;
            const int them = defenderNation(*attack_);
            game_.declareWar(us, them);
            // Measured: "Spanish cancel peace treaty with Dutch." when the
            // Dutch attack the Spanish.
            ask("CANCELPEACE", -1, {adjective(them), adjective(us)});
            if (dialog_) {
                placeMapDialog(*dialog_);
            }
        } else {
            attack_.reset();
        }
    } else if (question == "CANCELPEACE") {
        continueAttack();
    } else if (question == "HALF") {
        // "Charge!" or "Then let them rest."
        if (choice == 0 && attack_) {
            attack_->tiredAsked = true;
            continueAttack();
        } else {
            attack_.reset();
        }
    } else if (question == "SAILHOME") {
        if (yes && game_.sailToEurope(unit)) {
            say(game_.unitName(unit) + " sails for Europe.");
            advance();
        }
    } else if (question == "KISSUP") {
        // "Pay": unless the treasury is short (@KISSSORRY).
        if (choice == 1 && !game_.payBackTaxes(unit)) {
            const auto news = game_.takeNotices();
            ask("KISSSORRY", -1, {}, -1, news.empty() ? std::vector<int>{} : news.front().numbers);
        }
    } else if (question.rfind("KINGGALLEON", 0) == 0) {
        // "Very well, and let the Crown claim its rightful share."
        game_.answerTreasure(choice == 0);
        auto news = game_.takeNotices();
        notices_.insert(notices_.begin(), std::make_move_iterator(news.begin()), std::make_move_iterator(news.end()));
    } else if (question == "MEETING") {
        // A foreign leader's question: the talk goes on at once.
        game_.answerMeeting(choice);
        auto news = game_.takeNotices();
        notices_.insert(notices_.begin(), std::make_move_iterator(news.begin()), std::make_move_iterator(news.end()));
    } else if (question == "MERCENARIES") {
        // "No thank you." or "Pay": they land at once.
        game_.answerMercenaries(choice == 1);
        auto news = game_.takeNotices();
        notices_.insert(notices_.begin(), std::make_move_iterator(news.begin()), std::make_move_iterator(news.end()));
    } else if (question == "TAXOPTIONS") {
        // "Kiss pinky ring." or "Hold 'Boston Tea Party.'"
        // The party's news comes at once, before the rest of the turn's.
        game_.answerTaxRaise(choice == 1);
        auto news = game_.takeNotices();
        notices_.insert(notices_.begin(), std::make_move_iterator(news.begin()), std::make_move_iterator(news.end()));
    } else if (question == "SAILAWAY") {
        if (yes && game_.sailToNewWorld(unit) && europeScreen_) {
            const auto ships = unitsInEurope(game_.state(), game_.rules(), game_.human(), EuropePlace::InPort, true);
            europeScreen_->ship = ships.empty() ? -1 : ships.front();
        }
    } else if (question == "RECRUIT") {
        if (picked >= 0) {
            showOnDocks(game_.recruit(picked));
        }
    } else if (question == "KINGRECRUIT") {
        if (picked >= 0) {
            showOnDocks(game_.train(picked));
        }
    } else if (question == "PURCHASE") {
        if (picked >= 0 && art_ != nullptr) {
            const auto& nation = game_.state().nations[static_cast<std::size_t>(game_.human())];
            for (const auto& item : purchaseList(nation)) {
                if (item.unitType == picked) {
                    dialog_ = makeDialog(*art_, "REALLYBUY", {game_.rules().units[static_cast<std::size_t>(picked)].name},
                                         {item.price});
                    placeEuropeDialog(*dialog_, false);
                    question_ = "REALLYBUY";
                    questionUnit_ = picked;
                }
            }
        }
    } else if (question == "COLONYUNIT") {
        const int option = choice >= 0 && choice < static_cast<int>(unitOptions_.size())
                               ? unitOptions_[static_cast<std::size_t>(choice)]
                               : kUnitNoChange;
        switch (option) {
        case kUnitFront: {
            const int moved = game_.moveToFront(unit);
            if (colonyScreen_) {
                colonyScreen_->selectedUnit = moved;
            }
            break;
        }
        case kUnitClear: game_.setOrders(unit, SaveUnit::kNoOrders); break;
        case kUnitSentry: game_.setOrders(unit, SaveUnit::kSentry); break;
        case kUnitFortify:
            game_.setOrders(unit, SaveUnit::kFortify); // measured: orders 5
            playMusic({{MusicCue::Kind::Sound, sounds::kFortify}}); // read from the original
            break;
        default: break;
        }
    } else if (question == "BUYME1") {
        if (choice == 1) {
            game_.buyConstruction(unit);
        }
    } else if (question == "REALLYBUY") {
        if (yes) {
            const int made = game_.purchase(unit);
            if (made >= 0 && game_.isShip(made) && europeScreen_) {
                europeScreen_->ship = made;
                europeScreen_->focus = EuropeFocus::Ships;
            } else {
                showOnDocks(made);
            }
        }
    }
}

namespace {

std::string costText(int price) {
    return "(Cost: " + std::to_string(price) + ")";
}

} // namespace

void PlayScreen::tellPriceMove(int good, int move) {
    if (move == 0 || art_ == nullptr) {
        return;
    }
    const auto& nation = game_.state().nations[static_cast<std::size_t>(game_.human())];
    const std::string name =
        static_cast<std::size_t>(good) < game_.rules().cargo.size() ? game_.rules().cargo[static_cast<std::size_t>(good)].name : "";
    const std::string port = textLine(art_->names, "HOMEPORT", game_.human(), "");
    dialog_ = makeDialog(*art_, move > 0 ? "PRICEUP" : "PRICEDOWN", {name, port}, {bidPrice(nation, good)});
    choices_.clear();
    question_ = "PRICE";
    questionUnit_ = -1;
}

void PlayScreen::askBackTaxes(int good) {
    // Read from the original: a boycotted good can be traded again once
    // the back taxes are paid (@KISSUP).
    const std::string name = static_cast<std::size_t>(good) < game_.rules().cargo.size()
                                 ? game_.rules().cargo[static_cast<std::size_t>(good)].name
                                 : "";
    const std::string port = textLine(art_->names, "HOMEPORT", game_.human(), "");
    ask("KISSUP", good, {name, port}, -1, {game_.backTaxes(good)});
}

void PlayScreen::openRecruit() {
    if (art_ == nullptr) {
        return;
    }
    const auto& nation = game_.state().nations[static_cast<std::size_t>(game_.human())];
    const int price = recruitPrice(nation);
    Dialog d = makeDialog(*art_, "RECRUIT", {}, {price});
    choices_ = {-1};
    addOption(d, "(None)");
    for (std::size_t slot = 0; slot < nation.recruits.size(); ++slot) {
        const auto job = static_cast<std::size_t>(professionJob(nation.recruits[slot]));
        addOption(d, job < game_.rules().jobs.size() ? game_.rules().jobs[job].expertName : "?", {},
                  nation.gold < price);
        choices_.push_back(static_cast<int>(slot));
    }
    d.selected = 0;
    placeEuropeDialog(d);
    dialog_ = d;
    question_ = "RECRUIT";
    questionUnit_ = -1;
}

void PlayScreen::openPurchase() {
    if (art_ == nullptr) {
        return;
    }
    const auto& nation = game_.state().nations[static_cast<std::size_t>(game_.human())];
    Dialog d = makeDialog(*art_, "PURCHASE");
    choices_ = {-1};
    addOption(d, "None");
    for (const auto& item : purchaseList(nation)) {
        addOption(d, game_.rules().units[static_cast<std::size_t>(item.unitType)].name, costText(item.price),
                  nation.gold < item.price);
        choices_.push_back(item.unitType);
    }
    d.selected = 0;
    placeEuropeDialog(d);
    dialog_ = d;
    question_ = "PURCHASE";
    questionUnit_ = -1;
}

void PlayScreen::openTrain() {
    if (art_ == nullptr) {
        return;
    }
    const auto& nation = game_.state().nations[static_cast<std::size_t>(game_.human())];
    Dialog d = makeDialog(*art_, "KINGRECRUIT");
    choices_ = {-1};
    addOption(d, "None");
    for (const int row : trainingList(game_.rules())) {
        const auto& job = game_.rules().jobs[static_cast<std::size_t>(row)];
        addOption(d, job.expertName, costText(job.europeCost), nation.gold < job.europeCost);
        choices_.push_back(row);
    }
    d.selected = 0;
    placeEuropeDialog(d);
    dialog_ = d;
    question_ = "KINGRECRUIT";
    questionUnit_ = -1;
}

bool PlayScreen::handleEuropeScreen(Command command) {
    auto& state = *europeScreen_;
    say("");
    state.receipt.reset();
    auto& save = game_.state();
    const auto ships = unitsInEurope(save, game_.rules(), game_.human(), EuropePlace::InPort, true);
    const auto docks = unitsOnDocks(save, game_.rules(), game_.human());
    if (std::find(ships.begin(), ships.end(), state.ship) == ships.end()) {
        state.ship = ships.empty() ? -1 : ships.front();
    }
    state.dock = std::clamp(state.dock, 0, std::max(0, static_cast<int>(docks.size()) - 1));
    auto cycle = [](const std::vector<int>& list, int current, int by) {
        if (list.empty()) {
            return -1;
        }
        const auto it = std::find(list.begin(), list.end(), current);
        const int at = it == list.end() ? 0 : static_cast<int>(it - list.begin());
        const int n = static_cast<int>(list.size());
        return list[static_cast<std::size_t>((at + by + n) % n)];
    };
    const auto& nation = save.nations[static_cast<std::size_t>(game_.human())];
    const auto goodName = [&](int good) {
        return static_cast<std::size_t>(good) < game_.rules().cargo.size()
                   ? game_.rules().cargo[static_cast<std::size_t>(good)].name
                   : std::string();
    };
    switch (command) {
    case Command::Cancel:
    case Command::Europe:
        europeScreen_.reset();
        return true;
    case Command::MoveN:
    case Command::Up:
        state.focus = state.focus == EuropeFocus::Market  ? EuropeFocus::Ships
                      : state.focus == EuropeFocus::Ships ? EuropeFocus::Docks
                                                          : EuropeFocus::Buttons;
        return true;
    case Command::MoveS:
    case Command::Down:
        state.focus = state.focus == EuropeFocus::Buttons ? EuropeFocus::Docks
                      : state.focus == EuropeFocus::Docks ? EuropeFocus::Ships
                                                          : EuropeFocus::Market;
        return true;
    case Command::Recruit:
        openRecruit();
        return true;
    case Command::Purchase:
        openPurchase();
        return true;
    case Command::Train:
        openTrain();
        return true;
    case Command::MoveW:
    case Command::Left:
    case Command::MoveE:
    case Command::Right: {
        const int by = (command == Command::MoveW || command == Command::Left) ? -1 : 1;
        if (state.focus == EuropeFocus::Market) {
            state.good = (state.good + by + kGoodsKinds) % kGoodsKinds;
        } else if (state.focus == EuropeFocus::Ships) {
            state.ship = cycle(ships, state.ship, by);
        } else if (state.focus == EuropeFocus::Buttons) {
            state.button = (state.button + by + 3) % 3;
        } else if (!docks.empty()) {
            const int n = static_cast<int>(docks.size());
            state.dock = (state.dock + by + n) % n;
        }
        return true;
    }
    case Command::NextUnit:
        state.ship = cycle(ships, state.ship, 1);
        return true;
    case Command::Confirm:
        if (state.focus == EuropeFocus::Market) {
            if (state.ship < 0) {
                say("No ship in port.");
                return true;
            }
            if (game_.isBoycotted(game_.human(), state.good)) {
                askBackTaxes(state.good);
                return true;
            }
            const Trade t = buyGoods(save, game_.rules(), state.ship, state.good, game_.freeHolds(state.ship));
            if (t.amount > 0) {
                state.receipt = TradeReceipt{false, t.amount, goodName(state.good), t.price, t.gold, nation.taxRate, 0};
                tellPriceMove(state.good, t.priceMove);
            } else {
                say(nation.gold < askPrice(game_.rules(), nation, state.good) ? "Not enough gold."
                                                                              : "No room aboard.");
            }
        } else if (state.focus == EuropeFocus::Ships) {
            if (state.ship >= 0) {
                ask("SAILAWAY", state.ship);
            }
        } else if (state.focus == EuropeFocus::Buttons) {
            state.button == 0 ? openRecruit() : state.button == 1 ? openPurchase() : openTrain();
        } else if (!docks.empty() && state.ship >= 0) {
            const int who = docks[static_cast<std::size_t>(state.dock)];
            if (!game_.boardInPort(who, state.ship)) {
                say("No room aboard.");
            }
        }
        return true;
    case Command::Sentry: // S: sell
    case Command::BuildColony: // X on a gamepad
        if (state.focus == EuropeFocus::Market && game_.isBoycotted(game_.human(), state.good)) {
            askBackTaxes(state.good);
            return true;
        }
        if (state.focus == EuropeFocus::Market && state.ship >= 0) {
            const Trade t = sellGoods(save, game_.rules(), state.ship, state.good);
            if (t.amount > 0) {
                state.receipt =
                    TradeReceipt{true, t.amount, goodName(state.good), t.price, t.gold + t.tax, nation.taxRate, t.tax};
                tellPriceMove(state.good, t.priceMove);
            }
        }
        return true;
    case Command::Skip: // Space: everyone aboard the ship goes ashore
        if (state.ship >= 0) {
            for (const int p : game_.passengersInPort(state.ship)) {
                game_.leaveShipInPort(p);
            }
        }
        return true;
    default:
        return true;
    }
}

// "Select An Item To Build" opens on what the colony is building now.
void PlayScreen::openBuildList() {
    auto& state = *colonyScreen_;
    const auto& save = game_.state();
    const auto& colony = save.colonies[static_cast<std::size_t>(state.colony)];
    const auto options = buildOptions(colony, save.map, game_.rules(), buildLimits(save, colony.nation));
    state.panel = ColonyPanel::Make;
    state.buildList = 0;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (options[i].construction == colony.construction) {
            state.buildList = static_cast<int>(i) + 1;
        }
    }
}

// A click on a unit of the Units Present panel: "Options for Colonists
// (Indentured Servants):". Measured: "Move to front." is left out for the
// first unit, "Sentry / Board ship." for one already sentried (or without
// orders: the panel shows those as sentried), "Fortify." for one fortifying
// or fortified.
void PlayScreen::openUnitOptions(int unit, bool first) {
    if (art_ == nullptr) {
        return;
    }
    const auto& u = game_.unit(unit);
    std::string profession;
    constexpr int kFreeColonistJob = 19;
    const int job = professionJob(u.profession);
    const auto& jobs = game_.rules().jobs;
    if (job != kFreeColonistJob && job >= 0 && job < static_cast<int>(jobs.size())) {
        profession = " (" + jobs[static_cast<std::size_t>(job)].expertName + ")";
    }
    ask("COLONYUNIT", unit, {game_.unitName(unit), profession});
    if (!dialog_) {
        return;
    }
    unitOptions_.clear();
    const bool sentried = u.orders == SaveUnit::kNoOrders || u.orders == SaveUnit::kSentry;
    const bool fortified = u.orders == SaveUnit::kFortify || u.orders == SaveUnit::kFortified;
    for (int option = kUnitFront; option <= kUnitNoChange; ++option) {
        if ((option == kUnitFront && first) || (option == kUnitSentry && sentried) || (option == kUnitFortify && fortified)) {
            continue;
        }
        addOption(*dialog_, textLine(art_->gameText, "UNITOPTIONS", option, ""));
        unitOptions_.push_back(option);
    }
    placeUnitDialog(*dialog_, unit);
}

// BUY on the Make panel: the price to finish now, and the treasury.
void PlayScreen::openBuy() {
    auto& state = *colonyScreen_;
    const auto& save = game_.state();
    const auto& colony = save.colonies[static_cast<std::size_t>(state.colony)];
    const auto& nation = save.nations[static_cast<std::size_t>(game_.human())];
    const int price = constructionPrice(colony, game_.rules(), nation);
    if (price <= 0 || art_ == nullptr) {
        return;
    }
    state.panel = ColonyPanel::Make;
    ask(nation.gold >= price ? "BUYME1" : "BUYME0", state.colony, {constructionName(game_.rules(), colony.construction)},
        game_.human(), {price, nation.gold});
    placeBuyDialog(*dialog_);
}

bool PlayScreen::handleBuildList(Command command) {
    auto& state = *colonyScreen_;
    auto& save = game_.state();
    auto& colony = save.colonies[static_cast<std::size_t>(state.colony)];
    const auto options = buildOptions(colony, save.map, game_.rules(), buildLimits(save, colony.nation));
    const int lines = static_cast<int>(options.size()) + 1; // the first line is "(No Production)"
    switch (command) {
    case Command::MoveN:
    case Command::Up:
        state.buildList = (state.buildList + lines - 1) % lines;
        break;
    case Command::MoveS:
    case Command::Down:
        state.buildList = (state.buildList + 1) % lines;
        break;
    case Command::Confirm:
    case Command::Skip:
        if (state.buildList == 0) {
            colony.construction = -1;
        } else if (state.buildList <= static_cast<int>(options.size())) {
            const auto& o = options[static_cast<std::size_t>(state.buildList - 1)];
            if (!o.complete) {
                colony.construction = o.construction;
            }
        }
        state.buildList = -1;
        break;
    case Command::Cancel:
        state.buildList = -1;
        break;
    default:
        break;
    }
    return true;
}

bool PlayScreen::clickColonyScreen(int x, int y) {
    if (!colonyScreen_ || dialog_) {
        return false;
    }
    auto& state = *colonyScreen_;
    auto inside = [&](const auto& r) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; };
    if (state.buildList >= 0) {
        const auto& save = game_.state();
        const auto& colony = save.colonies[static_cast<std::size_t>(state.colony)];
        const auto limits = buildLimits(save, colony.nation);
        const int items = static_cast<int>(buildOptions(colony, save.map, game_.rules(), limits).size());
        const auto box = buildListBox(items);
        if (!inside(box)) {
            state.buildList = -1;
            return true;
        }
        const int line = (y - (box.firstLine - 1)) / 8;
        if (y >= box.firstLine - 1 && line <= items) {
            state.buildList = line;
            handleBuildList(Command::Confirm);
        }
        return true;
    }
    if (state.jobsMenu >= 0) {
        return false;
    }
    if (state.foreign) {
        return !(x >= 300 && y >= 178); // only the exit button
    }
    if (inside(kPanelButtons[0])) {
        state.panel = ColonyPanel::Production;
        return true;
    }
    if (inside(kPanelButtons[1])) {
        state.panel = ColonyPanel::Units;
        return true;
    }
    if (inside(kPanelButtons[2])) {
        state.panel = ColonyPanel::Make;
        return true;
    }
    if (state.panel == ColonyPanel::Make && inside(kChangeButton)) {
        openBuildList();
        return true;
    }
    if (state.panel == ColonyPanel::Units) {
        const auto& colony = game_.state().colonies[static_cast<std::size_t>(state.colony)];
        const auto units = unitsPresent(game_, colony);
        if (const int k = unitPresentAt(x, y); k >= 0 && k < static_cast<int>(units.size())) {
            state.selectedUnit = units[static_cast<std::size_t>(k)];
            openUnitOptions(state.selectedUnit, k == 0);
            return true;
        }
    }
    if (state.panel == ColonyPanel::Make && inside(kBuyButton)) {
        openBuy();
        return true;
    }
    return false;
}

bool PlayScreen::handleColonyScreen(Command command) {
    auto& state = *colonyScreen_;
    if (state.foreign) {
        if (command == Command::Cancel || command == Command::Confirm) {
            colonyScreen_.reset();
        }
        return true;
    }
    auto& save = game_.state();
    auto& colony = save.colonies[static_cast<std::size_t>(state.colony)];
    const int people = colony.population;
    if (state.buildList >= 0) {
        return handleBuildList(command);
    }
    if (state.jobsMenu >= 0) {
        const auto choices = jobChoices(colony, save.map, game_.rules(), save.colonies, state.selectedColonist, save.difficulty);
        const int lines = static_cast<int>(choices.size()) + 1; // the last line is "(More)"
        switch (command) {
        case Command::MoveN:
        case Command::Up:
            state.jobsMenu = (state.jobsMenu + lines - 1) % std::max(1, lines);
            break;
        case Command::MoveS:
        case Command::Down:
            state.jobsMenu = (state.jobsMenu + 1) % std::max(1, lines);
            break;
        case Command::Confirm:
        case Command::Skip:
            if (state.jobsMenu < static_cast<int>(choices.size())) {
                assignJob(colony, save.map, game_.rules(), save.colonies, state.selectedColonist,
                          choices[static_cast<std::size_t>(state.jobsMenu)].job);
            }
            state.jobsMenu = -1;
            break;
        case Command::Cancel:
            state.jobsMenu = -1;
            break;
        default:
            break;
        }
        return true;
    }
    switch (command) {
    case Command::Cancel:
        colonyScreen_.reset();
        break;
    case Command::BuildColony: // B, or X on a gamepad: choose what to build
        openBuildList();
        break;
    case Command::Purchase: // P, or Y on a gamepad: buy it
    case Command::EndTurn:
        openBuy();
        break;
    case Command::Confirm:
    case Command::Skip:
        if (state.selectedColonist >= 0 && state.selectedColonist < people) {
            // The jobs menu opens on the colonist's present job.
            const auto choices = jobChoices(colony, save.map, game_.rules(), save.colonies, state.selectedColonist, save.difficulty);
            const int current = colony.jobs[static_cast<std::size_t>(state.selectedColonist)];
            state.jobsMenu = 0;
            for (std::size_t i = 0; i < choices.size(); ++i) {
                if (choices[i].job == current) {
                    state.jobsMenu = static_cast<int>(i);
                }
            }
        } else {
            colonyScreen_.reset();
        }
        break;
    case Command::MoveE:
    case Command::Down:
    case Command::NextUnit:
        if (people > 0) {
            state.selectedColonist = (state.selectedColonist + 1) % people;
        }
        break;
    case Command::MoveW:
    case Command::Up:
    case Command::PreviousUnit:
        if (people > 0) {
            state.selectedColonist = state.selectedColonist < 0 ? people - 1
                                                                : (state.selectedColonist + people - 1) % people;
        }
        break;
    default:
        break;
    }
    return true;
}

void PlayScreen::clickSquare(int x, int y) {
    if (slide_) {
        return;
    }
    if (const auto colony = game_.colonyAt(x, y)) {
        if (game_.state().colonies[static_cast<std::size_t>(*colony)].nation == game_.human()) {
            openColony(*colony);
            return;
        }
    }
    if (viewMode_) {
        cursorX_ = x;
        cursorY_ = y;
        return;
    }
    // A square next to the active unit moves it there. Any other square
    // brings the map round to it, as in the original.
    const int active = game_.activeUnit();
    if (active >= 0 && game_.onMap(active)) {
        const auto& u = game_.unit(active);
        for (const auto& step : kSteps) {
            if (u.x + step.dx == x && u.y + step.dy == y) {
                moveActive(step);
                return;
            }
        }
    }
    centreOn(view_, game_, x, y);
}

void PlayScreen::clickMinimap(int x, int y) {
    centreOn(view_, game_, x, y);
    if (viewMode_) {
        cursorX_ = x;
        cursorY_ = y;
    }
}

void PlayScreen::scrollView(int dx, int dy) {
    const auto& map = game_.state().map;
    view_.originX = std::clamp(view_.originX + dx, 1, std::max(1, map.width - 1 - kViewColumns));
    view_.originY = std::clamp(view_.originY + dy, 1, std::max(1, map.height - 1 - kViewRows));
}

bool PlayScreen::handle(Command command) {
    if (slide_) {
        // Input waits until the unit has arrived.
        return true;
    }
    if (congressHall_ >= 0) {
        // Any key leaves the hall.
        congressHall_ = -1;
        showNotices();
        return true;
    }
    if (congressReport_) {
        // Any key closes the report.
        congressReport_ = false;
        return true;
    }
    if (dialog_) {
        return handleDialog(command);
    }
    if (analysisUp_) {
        // Any key goes on to the fight.
        resolveAttack();
        return true;
    }
    if (menu_) {
        return handleMenu(command);
    }
    if (europeScreen_) {
        return handleEuropeScreen(command);
    }
    if (colonyScreen_) {
        return handleColonyScreen(command);
    }
    if (command == Command::Europe) {
        openEurope();
        return true;
    }
    if (command == Command::CongressReport) {
        congressReport_ = true;
        return true;
    }
    if (command == Command::Menu) {
        openMenu(0, true);
        return true;
    }
    if (viewMode_) {
        if (const auto step = stepOf(command)) {
            const auto& map = game_.state().map;
            cursorX_ = std::clamp(cursorX_ + step->dx, 0, map.width - 1);
            cursorY_ = std::clamp(cursorY_ + step->dy, 0, map.height - 1);
            const bool inside = cursorX_ > view_.originX && cursorY_ > view_.originY &&
                                cursorX_ < view_.originX + kViewColumns - 1 && cursorY_ < view_.originY + kViewRows - 1;
            if (!inside) {
                centreOn(view_, game_, cursorX_, cursorY_);
            }
            return true;
        }
        switch (command) {
        case Command::Confirm:
            if (const auto colony = game_.colonyAt(cursorX_, cursorY_)) {
                if (game_.state().colonies[static_cast<std::size_t>(*colony)].nation == game_.human()) {
                    openColony(*colony);
                }
            }
            return true;
        case Command::Cancel:
        case Command::MoveMode:
            viewMode_ = false;
            return true;
        default:
            break;
        }
    }
    if (const auto step = stepOf(command)) {
        moveActive(*step);
        return true;
    }
    const int active = game_.activeUnit();
    switch (command) {
    case Command::Cancel:
        return false;
    case Command::ViewMode: {
        viewMode_ = true;
        if (active >= 0) {
            cursorX_ = game_.unit(active).x;
            cursorY_ = game_.unit(active).y;
        } else {
            cursorX_ = view_.originX + kViewColumns / 2;
            cursorY_ = view_.originY + kViewRows / 2;
        }
        break;
    }
    case Command::MoveMode:
        viewMode_ = false;
        break;
    case Command::Confirm:
        if (active < 0) {
            handle(Command::EndTurn);
        }
        break;
    case Command::EndTurn: {
        const int year = game_.state().year;
        const int season = game_.state().season;
        game_.endTurn();
        waiting_.clear();
        {
            std::string news;
            for (const auto& line : game_.lastTurnNews()) {
                news += (news.empty() ? "" : " ") + line;
            }
            say(news);
        }
        activate(game_.activeUnit());
        auto notices = game_.takeNotices();
        if (!notices.empty()) {
            view_.shownYear = year;
            view_.shownSeason = season;
        }
        for (auto& notice : notices) {
            tell(std::move(notice));
        }
        break;
    }
    case Command::Skip:
        if (active >= 0) {
            game_.skipUnit(active);
            advance();
        }
        break;
    case Command::Wait:
        if (active >= 0) {
            waiting_.push_back(active);
            advance();
        }
        break;
    case Command::Sentry:
        if (active >= 0) {
            game_.setOrders(active, SaveUnit::kSentry);
            advance();
        }
        break;
    case Command::Fortify:
        if (active >= 0 && !game_.isShip(active)) {
            game_.setOrders(active, SaveUnit::kFortify);
            advance();
        }
        break;
    case Command::BuildColony:
        if (active >= 0) {
            const std::string name =
                colonyNames_ != nullptr ? game_.suggestColonyName(*colonyNames_) : "New Colony";
            switch (game_.foundColony(active, name)) {
            case FoundResult::Founded:
                say("Founded " + name + ".");
                openColony(static_cast<int>(game_.state().colonies.size()) - 1);
                game_.setActiveUnit(-1);
                activate(game_.nextUnitNeedingOrders(-1));
                if (game_.activeUnit() >= 0) {
                    say("Founded " + name + ".");
                }
                break;
            case FoundResult::NotAColonist:
                say("Only colonists can build a colony.");
                break;
            case FoundResult::NotOnLand:
                say("Colonies must be built on land.");
                break;
            case FoundResult::Mountains:
                say("No colonies on mountains.");
                break;
            case FoundResult::TooClose:
                say("Too close to another colony.");
                break;
            }
        }
        break;
    case Command::NextUnit:
        advance();
        break;
    case Command::PreviousUnit: {
        const int count = game_.unitCount();
        for (int step = 1; step < count && active >= 0; ++step) {
            const int i = ((active - step) % count + count) % count;
            if (game_.needsOrders(i)) {
                activate(i);
                break;
            }
        }
        break;
    }
    case Command::Centre:
        if (active >= 0) {
            centreOn(view_, game_, game_.unit(active).x, game_.unit(active).y);
        }
        break;
    case Command::GoTo:
        if (active >= 0) {
            openGoTo(0);
        }
        break;
    case Command::Purchase: // P
        work(SaveUnit::kPlow);
        break;
    case Command::Recruit: // R
        work(SaveUnit::kRoad);
        break;
    case Command::Save:
        if (save()) {
            say("Game saved.");
        }
        break;
    default:
        break;
    }
    return true;
}

bool PlayScreen::save() {
    const Bytes bytes = writeSaveGame(game_.state());
    const auto temp = savePath_.string() + ".new";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            say("Could not save the game.");
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, savePath_, ec);
    if (ec) {
        say("Could not save the game.");
        return false;
    }
    return true;
}

void PlayScreen::tick(std::uint64_t milliseconds) {
    clock_ += milliseconds;
    playMusic(game_.takeMusicCues());
    view_.blinkOn = (clock_ / kBlinkMs) % 2 == 0;
    if (dialog_) {
        dialog_->shownMs += milliseconds;
        dialog_->overlayFrame = kingArmFrame(dialog_->shownMs);
    }
    if (slide_) {
        slide_->elapsed += milliseconds;
        const auto t = std::min(slide_->elapsed, kSlideMs);
        const int left = static_cast<int>((kSlideMs - t) * kSquare / kSlideMs);
        view_.slideX = -slide_->dx * left;
        view_.slideY = -slide_->dy * left;
        view_.blinkOn = true;
        if (slide_->elapsed >= kSlideMs + (slide_->advanceAfter ? kPauseMs : 0)) {
            finishSlide();
        }
    }
}

void PlayScreen::finishSlide() {
    if (!slide_) {
        return;
    }
    const Slide done = *slide_;
    slide_.reset();
    view_.slideUnit = -1;
    view_.slideX = 0;
    view_.slideY = 0;
    if (dialog_) {
        return; // the high seas question is up; it decides what comes next
    }
    if (!notices_.empty()) {
        showNotices(); // a tribe's welcome
        return;
    }
    if (done.advanceAfter) {
        advance();
    } else if (game_.activeUnit() == done.unit) {
        activate(done.unit);
    }
}

void PlayScreen::draw(Screen& screen, const Art& art) const {
    if (congressHall_ >= 0) {
        std::vector<bool> fathers(static_cast<std::size_t>(Game::kFoundingFathers));
        for (int f = 0; f < Game::kFoundingFathers; ++f) {
            fathers[static_cast<std::size_t>(f)] = game_.hasFather(game_.human(), f);
        }
        drawCongressHall(screen, art, fathers);
        return;
    }
    if (congressReport_) {
        drawCongressReport(screen, art, game_, game_.human());
        return;
    }
    drawScreen(screen, art);
    if (menu_ && !colonyScreen_ && !europeScreen_) {
        drawMenuBar(screen, art, art.menus, &*menu_);
    }
    if (analysisUp_ && attack_) {
        drawCombatAnalysis(screen, art, game_,
                           attack_->settlement >= 0 ? game_.analyseVillageAttack(attack_->attacker, attack_->settlement)
                           : attack_->colony >= 0
                               ? game_.analyseColonyAttack(attack_->attacker, attack_->colony, attack_->colonist)
                               : game_.analyseAttack(attack_->attacker, attack_->defender));
    }
    if (dialog_) {
        drawDialog(screen, art, *dialog_);
        if (dialog_->unit >= 0 && dialog_->unit < game_.unitCount()) {
            drawUnitInColonyList(screen, art, game_, dialog_->unit, dialog_->x + 5, dialog_->y + 6);
        }
    }
}

void PlayScreen::drawScreen(Screen& screen, const Art& art) const {
    if (europeScreen_ && colonyArt_ != nullptr) {
        drawEuropeScreen(screen, art, *colonyArt_, game_, *europeScreen_);
        // Our own line for what just happened (the original has none here).
        if (!view_.message.empty()) {
            const int w = Screen::textWidth(art.tiny, view_.message);
            screen.text(art.tiny, 160 - w / 2, 60, view_.message, {0x0F, kNoColour, kNoColour});
        }
        return;
    }
    if (colonyScreen_ && colonyArt_ != nullptr) {
        drawColonyScreen(screen, art, *colonyArt_, game_, *colonyScreen_);
        return;
    }
    drawGameScreen(screen, art, game_, view_);
    const bool lookAtEnd = view_.endOfTurn && game_.activeUnit() < 0 && view_.lookX >= 0;
    if ((viewMode_ || lookAtEnd) && view_.blinkOn) {
        const int lx = viewMode_ ? cursorX_ : view_.lookX;
        const int ly = viewMode_ ? cursorY_ : view_.lookY;
        const int px = kMapArea.x + (lx - view_.originX) * kSquare;
        const int py = kMapArea.y + (ly - view_.originY) * kSquare;
        screen.setClip(kMapArea);
        screen.frame({px, py, kSquare, kSquare}, 0x0F);
        screen.resetClip();
    }
}

// --- Load menu ---------------------------------------------------------------

std::vector<SaveSlot> listSaves(const std::filesystem::path& folder, const Art& art) {
    std::vector<SaveSlot> slots;
    for (int i = 0; i < 10; ++i) {
        const std::string name = "COLONY0" + std::to_string(i) + ".SAV";
        const auto path = findFileNoCase(folder, name);
        if (!path) {
            continue;
        }
        try {
            const auto save = readSaveGame(readFile(*path));
            std::string nation = "?";
            if (const auto* s = art.names.find("NATIONALITY")) {
                if (static_cast<std::size_t>(save.humanNation) < s->lines.size()) {
                    nation = s->lines[static_cast<std::size_t>(save.humanNation)];
                }
            }
            std::string season = save.season == 0 ? "Spring" : "Autumn";
            if (const auto* s = art.names.find("SEASONS")) {
                if (static_cast<std::size_t>(save.season) < s->lines.size()) {
                    season = s->lines[static_cast<std::size_t>(save.season)];
                }
            }
            const auto& player = save.players[static_cast<std::size_t>(save.humanNation)];
            slots.push_back({*path, name.substr(0, 8) + "  " + player.name + ", " + nation + "  " + season + " " +
                                        std::to_string(save.year)});
        } catch (const FormatError&) {
            // Not a save we can read: leave it out.
        }
    }
    return slots;
}

std::optional<SaveSlot> LoadMenu::handle(Command command) {
    if (slots_.empty()) {
        return std::nullopt;
    }
    const int count = static_cast<int>(slots_.size());
    switch (command) {
    case Command::Up:
    case Command::MoveN:
        selected_ = (selected_ + count - 1) % count;
        break;
    case Command::Down:
    case Command::MoveS:
        selected_ = (selected_ + 1) % count;
        break;
    case Command::Confirm:
        return slots_[static_cast<std::size_t>(selected_)];
    default:
        break;
    }
    return std::nullopt;
}

std::optional<SaveSlot> LoadMenu::click(int x, int y) {
    // The rows as draw() lays them out: from y 50, 10 apart.
    if (x < 34 || x >= 286 || y < 48) {
        return std::nullopt;
    }
    const int row = (y - 48) / 10;
    if (row >= static_cast<int>(slots_.size())) {
        return std::nullopt;
    }
    selected_ = row;
    return slots_[static_cast<std::size_t>(row)];
}

void LoadMenu::draw(Screen& screen, const Art& art) const {
    screen.clear(kBlack);
    screen.tile(art.wood, {0, 0, Screen::kWidth, Screen::kHeight});
    const Rect box{30, 30, 260, 24 + 10 * static_cast<int>(std::max<std::size_t>(slots_.size(), 1))};
    screen.fill(box, kBlack);
    screen.frame(box, kGold);
    screen.text(art.small, box.x + 8, box.y + 6, "Load Game", {kGold, kGold, kGold});
    if (slots_.empty()) {
        screen.text(art.tiny, box.x + 8, box.y + 20, "No savegames found.", {kGreen, kNoColour, kNoColour});
    }
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        const int y = box.y + 20 + 10 * static_cast<int>(i);
        const bool selected = static_cast<int>(i) == selected_;
        if (selected) {
            screen.fill({box.x + 4, y - 2, box.w - 8, 9}, kGreen);
        }
        screen.text(art.tiny, box.x + 8, y, slots_[i].label,
                    {selected ? kBlack : kGreen, kNoColour, kNoColour});
    }
}

} // namespace viceroy

// --- The menu bar -------------------------------------------------------------

namespace viceroy {
namespace {

// Rows of the menus in MENU.TXT that Viceroy acts on.
namespace game_menu {
constexpr int kSoundOptions = 2;
constexpr int kPickMusic = 3;
constexpr int kSave = 4;
constexpr int kLoad = 5;
constexpr int kExit = 8;
} // namespace game_menu
namespace view_menu {
constexpr int kMovePieces = 0;
constexpr int kViewPieces = 1;
constexpr int kCentre = 11;
} // namespace view_menu
namespace reports_menu {
constexpr int kCongress = 2;
} // namespace reports_menu
namespace orders_menu {
constexpr int kActivate = 0;
constexpr int kWait = 1;
constexpr int kFortify = 2;
constexpr int kUnfortify = 3; // a second "Fortify" row, not seen yet
constexpr int kSentry = 4;
constexpr int kBuildColony = 5;
constexpr int kJoinColony = 6;
constexpr int kClearForest = 7;
constexpr int kPlow = 8;
constexpr int kRoad = 9;
constexpr int kLoad = 10;
constexpr int kUnload = 11;
constexpr int kPillage = 12;
constexpr int kGoToPort = 13;
constexpr int kGoToPlace = 14;
constexpr int kTradeRoute = 15;
constexpr int kReturnToEurope = 16;
constexpr int kNoOrders = 17;
constexpr int kDump = 18;
// 19 is Disband, not handled yet.
} // namespace orders_menu
constexpr int kHiddenPedia = 6; // Miscellaneous and Complete are not shown
constexpr std::uint8_t kSeaLaneTerrain = 26;

} // namespace

std::vector<EntryState> PlayScreen::menuStates(int index) const {
    std::vector<EntryState> states;
    if (art_ == nullptr || index < 0 || index >= static_cast<int>(art_->menus.size())) {
        return states;
    }
    const auto& def = art_->menus[static_cast<std::size_t>(index)];
    states.assign(def.entries.size(), EntryState::Enabled);
    if (def.section == "PEDIA") {
        for (std::size_t i = kHiddenPedia; i < states.size(); ++i) {
            states[i] = EntryState::Hidden;
        }
    }
    if (def.section != "ORDERS") {
        return states;
    }
    // The orders depend on the active unit. Seen in the original for a
    // frigate at sea; the rest follows the manual and is not yet checked.
    using namespace orders_menu;
    auto set = [&](int row, EntryState state) {
        if (row < static_cast<int>(states.size())) {
            states[static_cast<std::size_t>(row)] = state;
        }
    };
    const int active = game_.activeUnit();
    set(kUnfortify, EntryState::Hidden);
    if (active < 0 || !game_.onMap(active)) {
        for (auto& s : states) {
            s = s == EntryState::Hidden ? s : EntryState::Disabled;
        }
        set(kBuildColony, EntryState::Hidden);
        set(kJoinColony, EntryState::Hidden);
        set(kClearForest, EntryState::Hidden);
        set(kPillage, EntryState::Hidden);
        set(kGoToPlace, EntryState::Hidden);
        return states;
    }
    const auto& u = game_.unit(active);
    const bool ship = game_.isShip(active);
    const bool wagon = u.type == unit_type::kWagonTrain;
    const bool pioneer = u.type == unit_type::kPioneer;
    const bool military = game_.rules().units[static_cast<std::size_t>(u.type)].attack > 0;
    const auto& square = game_.state().map.at(u.x, u.y);
    const bool inColony = game_.colonyAt(u.x, u.y).has_value();

    if (ship) {
        set(kBuildColony, EntryState::Hidden);
        set(kJoinColony, EntryState::Hidden);
    } else {
        set(inColony ? kBuildColony : kJoinColony, EntryState::Hidden);
        const int row = inColony ? kJoinColony : kBuildColony;
        // Seen on mountains and arctic: Build Colony is offered all the
        // same, and picking it says why not.
        set(row, inColony || game_.canFoundColony(active) != FoundResult::NotAColonist ? EntryState::Enabled
                                                                                       : EntryState::Disabled);
    }
    // Seen for a pioneer: Clear Forest on a forest, Plow Fields elsewhere,
    // neither on hills or mountains; both stay up on a plowed square or a
    // road, and picking them then says so.
    set(square.isForest() ? kPlow : kClearForest, EntryState::Hidden);
    const bool plowable = !ship && game_.canPlow(u.x, u.y);
    set(square.isForest() ? kClearForest : kPlow,
        pioneer && plowable ? EntryState::Enabled : pioneer ? EntryState::Hidden : EntryState::Disabled);
    set(kRoad, pioneer ? EntryState::Enabled : EntryState::Disabled);
    const bool cargo = (ship || wagon) && u.holds > 0;
    // Seen for a pioneer: no Load or Unload for those who carry nothing.
    set(kLoad, !(ship || wagon) ? EntryState::Hidden : inColony ? EntryState::Enabled : EntryState::Disabled);
    set(kUnload, !(ship || wagon) ? EntryState::Hidden : cargo && inColony ? EntryState::Enabled : EntryState::Disabled);
    set(kPillage, military && !ship ? EntryState::Enabled : EntryState::Hidden);
    set(kGoToPort, ship ? EntryState::Enabled : EntryState::Hidden);
    set(kGoToPlace, ship ? EntryState::Hidden : EntryState::Enabled);
    set(kTradeRoute, ship || wagon ? EntryState::Enabled : EntryState::Hidden);
    set(kReturnToEurope, !ship ? EntryState::Hidden
                         : square.terrain == kSeaLaneTerrain ? EntryState::Enabled
                                                             : EntryState::Disabled);
    set(kDump, !(ship || wagon) ? EntryState::Hidden : cargo ? EntryState::Enabled : EntryState::Disabled);
    return states;
}

void PlayScreen::openMenu(int index, bool keyboard) {
    if (art_ == nullptr || index < 0 || index >= static_cast<int>(art_->menus.size()) || slide_ || dialog_ ||
        colonyScreen_ || europeScreen_) {
        return;
    }
    OpenMenu open;
    open.menu = index;
    open.states = menuStates(index);
    menu_ = std::move(open);
    if (keyboard) {
        stepMenu(1);
    }
}

void PlayScreen::stepMenu(int delta) {
    if (!menu_) {
        return;
    }
    const int n = static_cast<int>(menu_->states.size());
    int i = menu_->selected;
    for (int tries = 0; tries < n; ++tries) {
        i = i < 0 ? (delta > 0 ? 0 : n - 1) : (i + delta + n) % n;
        if (menu_->states[static_cast<std::size_t>(i)] == EntryState::Enabled) {
            menu_->selected = i;
            return;
        }
    }
}

bool PlayScreen::handleMenu(Command command) {
    const int count = static_cast<int>(art_->menus.size());
    switch (command) {
    case Command::MoveN:
    case Command::Up:
        stepMenu(-1);
        return true;
    case Command::MoveS:
    case Command::Down:
        stepMenu(1);
        return true;
    case Command::MoveW:
    case Command::Left:
        openMenu((menu_->menu + count - 1) % count, true);
        return true;
    case Command::MoveE:
    case Command::Right:
        openMenu((menu_->menu + 1) % count, true);
        return true;
    case Command::Confirm:
        return pickMenuEntry(menu_->selected);
    case Command::Cancel:
    case Command::Menu:
        menu_.reset();
        return true;
    default:
        return true;
    }
}

bool PlayScreen::clickMenu(int x, int y) {
    if (art_ == nullptr || slide_ || dialog_ || colonyScreen_ || europeScreen_) {
        return false;
    }
    const int title = menuTitleAt(*art_, art_->menus, x, y);
    if (title >= 0) {
        if (menu_ && menu_->menu == title) {
            menu_.reset();
        } else {
            openMenu(title, false);
        }
        return true;
    }
    if (!menu_) {
        return false;
    }
    const auto& def = art_->menus[static_cast<std::size_t>(menu_->menu)];
    const int entry = menuEntryAt(*art_, def, *menu_, x, y);
    const Rect box = menuBox(*art_, def, *menu_);
    if (entry >= 0) {
        if (!pickMenuEntry(entry)) {
            quitToLoad_ = true;
        }
    } else if (x < box.x || x >= box.x + box.w || y < box.y || y >= box.y + box.h) {
        menu_.reset(); // a click outside closes the menu
    }
    return true;
}

void PlayScreen::menuLetter(char letter) {
    if (!menu_ || art_ == nullptr) {
        return;
    }
    const auto& def = art_->menus[static_cast<std::size_t>(menu_->menu)];
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(letter)));
    for (int i = 0; i < static_cast<int>(def.entries.size()); ++i) {
        if (def.entries[static_cast<std::size_t>(i)].hotkey == upper &&
            menu_->states[static_cast<std::size_t>(i)] == EntryState::Enabled) {
            menu_->selected = i;
            if (!pickMenuEntry(i)) {
                quitToLoad_ = true;
            }
            return;
        }
    }
}

void PlayScreen::dialogLetter(char letter) {
    if (!dialog_ || !dialog_->checkbox) {
        return;
    }
    // Seen: the letter moves the bar to its option and turns it over.
    const int option = checkboxForLetter(*dialog_, letter);
    if (option >= 0) {
        dialog_->selected = option;
        handleCheckboxes(Command::Skip);
    }
}

bool PlayScreen::handleCheckboxes(Command command) {
    auto& d = *dialog_;
    switch (command) {
    case Command::MoveN:
    case Command::Up:
        stepSelection(d, -1);
        return true;
    case Command::MoveS:
    case Command::Down:
        stepSelection(d, 1);
        return true;
    case Command::Skip: {
        // Space turns the option under the bar on or off.
        const auto at = static_cast<std::size_t>(d.selected);
        if (at < d.checked.size()) {
            d.checked[at] = !d.checked[at];
        }
        return true;
    }
    case Command::Confirm:
        // Return keeps the changes (seen); Escape drops them.
        if (question_ == "SOUNDOPTIONS") {
            sound::SoundOptions options;
            options.background = d.checked.size() > 0 && d.checked[0];
            options.events = d.checked.size() > 1 && d.checked[1];
            options.effects = d.checked.size() > 2 && d.checked[2];
            game_.setSoundOptionBits(options.toSave(game_.soundOptionBits()));
            if (music_ != nullptr) {
                music_->options = options;
            }
        }
        break;
    case Command::Cancel:
        break;
    default:
        return true;
    }
    dialog_.reset();
    question_.clear();
    showNotices();
    return true;
}

void PlayScreen::setMusic(sound::BackgroundMusic* music) {
    music_ = music;
}

void PlayScreen::playMusic(const std::vector<MusicCue>& cues) {
    if (music_ == nullptr) {
        return;
    }
    for (const auto& cue : cues) {
        switch (cue.kind) {
        case MusicCue::Kind::SetMood:
            music_->setMood(cue.value);
            break;
        case MusicCue::Kind::ChangeMood:
            music_->changeMood(cue.value);
            break;
        case MusicCue::Kind::MoodEvent:
            music_->moodEvent(cue.value);
            break;
        case MusicCue::Kind::Pick:
            music_->pick(cue.value);
            break;
        case MusicCue::Kind::Sound:
            music_->sound(cue.value);
            break;
        }
    }
}

void PlayScreen::showSoundOptions() {
    ask("SOUNDOPTIONS", -1);
    if (!dialog_) {
        return;
    }
    const auto options = sound::SoundOptions::fromSave(game_.soundOptionBits());
    dialog_->checked = {options.background, options.events, options.effects};
    dialog_->checked.resize(dialog_->options.size());
}

void PlayScreen::showPickMusic() {
    // Seen: the bar starts on the tune that is playing.
    ask("PICKMUSIC", -1);
    if (!dialog_) {
        return;
    }
    choices_.clear();
    for (int i = 0; i < static_cast<int>(dialog_->options.size()); ++i) {
        choices_.push_back(i);
    }
    const int playing = music_ != nullptr ? music_->current() : 0;
    for (int tune = 1; tune <= sound::kTuneCount; ++tune) {
        if (sound::tuneCommand(tune) == playing) {
            dialog_->selected = tune <= 12 ? tune - 1 : tune <= 18 ? 12 : tune <= 22 ? 13 : 14;
            if (tune <= 12) {
                dialog_->marked = tune - 1; // seen: the tune playing in gold
            }
            break;
        }
    }
    dialog_->selected = std::min(dialog_->selected, static_cast<int>(dialog_->options.size()) - 1);
}

void PlayScreen::pickTune(int tune) {
    if (music_ != nullptr) {
        music_->pick(sound::tuneCommand(tune));
    }
}

void PlayScreen::notYet() {
    say("Not in Viceroy yet.");
}

bool PlayScreen::pickMenuEntry(int entry) {
    if (!menu_ || entry < 0 || entry >= static_cast<int>(menu_->states.size()) ||
        menu_->states[static_cast<std::size_t>(entry)] != EntryState::Enabled) {
        return true;
    }
    const std::string section = art_->menus[static_cast<std::size_t>(menu_->menu)].section;
    menu_.reset();
    if (section == "GAME") {
        switch (entry) {
        case game_menu::kSoundOptions:
            showSoundOptions();
            return true;
        case game_menu::kPickMusic:
            showPickMusic();
            return true;
        case game_menu::kSave:
            if (save()) {
                say("Game saved.");
            }
            return true;
        case game_menu::kLoad:
            return false;
        case game_menu::kExit:
            quit_ = true;
            return true;
        default:
            notYet();
            return true;
        }
    }
    if (section == "VIEW") {
        switch (entry) {
        case view_menu::kMovePieces:
            return handle(Command::MoveMode);
        case view_menu::kViewPieces:
            return handle(Command::ViewMode);
        case view_menu::kCentre:
            return handle(Command::Centre);
        default:
            notYet();
            return true;
        }
    }
    if (section == "REPORTS") {
        switch (entry) {
        case reports_menu::kCongress:
            return handle(Command::CongressReport);
        default:
            notYet();
            return true;
        }
    }
    if (section == "ORDERS") {
        using namespace orders_menu;
        switch (entry) {
        case kActivate:
            return handle(Command::MoveMode);
        case kWait:
            return handle(Command::Wait);
        case kFortify:
            return handle(Command::Fortify);
        case kSentry:
            return handle(Command::Sentry);
        case kBuildColony:
        case kJoinColony:
            return handle(Command::BuildColony);
        case kClearForest:
        case kPlow:
            work(SaveUnit::kPlow);
            return true;
        case kRoad:
            work(SaveUnit::kRoad);
            return true;
        case kNoOrders:
            return handle(Command::Skip);
        case kGoToPort:
        case kGoToPlace:
            return handle(Command::GoTo);
        case kReturnToEurope: {
            const int active = game_.activeUnit();
            if (active >= 0 && game_.sailToEurope(active)) {
                say("Sailing to Europe.");
                advance();
            }
            return true;
        }
        default:
            notYet();
            return true;
        }
    }
    notYet();
    return true;
}

} // namespace viceroy
