// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/ui/play.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/data/text_table.hpp"
#include "viceroy/game/colony.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game/natives.hpp"
#include "viceroy/game/sounds.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/render/report_view.hpp"
#include "viceroy/render/trade_route_view.hpp"
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
constexpr std::uint8_t kBlack = 0x00;
// Rows of LABELS.TXT for the custom house's sales.
constexpr int kLabelSells = 47; // @MISC "sells"
constexpr int kLabelFor = 48;   // @MISC "for"
constexpr int kLabelTax = 17;   // @CMESSAGE "% Tax:"
constexpr int kLabelNet = 18;   // @CMESSAGE ". Net:"
constexpr int kLabelNothingToDump = 32; // @MISC "Nothing"
// Rows of @CMESSAGE for goods moved on the colony screen.
constexpr int kLabelMovedTo = 2;
constexpr int kLabelNoRoom = 4;
constexpr int kLabelNotEnough = 6;
constexpr int kLabelToFill = 7;
constexpr int kLabelNo = 9;
constexpr int kLabelInHold = 10;
constexpr int kLabelLoadingGoods = 15;
constexpr int kLabelUnloadingGoods = 16;

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
        if (!wellInside(view_, u.x, u.y)) {
            centreOn(view_, game_, u.x, u.y);
        }
        view_.endOfTurn = false;
        // Read from the original: with Tutorial Hints on, each time a unit
        // is up for orders the advisers may have a lesson about it.
        if (auto hint = game_.unitHint(index)) {
            tell(std::move(*hint));
        }
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
        if (!wellInside(view_, u.x, u.y)) {
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
        if (game_.isShip(index) && map.inside(tx, ty) && map.at(tx, ty).isLand()) {
            // Read from the original: the last unit aboard with moves left
            // may go ashore; the question names a river when there is one
            // ("Our ships cannot navigate up this river").
            int lander = -1;
            for (const int p : aboard) {
                if (game_.movesLeft(p) > 0) {
                    lander = p;
                }
            }
            if (lander >= 0) {
                landfall_ = Landfall{lander, step};
                ask(map.at(tx, ty).river() ? "LANDFALL2" : "LANDFALL", index);
                break;
            }
            say("");
            break;
        }
        if (game_.isShip(index) && map.inside(tx, ty) && map.at(tx, ty).isWater() &&
            (map.at(tx, ty).region & 0x0F) != 1 && !game_.isDamagedShip(index)) {
            // "Ship units cannot enter inland lake squares."
            tell({"SHIPLAKE", {}});
            break;
        }
        say("Can't go there.");
        break;
    }
    case MoveResult::Occupied: {
        const auto& u = game_.unit(index);
        const int tx = u.x + step.dx;
        const int ty = u.y + step.dy;
        const int defender = game_.canAttack(index) ? game_.defenderAt(tx, ty, index) : -1;
        const auto colony = game_.colonyAt(tx, ty);
        const auto settlement = game_.settlementAt(tx, ty);
        const bool foreignColony =
            colony && game_.state().colonies[static_cast<std::size_t>(*colony)].nation < kEuropeanNations;
        const auto& from = game_.state().map.at(u.x, u.y);
        if (!game_.isShip(index) && from.isWater()) {
            // Read from the original: "Land units cannot enter an enemy
            // occupied square from on board a ship."
            tell({"LANDFIRST", {}});
        } else if (game_.isShip(index) && !game_.canAttack(index) && game_.state().map.at(tx, ty).isWater()) {
            // "Only Privateers and Frigates can attack enemy ships."
            tell({"SHIPCOMBAT", {}});
        } else if (settlement) {
            enterVillage(index, *settlement);
        } else if (colony && game_.unit(index).type == unit_type::kScout &&
                   game_.state().colonies[static_cast<std::size_t>(*colony)].nation < kEuropeanNations) {
            // Read from the original: a scout asks what to do there.
            scoutColony_ = *colony;
            ask("SCOUTCOLONY", index, {game_.state().colonies[static_cast<std::size_t>(*colony)].name});
            if (dialog_) {
                placeFrontiersmanDialog(*dialog_);
            }
        } else if (colony && game_.state().colonies[static_cast<std::size_t>(*colony)].nation < kEuropeanNations &&
                   game_.unit(index).type < static_cast<int>(game_.rules().units.size()) &&
                   game_.rules().units[static_cast<std::size_t>(game_.unit(index).type)].cargoSlots > 0) {
            // Read from the original: wagon trains and ships trade there.
            foreignTrade(index, *colony);
        } else if (foreignColony && game_.independenceDeclared() &&
                   game_.state().colonies[static_cast<std::size_t>(*colony)].nation != game_.crownNation()) {
            // "Foreign colonies cannot be attacked during the War of
            // Independence."
            tell({"NOWARSDURINGREV", {}});
        } else if (defender >= 0 && !settlement) {
            startAttack(index, defender);
        } else if (colony && !game_.isShip(index) && game_.canAttack(index) && game_.colonyUndefended(*colony)) {
            startAttack(index, -1, *colony);
        } else if (!game_.isShip(index) && !game_.canAttack(index) &&
                   (defender >= 0 || game_.defenderAt(tx, ty, index) >= 0 || foreignColony)) {
            // "That unit type cannot attack."
            tell({"CANNOTATTACK", {}});
        } else {
            say("Someone else is there.");
        }
        break;
    }
    case MoveResult::HighSeas:
        say("");
        if (game_.independenceDeclared()) {
            tell({"EUROPENOTLEAVE", {}}); // no sailing to Europe during the war
            break;
        }
        ask("SAILHOME", index);
        if (dialog_) {
            placeNavalDialog(*dialog_);
        }
        break;
    }
}

void PlayScreen::type(char letter) {
    if (typing()) {
        typeInto(*dialog_, letter);
    }
}

void PlayScreen::eraseTyped() {
    if (typing()) {
        eraseInput(*dialog_);
    }
}

void PlayScreen::buildColony(int unit) {
    // Read from the original: what keeps the unit from founding a colony
    // there, then the warnings, then the name.
    const auto& save = game_.state();
    switch (game_.canFoundColony(unit)) {
    case FoundResult::Founded:
        founding_ = Founding{unit, game_.foundAdvice(unit)};
        nextFoundingStep();
        return;
    case FoundResult::NotAColonist:
        say("Only colonists can build a colony.");
        return;
    case FoundResult::NotOnLand:
    case FoundResult::InColony:
        return;
    case FoundResult::AtSea:
        // "It may surprise you to learn that colonies cannot be built at sea."
        ask("SEACOLONY", unit);
        if (dialog_) {
            placeNavalDialog(*dialog_);
        }
        return;
    case FoundResult::TooClose: {
        // "This land is too near to Jamestown for a new colony."
        const int near = game_.colonyNextTo(unit);
        ask("TOONEAR", unit, {near >= 0 ? save.colonies[static_cast<std::size_t>(near)].name : ""});
        if (dialog_) {
            placeBuyDialog(*dialog_);
        }
        return;
    }
    case FoundResult::Mountains:
        ask("TOOMOUNTAIN", unit);
        if (dialog_) {
            placeFrontiersmanDialog(*dialog_);
        }
        return;
    case FoundResult::TooMany:
        tell({"TOOMANYCOLONIES", {}});
        return;
    }
}

void PlayScreen::nextFoundingStep() {
    if (!founding_) {
        return;
    }
    auto& f = *founding_;
    const auto warn = [&](const char* section) {
        ask(section, f.unit);
        if (dialog_) {
            placeFrontiersmanDialog(*dialog_);
        }
    };
    if (f.advice.noPort) {
        f.advice.noPort = false;
        warn("NOPORT");
        return;
    }
    if (f.advice.fewSquares) {
        f.advice.fewSquares = false;
        warn("TUTNOSPACES");
        return;
    }
    if (f.advice.noForest) {
        f.advice.noForest = false;
        warn("TUTNOLUMBER");
        return;
    }
    // "What shall we name this colony?" with the next name of the list,
    // from the woman of the buy box (MSS5).
    const auto& u = game_.unit(f.unit);
    centreOn(view_, game_, u.x, u.y);
    ask("COLONY", f.unit);
    if (dialog_ && art_ != nullptr) {
        makeInput(*art_, *dialog_, game_.nextColonyName(u.nation));
        placeNameDialog(*dialog_);
    }
}

void PlayScreen::foundNamedColony(int unit, const std::string& name) {
    founding_.reset();
    if (game_.foundColony(unit, name) != FoundResult::Founded) {
        return;
    }
    // The first colony's woodcut comes before its screen.
    for (auto& notice : game_.takeNotices()) {
        notices_.push_back(std::move(notice));
    }
    openColony(static_cast<int>(game_.state().colonies.size()) - 1);
    game_.setActiveUnit(-1);
    activate(game_.nextUnitNeedingOrders(-1));
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
    // Read from the original: with Tutorial Hints on, the screen tells of
    // a shortage of food, how to give jobs and how to load a ship.
    if (auto food = game_.foodHint(index)) {
        notices_.push_back(std::move(*food));
    }
    for (auto& hint : game_.colonyScreenHints(index)) {
        notices_.push_back(std::move(hint));
    }
    showNotices();
}

void PlayScreen::pointAt(int x, int y) {
    if (!colonyScreen_) {
        return;
    }
    const auto& save = game_.state();
    const auto index = static_cast<std::size_t>(colonyScreen_->colony);
    if (index >= save.colonies.size()) {
        return;
    }
    const int place = townPlaceAt(save, save.colonies[index], x, y);
    if (place >= 0) {
        colonyScreen_->townLabel = place;
    }
    colonyScreen_->warehouseLabel = warehouseGoodAt(x, y);
    // Measured: the ship's frame turns white while the pointer is in the
    // harbour, and green again over the warehouse.
    if (inHarbour(x, y) && !shipsInPort(game_, save.colonies[index]).empty()) {
        colonyScreen_->harbourChosen = true;
    } else if (colonyScreen_->warehouseLabel >= 0) {
        colonyScreen_->harbourChosen = false;
    }
}

void PlayScreen::openEurope() {
    EuropeViewState state;
    const auto ships = unitsInEurope(game_.state(), game_.rules(), game_.human(), EuropePlace::InPort, true);
    state.ship = ships.empty() ? -1 : ships.front();
    europeScreen_ = state;
    // Read from the original: with Tutorial Hints on, the first visit
    // explains the screen.
    if (auto hint = game_.europeHint()) {
        tell(std::move(*hint));
    }
}

void PlayScreen::enterVillage(int unit, int settlement) {
    if (art_ == nullptr) {
        return;
    }
    say("");
    const auto& save = game_.state();
    const int tribe = tribeOf(save.settlements[static_cast<std::size_t>(settlement)]);
    const int nation = game_.unit(unit).nation;
    game_.villageEntryMood(nation, tribe);
    playMusic(game_.takeMusicCues());
    // "a Camp of Sioux": the settlement's word for the tribe's level.
    std::string place;
    if (const auto* levels = art_->names.find("LEVELS")) {
        const auto row = static_cast<std::size_t>(tribeLevel(save, tribe));
        const auto fields = row < levels->lines.size() ? splitFields(levels->lines[row]) : std::vector<std::string>{};
        place = fields.size() > 1 ? fields[1] : "";
    }
    // Read from the original: a ship is sent away before the menu by a
    // tribe that has not met the nation on land, or that is mad at it.
    if (const auto refusal = game_.shipTradeRefusal(unit, settlement); !refusal.empty()) {
        village_ = settlement;
        ask(refusal, unit, {adjective(kEuropeanNations + tribe)});
        if (dialog_ && refusal == "MADATSHIPS") {
            placeNative(tribe);
        }
        question_ = "LESSONDONE";
        return;
    }
    // Read from the original: the first time a unit of the player's
    // enters a village, its woodcut comes before the greeting.
    if (nation == game_.human()) {
        if (auto picture = game_.woodcut(7)) {
            villageAfterWoodcut_ = std::make_pair(unit, settlement);
            tell(std::move(*picture));
            return;
        }
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

void PlayScreen::foreignTrade(int unit, int colony) {
    const int holds = game_.startForeignTrade(unit, colony);
    if (holds == 1) {
        game_.offerForeignTrade(unit, colony, 0);
    } else if (holds > 1) {
        // "Which cargo shall we offer to trade, Your Excellency?"
        ask("TRADEWHICH", unit);
        if (dialog_) {
            const auto& u = game_.unit(unit);
            for (int k = 0; k < u.holds; ++k) {
                addOption(*dialog_, std::to_string(holdAmount(u, k)) + " " + goodName(holdGood(u, k)));
            }
            addOption(*dialog_, textLine(art_->labelText, "MISC", 32, "Nothing"));
            placeCentredDialog(*dialog_);
            question_ = "FOREIGNTRADEWHICH";
            foreignTradeColony_ = colony;
            return;
        }
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

std::string PlayScreen::unitTypeName(int unit) const {
    const int type = game_.unit(unit).type;
    const auto& units = game_.rules().units;
    return type >= 0 && static_cast<std::size_t>(type) < units.size() ? units[static_cast<std::size_t>(type)].name
                                                                      : std::string();
}

std::string PlayScreen::cargoLabel(int line) const {
    return art_ != nullptr ? textLine(art_->labelText, "CMESSAGE", line, "") : std::string();
}

std::string PlayScreen::goodName(int good) const {
    const auto& cargo = game_.rules().cargo;
    return good >= 0 && static_cast<std::size_t>(good) < cargo.size() ? cargo[static_cast<std::size_t>(good)].name : "";
}

void PlayScreen::tradeWithVillage(int unit) {
    // Read from the original: a tense tribe may take the wagon train, send
    // it away or trade with it grudgingly.
    const int tribe = tribeOf(game_.state().settlements[static_cast<std::size_t>(village_)]);
    const auto welcome = game_.wagonTradeWelcome(unit, village_);
    if (welcome.empty()) {
        startTrade(unit);
        return;
    }
    const bool gone = welcome == "KILLWAGONS";
    ask(welcome, gone ? -1 : unit, {adjective(kEuropeanNations + tribe)});
    if (!dialog_) {
        if (welcome == "GRUDGEWAGONS") {
            startTrade(unit);
        }
        return;
    }
    if (!gone) {
        placeNative(tribe);
    }
    if (welcome == "GRUDGEWAGONS") {
        questionUnit_ = unit;
    } else {
        question_ = "LESSONDONE";
    }
}

void PlayScreen::startTrade(int unit) {
    game_.villageVisitMood(game_.unit(unit).nation,
                           tribeOf(game_.state().settlements[static_cast<std::size_t>(village_)]));
    playMusic(game_.takeMusicCues());
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
    if (d.woodcut > 0) {
        dialog_.reset();
        question_.clear();
        if (const auto visit = std::exchange(villageAfterWoodcut_, std::nullopt)) {
            enterVillage(visit->first, visit->second);
        }
        showNotices();
        return true;
    }
    if (d.checkbox) {
        return handleCheckboxes(command);
    }
    if (d.input) {
        // Return takes what was typed, Escape gives up (seen).
        if (command != Command::Confirm && command != Command::Cancel) {
            return true;
        }
        typed_ = d.inputText;
        const std::string question = question_;
        const int unit = questionUnit_;
        dialog_.reset();
        answer(question, unit, command == Command::Confirm ? 0 : -1);
        showNotices();
        return true;
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
        if (question_ == "TAXOPTIONS" || question_ == "KISSUP" || question_ == "MERCENARIES" ||
            question_.rfind("LANDFALL", 0) == 0) {
            // Escape kisses the ring, or refuses to pay: only the second
            // choice holds a party or pays (or makes landfall).
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
        if (question_ == "SAILPORT" || question_ == "TRAVELPLACE" || question_.rfind("PICK", 0) == 0 ||
            question_ == "SAVEGAME" || question_ == "LOADGAME" ||
            (question_.rfind("ROUTE", 0) == 0 && question_ != "ROUTESURE")) {
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
    // Read from the original: during the war of independence the player's
    // side is "Rebel" and the Crown's "Tory" (LABELS.TXT).
    if (art_ != nullptr && (game_.state().header[18] & 0x01) != 0 && nation >= 0) {
        if (nation == game_.human()) {
            return textLine(art_->labelText, "MISC", 69, "Rebel");
        }
        if (nation == game_.crownNation()) {
            return textLine(art_->labelText, "MISC", 70, "Tory");
        }
    }
    if (art_ != nullptr) {
        if (const auto* s = art_->names.find("NATIONALITY")) {
            if (nation >= 0 && static_cast<std::size_t>(nation) < s->lines.size()) {
                return s->lines[static_cast<std::size_t>(nation)];
            }
        }
    }
    return "";
}

std::string PlayScreen::people(int nation) const {
    // The same, but "Rebels" and "Tories" during the war (read from the
    // original).
    if (art_ != nullptr && (game_.state().header[18] & 0x01) != 0 && nation >= 0) {
        if (nation == game_.human()) {
            return textLine(art_->labelText, "MISC", 86, "Rebels");
        }
        if (nation == game_.crownNation()) {
            return textLine(art_->labelText, "MISC", 87, "Tories");
        }
    }
    return adjective(nation);
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
    if (them >= kEuropeanNations && game_.asksBeforeAttackingTribe(attacker, them - kEuropeanNations)) {
        // "Shall we attack the Sioux, Your Excellency?"
        ask("WHACKINDIANS", attacker, {adjective(them)});
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
    if (ending_ != 0) {
        return; // the rest of the news waits for the end of the scoring
    }
    if (!dialog_ && notices_.empty() && game_.gameOver()) {
        // The game is over: as the original, the program ends.
        if (ending_ == 0) {
            quit_ = true;
        }
        return;
    }
    if (!dialog_ && retiring_) {
        // Read from the original: the leader who retires is scored right
        // after the retirement is told, before the rest of the news (SCORED).
        retiring_ = false;
        startEnding(false);
        return;
    }
    if (!dialog_ && notices_.empty()) {
        view_.shownYear = -1; // the new turn's date
        if (advanceAfterNotices_) {
            advanceAfterNotices_ = false;
            advance();
        }
        if (colonyAfterNotices_ >= 0) {
            openColony(std::exchange(colonyAfterNotices_, -1));
        }
        if (const int ship = std::exchange(europeAfterNotices_, -1); ship >= 0 && ship < game_.unitCount()) {
            openEurope();
            if (europeScreen_ && europePlace(game_.unit(ship)) == EuropePlace::InPort) {
                europeScreen_->ship = ship;
            }
        }
    }
    if (dialog_ || notices_.empty() || art_ == nullptr) {
        return;
    }
    GameNotice notice = notices_.front();
    notices_.erase(notices_.begin());
    if (notice.section == "RETIRING" || notice.section == "RETIRING2") {
        retiring_ = true;
    }
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
    for (const auto& [string, nation] : notice.peoples) {
        if (string >= 0) {
            if (notice.strings.size() <= static_cast<std::size_t>(string)) {
                notice.strings.resize(static_cast<std::size_t>(string) + 1);
            }
            notice.strings[static_cast<std::size_t>(string)] = people(nation);
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
    for (const auto& [string, nation] : notice.sentenceCountries) {
        if (string >= 0) {
            if (notice.strings.size() <= static_cast<std::size_t>(string)) {
                notice.strings.resize(static_cast<std::size_t>(string) + 1);
            }
            const std::string country = textLine(art_->names, "COUNTRY", nation, "");
            notice.strings[static_cast<std::size_t>(string)] = nation == 3 ? "The " + country : country;
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
    if (notice.section == "CUSTOMSALE" && notice.strings.size() >= 2 && notice.numbers.size() >= 4) {
        // Read from the original: the sale is written over the menu bar and
        // the turn goes on. "Curacao sells 143 Ore for 715." and, with the
        // tax, " 2% Tax: 14. Net: 701".
        std::string text = notice.strings[0] + " " + textLine(art_->labelText, "MISC", kLabelSells, "") + " " +
                           std::to_string(notice.numbers[0]) + " " + notice.strings[1] + " " +
                           textLine(art_->labelText, "MISC", kLabelFor, "") + " " + std::to_string(notice.numbers[1]) +
                           ".";
        if (notice.value >= 0) {
            text += " " + std::to_string(notice.value) + textLine(art_->labelText, "CMESSAGE", kLabelTax, "") + " " +
                    std::to_string(notice.numbers[2]) + textLine(art_->labelText, "CMESSAGE", kLabelNet, "") + " " +
                    std::to_string(notice.numbers[3]);
        }
        view_.topLine = text;
        showNotices();
        return;
    }
    if (notice.section == "SIGNING") {
        // Read from the original: the leader's name is written on the
        // declaration, then the map comes back at once.
        if (!notice.strings.empty()) {
            auto strokes = signatureStrokes(notice.strings[0], *art_);
            if (!strokes.empty()) {
                signing_ = Signing{std::move(strokes)};
            }
        }
        if (!signing_) {
            showNotices();
        }
        return;
    }
    if (notice.section == "WOODCUT") {
        // Read from the original: the picture fills the screen until a key
        // or a click.
        dialog_ = Dialog{};
        dialog_->woodcut = notice.value;
        question_ = "WOODCUT";
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
    if (notice.section == "LOSTCITY0" || notice.section == "RECRUITCHOOSE") {
        // A Fountain of Youth: "Which of the following individuals shall
        // we recruit?", one of the three on the docks, for free. With
        // William Brewster the religious adviser asks the same when the
        // crosses bring a newcomer (read from the original).
        const auto& nation = game_.state().nations[static_cast<std::size_t>(game_.human())];
        const bool brewster = notice.section == "RECRUITCHOOSE";
        Dialog d = brewster ? makeDialog(*art_, notice.section, {textLine(art_->names, "HOMEPORT", game_.human(), "")},
                                         {}, game_.human())
                            : makeDialog(*art_, "LOSTCITY0");
        choices_.clear();
        for (std::size_t slot = 0; slot < nation.recruits.size(); ++slot) {
            const auto job = static_cast<std::size_t>(professionJob(nation.recruits[slot]));
            addOption(d, job < game_.rules().jobs.size() ? game_.rules().jobs[job].expertName : "?");
            choices_.push_back(static_cast<int>(slot));
        }
        d.selected = 0;
        if (brewster) {
            placeReligiousDialog(d);
        } else {
            placeFrontiersmanDialog(d);
        }
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
        // Read from the original: with Tutorial Hints on, the naval adviser
        // follows the first of these with how to fetch them.
        if (auto hint = game_.docksHint(notice.value)) {
            notices_.insert(notices_.begin(), std::move(*hint));
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
    if (notice.section == "LANDHO") {
        // "Land Ho! What shall we call this new land?", with the name the
        // land has so far (read from the original).
        ask(notice.section, -1);
        if (dialog_) {
            makeInput(*art_, *dialog_, game_.state().players[static_cast<std::size_t>(game_.human())].country);
            placeTutorialDialog(*dialog_, kNavalAdvisor);
        }
        return;
    }
    if (notice.section.rfind("TUTORIAL", 0) == 0) {
        if (notice.section == "TUTORIAL6") {
            // %STRING2 of the cargo hint is the home port.
            if (notice.strings.size() < 3) {
                notice.strings.resize(3);
            }
            notice.strings[2] = textLine(art_->names, "HOMEPORT", game_.human(), "");
        }
        ask(notice.section, -1, notice.strings, game_.human(), notice.numbers);
        if (dialog_) {
            // The adviser by the number of its picture (MSSn), each where
            // the other news of that adviser stands (measured with the
            // naval and colonial advisers). A hint without an adviser is a
            // plain box where the text's @y puts it (measured on the
            // Europe screen's).
            switch (notice.value) {
            case 0:
                placeTutorialDialog(*dialog_, kNavalAdvisor);
                break;
            case 1:
                placeColonialDialog(*dialog_);
                break;
            case 2:
                placeForeignNewsDialog(*dialog_);
                break;
            case 3:
                placeFrontiersmanDialog(*dialog_);
                break;
            case 4:
                placeReligiousDialog(*dialog_);
                break;
            case 5:
                placeTutorialDialog(*dialog_, kBuyAdvisor);
                break;
            default:
                if (dialog_->sectionX >= 0) {
                    dialog_->x = dialog_->sectionX; // guess: as its @y, the food hint's @x
                }
                break;
            }
            dropTallAdviser(*dialog_);
        }
        return;
    }
    ask(notice.section, -1, notice.strings, -1, notice.numbers);
    if (dialog_) {
        // Measured: battles are told by the colonial advisor, the work of
        // pioneers by the frontiersman.
        constexpr std::string_view kBattles[] = {
            "DEMOTE", "COLONISTCAPTURE", "COLONISTCAPTURE2", "WAGONCAPTURE", "LOOTCAPTURE", "ARTILLERY", "ARTILLERY2",
            "VETERAN", "VALOR", "CAPTURED", "CAPTURED3", "LOOT", "LOOT2", "INDIANWINCOLONY", "INDIANBURNCOLONY",
            "INDIANSLAVES", "INDIANWARFARE", "INDIANBURN", "TEAPARTY", "KINGBUY", "REBELUP", "REBELUP50", "REBELDOWN",
            "MERCENARIES", "MERCS", "NOMAYORSDURINGREV",
            // The war of independence (placement not measured yet).
            "ALREADYREVOLUTION", "TOOTORY", "MULTIREV", "DECLARE", "INDEPENDENCE", "SEIZURE", "MOBILIZE", "MOBILIZE2",
            "INTERVENE", "INTERVENTION", "AMBUSHHINT", "CONSIDER", "NOWARSDURINGREV", "EUROPENOTAVAIL",
            "EUROPENOTLEAVE", "NOCOLONIESEITHER", "INVASION", "SEIZURESEA", "SEIZURELAND", "TORYUPRISING",
            "INDIANGRUDGE", "HOWTOWIN", "WINNING", "LOSING1", "LOSING2", "LOSING3", "WARN1", "WARN2", "WARN3",
            // The end of the game (placement not measured yet).
            "LOSENOCOLONIES", "SOONRETIRING0", "SOONRETIRING1", "RETIRING", "RETIRING2", "SCORED", "OTHERGRANTED",
            "OTHERMIGHT", "OTHERLESS"};
        if (std::find(std::begin(kBattles), std::end(kBattles), notice.section) != std::end(kBattles)) {
            placeMapDialog(*dialog_);
        } else if (notice.section == "DEADCONVERTS") {
            placeReligiousDialog(*dialog_); // read from the original
        } else if (notice.section == "CAPTURED2") {
            placeForeignNewsDialog(*dialog_); // read from the original: the man in the home port
        } else if (notice.section == "LOOTCASH" || notice.section == "CASHTREASURE") {
            placeTradeDialog(*dialog_); // read from the original: the trade adviser
        } else {
            placeFrontiersmanDialog(*dialog_);
        }
    }
}

void PlayScreen::askLandClaim(const std::string& section, const LandClaim& claim, int unit) {
    // Read from the original: the tribe's figure asks; "We offer you ..."
    // is greyed when the treasury is short.
    const int tribeNation = kEuropeanNations + claim.tribe;
    ask(section, unit, {adjective(tribeNation)}, -1, {0, claim.price});
    if (!dialog_) {
        return;
    }
    landClaim_ = claim;
    const int nation = unit >= 0 ? game_.unit(unit).nation : game_.human();
    if (dialog_->disabled.size() < dialog_->options.size()) {
        dialog_->disabled.resize(dialog_->options.size());
    }
    if (dialog_->disabled.size() > 1 && game_.state().nations[static_cast<std::size_t>(nation)].gold < claim.price) {
        dialog_->disabled[1] = true;
    }
    placeNative(claim.tribe);
}

bool PlayScreen::landBought(const LandClaim& claim, int nation, int unit) {
    if (!game_.buyLand(claim, nation)) {
        return false;
    }
    // "Very well, we withdraw our objection." The work goes on after it.
    ask("INDIANBRIBE", unit);
    placeNative(claim.tribe);
    return dialog_.has_value();
}

void PlayScreen::putToLandWork() {
    if (colonyScreen_) {
        doColonyMove(landWho_, landJob_, landSquare_);
    }
}

void PlayScreen::work(int orders) {
    const int active = game_.activeUnit();
    if (active < 0) {
        return;
    }
    if (game_.canStartWork(active, orders) == WorkResult::Started && game_.unit(active).nation == game_.human()) {
        if (const auto claim = game_.workClaim(active, orders)) {
            workOrders_ = orders;
            askLandClaim(orders == SaveUnit::kRoad ? "INDIANROAD" : "INDIANFOREST", *claim, active);
            return;
        }
    }
    startWork(active, orders);
}

void PlayScreen::startWork(int active, int orders) {
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
    if (answerRoute(question, choice, picked)) {
        return;
    }
    if (question == "INDIANFOREST" || question == "INDIANROAD") {
        // Leave it, pay the tribe, or go on regardless.
        const auto claim = std::exchange(landClaim_, std::nullopt);
        if (unit < 0 || !claim) {
            return;
        }
        if (choice == 0) {
            game_.setOrders(unit, SaveUnit::kNoOrders);
            game_.skipUnit(unit);
            advance();
            return;
        }
        landAfterBribe_ = LandAfter::Work;
        if (choice == 1 && landBought(*claim, game_.unit(unit).nation, unit)) {
            return;
        }
        startWork(unit, workOrders_);
        return;
    }
    if (question == "INDIANBRIBE") {
        if (landAfterBribe_ == LandAfter::Work && unit >= 0) {
            startWork(unit, workOrders_);
        } else if (landAfterBribe_ == LandAfter::Colony) {
            putToLandWork();
        }
        landAfterBribe_ = LandAfter::Nothing;
        return;
    }
    if (question == "ABANDON") {
        // "Yes, it is God's will." The colony goes with its last colonist.
        if (choice == 0) {
            doColonyMove(moveWho_, moveJob_, -1);
        }
        return;
    }
    if (question == "INDIANLAND") {
        // "Very well, ...": the colonist stays where they were; paying or
        // "this is OUR land now" puts them to work.
        const auto claim = std::exchange(landClaim_, std::nullopt);
        if (!claim || !colonyScreen_ || choice == 0) {
            return;
        }
        landAfterBribe_ = LandAfter::Colony;
        const int nation = game_.state().colonies[static_cast<std::size_t>(colonyScreen_->colony)].nation;
        if (choice == 1 && landBought(*claim, nation, -1)) {
            return;
        }
        putToLandWork();
        return;
    }
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
            playMusic(game_.takeMusicCues());
            showLesson(unit, lesson.section, lesson.strings);
            if (lesson.section != "LEARNSTAY") {
                question_ = "LESSONDONE";
            }
        } else if (option == kVillageSpeak && village_ >= 0) {
            speakWithChief(unit);
        } else if (option == kVillageMission && village_ >= 0) {
            foundMission(unit);
        } else if (option == kVillageTrade && village_ >= 0) {
            tradeWithVillage(unit);
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
    } else if (question == "FOREIGNTRADEWHICH") {
        const int colony = std::exchange(foreignTradeColony_, -1);
        if (colony >= 0 && choice >= 0 && choice < game_.unit(unit).holds) {
            game_.offerForeignTrade(unit, colony, choice);
        }
        for (auto& notice : game_.takeNotices()) {
            notices_.push_back(std::move(notice));
        }
        advanceAfterNotices_ = true;
    } else if (question == "TRADEWITH") {
        // "We'll take the goods.", "Just give us the gold." or "Do you take
        // us for fools?"
        game_.answerForeignTrade(choice);
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
    } else if (question == "NOPORT" || question == "TUTNOSPACES" || question == "TUTNOLUMBER") {
        // The second answer builds anyway; the first, or Escape, gives up.
        if (choice == 1) {
            nextFoundingStep();
        } else {
            founding_.reset();
        }
    } else if (question == "LOBOTOMIZE") {
        if (choice == 0 && colonyScreen_) {
            game_.clearSpecialty(colonyScreen_->colony, colonyScreen_->selectedColonist);
        }
    } else if (question == "FINDCITY") {
        // Seen: the whole name, in any case; the map then shows the colony
        // with the view cursor on it. Another name is "not found".
        if (choice == 0 && !typed_.empty()) {
            const auto lower = [](std::string text) {
                for (auto& c : text) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                return text;
            };
            int found = -1;
            const auto& colonies = game_.state().colonies;
            for (std::size_t i = 0; i < colonies.size() && found < 0; ++i) {
                if (lower(colonies[i].name) == lower(typed_)) {
                    found = static_cast<int>(i);
                }
            }
            if (found >= 0) {
                const auto& c = colonies[static_cast<std::size_t>(found)];
                viewMode_ = true;
                cursorX_ = c.x;
                cursorY_ = c.y;
                centreOn(view_, game_, c.x, c.y);
            } else {
                ask("NOCITY", -1, {typed_});
            }
        }
    } else if (question == "LANDHO") {
        if (choice == 0 && !typed_.empty()) {
            game_.setCountryName(typed_);
        }
        // Read from the original: with Tutorial Hints on, how to go ashore.
        if (game_.tutorialHints()) {
            notices_.insert(notices_.begin(), GameNotice{"TUTORIAL2", {}, 0});
        }
    } else if (question == "RENAMECOLONY") {
        // An empty name keeps the old one (read from the original).
        if (choice == 0 && !typed_.empty() && colonyScreen_) {
            game_.renameColony(colonyScreen_->colony, typed_);
        }
    } else if (question == "HOWMUCH1" || question == "HOWMUCH2") {
        const auto move = cargoMove_;
        cargoMove_ = CargoMove{};
        const int amount = std::min(std::atoi(typed_.c_str()), move.amount);
        if (choice == 0 && amount > 0 && colonyScreen_) {
            if (question == "HOWMUCH1") {
                const int moved = game_.loadInColony(move.carrier, colonyScreen_->colony, move.good, amount);
                if (moved > 0) {
                    setColonyMessage(std::to_string(moved) + " " + goodName(move.good) + " " +
                                     cargoLabel(kLabelMovedTo) + " " + unitTypeName(move.carrier));
                }
            } else {
                checkWarehouse(move.carrier, move.good, amount);
            }
        }
    } else if (question == "WAREHOUSEFULL") {
        const auto move = cargoMove_;
        cargoMove_ = CargoMove{};
        if (choice == 1 && colonyScreen_) {
            finishUnload(move.carrier, move.good, move.amount);
        } else {
            unloadAll_.clear();
        }
    } else if (question == "SUREDISBAND") {
        if (choice == 0 && unit >= 0 && unit < game_.unitCount()) {
            const bool wasActive = unit == game_.activeUnit();
            game_.disband(unit);
            if (wasActive) {
                game_.setActiveUnit(-1);
                advance();
            }
        }
    } else if (question == "OVERBOARD") {
        // The first line, "Nothing", keeps everything.
        if (choice >= 1 && unit >= 0 && unit < game_.unitCount()) {
            game_.dumpHold(unit, choice - 1);
        }
    } else if (question == "EUROPESHIPCLICK") {
        const int option = choice >= 0 && choice < static_cast<int>(unitOptions_.size())
                               ? unitOptions_[static_cast<std::size_t>(choice)]
                               : kEuroShipNoChange;
        if (option == kEuroShipFront) {
            const int moved = game_.moveToFront(unit);
            if (europeScreen_) {
                europeScreen_->ship = moved;
            }
        } else if (option == kEuroShipSail) {
            ask("SAILAWAY", unit);
        } else if (option == kEuroShipUnloadAll) {
            unloadAllInEurope(unit);
        }
    } else if (question == "EUROPEARM") {
        const int option = choice >= 0 && choice < static_cast<int>(unitOptions_.size())
                               ? unitOptions_[static_cast<std::size_t>(choice)]
                               : kDockNoChange;
        using Gear = Game::DockGear;
        int move = 0;
        switch (option) {
        case kDockStayOff:
            game_.setOrders(unit, SaveUnit::kNoOrders);
            break;
        case kDockBoard:
            if (europeScreen_ && (europeScreen_->ship < 0 || !game_.boardInPort(unit, europeScreen_->ship))) {
                say("No room aboard.");
            }
            break;
        case kDockFront:
            game_.moveToFront(unit);
            if (europeScreen_) {
                europeScreen_->dock = 0;
            }
            break;
        case kDockArm:
            move = game_.equipOnDocks(unit, Gear::Muskets, true);
            playMusic({{MusicCue::Kind::Sound, sounds::kFortify}}); // read from the original
            tellPriceMove(cargo::kMuskets, move);
            break;
        case kDockSellMuskets:
            tellPriceMove(cargo::kMuskets, game_.equipOnDocks(unit, Gear::Muskets, false));
            break;
        case kDockTools:
            tellPriceMove(cargo::kTools, game_.equipOnDocks(unit, Gear::Tools, true));
            break;
        case kDockSellTools:
            tellPriceMove(cargo::kTools, game_.equipOnDocks(unit, Gear::Tools, false));
            break;
        case kDockHorses:
            move = game_.equipOnDocks(unit, Gear::Horses, true);
            playMusic({{MusicCue::Kind::Sound, sounds::kSaddle}}); // read from the original
            tellPriceMove(cargo::kHorses, move);
            break;
        case kDockSellHorses:
            tellPriceMove(cargo::kHorses, game_.equipOnDocks(unit, Gear::Horses, false));
            break;
        case kDockBless:
            game_.setUnitType(unit, unit_type::kMissionary);
            playMusic({{MusicCue::Kind::Sound, sounds::kChurch}}); // read from the original
            break;
        case kDockUnbless:
            game_.setUnitType(unit, unit_type::kColonist);
            break;
        default:
            break;
        }
    } else if (question == "COLONYSHIP") {
        const int option = choice >= 0 && choice < static_cast<int>(unitOptions_.size())
                               ? unitOptions_[static_cast<std::size_t>(choice)]
                               : kShipNoChange;
        switch (option) {
        case kShipFront:
            game_.moveToFront(unit);
            if (colonyScreen_) {
                colonyScreen_->selectedShip = 0;
            }
            break;
        case kShipClear:
            game_.setOrders(unit, SaveUnit::kNoOrders);
            break;
        case kShipSentry:
            game_.setOrders(unit, SaveUnit::kSentry);
            break;
        case kShipAnchor:
            game_.setOrders(unit, SaveUnit::kFortify);
            break;
        case kShipUnloadAll:
            unloadAll_ = {unit};
            continueUnloadAll();
            break;
        default:
            break;
        }
    } else if (question == "COLONY") {
        if (choice == 0 && founding_) {
            foundNamedColony(founding_->unit, typed_);
        } else {
            founding_.reset();
        }
    } else if (question == "GRUDGEWAGONS") {
        startTrade(unit);
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
            game_.europeanAttack(us, them);
            // Measured: "Spanish cancel peace treaty with Dutch." when the
            // Dutch attack the Spanish; the music turns to war.
            playMusic({{MusicCue::Kind::ChangeMood, 4}});
            ask("CANCELPEACE", -1, {adjective(them), adjective(us)});
            if (dialog_) {
                placeMapDialog(*dialog_);
            }
        } else {
            attack_.reset();
        }
    } else if (question == "WHACKINDIANS") {
        // "Yes" makes war on the tribe; "No" calls the attack off.
        if (choice == 0 && attack_) {
            game_.makeWarOnTribe(game_.unit(attack_->attacker).nation, defenderNation(*attack_) - kEuropeanNations);
            continueAttack();
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
    } else if (question == "LANDFALL" || question == "LANDFALL2") {
        // "Make Landfall": everyone aboard wakes up and one goes ashore.
        if (choice == 1 && landfall_) {
            const auto landing = *landfall_;
            for (const int p : game_.passengers(unit)) {
                game_.setOrders(p, SaveUnit::kNoOrders);
            }
            game_.setActiveUnit(landing.unit);
            moveActive(landing.step);
        }
        landfall_.reset();
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
    } else if (question == "KINGFRIGATE") {
        // "Yes, I fear it is necessary.": the frigate, and the tax rise.
        game_.answerKingFrigate(choice == 0);
        auto news = game_.takeNotices();
        notices_.insert(notices_.begin(), std::make_move_iterator(news.begin()), std::make_move_iterator(news.end()));
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
    } else if (question == "MULTIREV") {
        // "Declare independence" or "Never Mind".
        if (choice == 0) {
            game_.playAlone();
            notices_.insert(notices_.begin(), game_.independenceQuestion());
        }
    } else if (question == "DECLARE") {
        // "Never! ..." or "Yes! Give me liberty or give me death!": the
        // turn ends with the declaration.
        if (choice == 1) {
            game_.declareIndependence();
            auto news = game_.takeNotices();
            notices_.insert(notices_.begin(), std::make_move_iterator(news.begin()), std::make_move_iterator(news.end()));
            game_.setActiveUnit(-1);
        }
    } else if (question == "SAVEGAME") {
        if (choice >= 0 && choice < 8 && art_ != nullptr) {
            const std::string name = "COLONY0" + std::to_string(choice) + ".SAV";
            const auto folder = savePath_.parent_path();
            const auto found = findFileNoCase(folder, name);
            if (saveTo(found ? *found : folder / name)) {
                ask("SAVEGOOD", -1, {name, saveTitle(game_.state(), *art_, true)});
            } else {
                ask("SAVEERROR", -1, {name});
            }
            if (dialog_) {
                placeCentredDialog(*dialog_);
            }
        }
    } else if (question == "LOADGAME") {
        // Seen: an empty slot does nothing.
        if (choice >= 0 && art_ != nullptr) {
            const auto slots = listSaves(savePath_.parent_path(), *art_);
            if (static_cast<std::size_t>(choice) < slots.size() && !slots[static_cast<std::size_t>(choice)].empty) {
                loadPath_ = slots[static_cast<std::size_t>(choice)].path;
            }
        }
    } else if (question == "DOS") {
        quit_ = choice == 0;
    } else if (question == "RETIRE") {
        // "Yes" or "No".
        if (choice == 0) {
            retire();
        }
    } else if (question == "SCORED") {
        // "That's all." or "Keep playing anyway."
        if (choice == 1) {
            game_.keepPlaying();
        }
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
            // Read from the original: a purchase the treasury cannot pay
            // in full is not made; the hint on buying less follows (shown
            // whatever the Tutorial Hints option).
            const int room = buyRoom(save, state.ship, state.good, game_.freeHolds(state.ship));
            const int price = askPrice(game_.rules(), nation, state.good);
            if (room > 0 && price * room > nation.gold) {
                tell(GameNotice{"TUTORIAL18", {goodName(state.good)}, kTutorialNoAdviser, {price, nation.gold}});
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
                openEuropeShipOptions(state.ship);
            }
        } else if (state.focus == EuropeFocus::Buttons) {
            state.button == 0 ? openRecruit() : state.button == 1 ? openPurchase() : openTrain();
        } else if (!docks.empty()) {
            state.dock = std::clamp(state.dock, 0, static_cast<int>(docks.size()) - 1);
            openDockOptions(docks[static_cast<std::size_t>(state.dock)], state.dock == 0);
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

ColonyMover PlayScreen::colonyMover() const {
    const auto& state = *colonyScreen_;
    if (state.selectedOutside >= 0) {
        return {-1, state.selectedOutside};
    }
    const auto& colony = game_.state().colonies[static_cast<std::size_t>(state.colony)];
    if (state.selectedColonist >= 0 && state.selectedColonist < colony.population) {
        return {state.selectedColonist, -1};
    }
    return {};
}

void PlayScreen::tryColonyMove(ColonyMover who, int job, int square) {
    if (!colonyScreen_ || (who.colonist < 0 && who.unit < 0) || job < 0) {
        return;
    }
    auto& state = *colonyScreen_;
    const auto& save = game_.state();
    const auto& colony = save.colonies[static_cast<std::size_t>(state.colony)];
    const bool onLand = job < static_cast<int>(kSquareWorkCount);
    const auto result = game_.checkColonyMove(state.colony, who, job);
    if (result == ColonyMove::Same) {
        // The same work on another square is a move all the same.
        if (!onLand || square < 0 || who.colonist < 0 ||
            colony.squares[static_cast<std::size_t>(square)] == who.colonist) {
            return;
        }
    } else if (result == ColonyMove::Abandon) {
        // Read from the original: ABANDON2 warns of the charter when the
        // nation has fewer than two colonies after 1575.
        int colonies = 0;
        for (const auto& other : save.colonies) {
            colonies += other.nation == colony.nation ? 1 : 0;
        }
        moveWho_ = who;
        moveJob_ = job;
        ask(colonies < 2 && save.year > 1575 ? "ABANDON2" : "ABANDON", -1, {colony.name});
        if (dialog_) {
            question_ = "ABANDON";
            placeBuyDialog(*dialog_);
        }
        return;
    } else if (result != ColonyMove::Allowed) {
        tellColonyMove(result, who);
        return;
    }
    if (onLand && square < 0) {
        // From the jobs menu: the best free square for the work.
        return;
    }
    if (onLand) {
        const bool there = who.colonist >= 0 && colony.squares[static_cast<std::size_t>(square)] == who.colonist;
        if (!there && !squareFree(colony, save.map, save.colonies, square)) {
            return;
        }
        if (!there) {
            if (const auto claim = game_.colonyClaim(state.colony, square)) {
                // Read from the original: the tribe whose land it is asks.
                landWho_ = who;
                landJob_ = job;
                landSquare_ = square;
                askLandClaim("INDIANLAND", *claim, -1);
                return;
            }
        }
    }
    doColonyMove(who, job, onLand ? square : -1);
}

void PlayScreen::doColonyMove(ColonyMover who, int job, int square) {
    if (!colonyScreen_) {
        return;
    }
    auto& state = *colonyScreen_;
    const auto before = game_.state().colonies.size();
    const int out = game_.moveColonist(state.colony, who, job, square);
    if (game_.state().colonies.size() != before) {
        // The last colonist left: the colony is no more.
        colonyScreen_.reset();
        return;
    }
    const auto& colony = game_.state().colonies[static_cast<std::size_t>(state.colony)];
    if (out >= 0) {
        // Measured: the one who went out stays picked, outside.
        state.selectedOutside = out;
        state.selectedColonist = -1;
    } else if (who.unit >= 0) {
        state.selectedOutside = -1;
        state.selectedColonist = colony.population - 1;
    }
    // Read from the original: a colonist put to work in a colony of three
    // without a stockade brings the lesson on building one; the screen
    // drawn again may show the colony short of food.
    if (out < 0) {
        if (auto hint = game_.stockadeHint(state.colony)) {
            notices_.push_back(std::move(*hint));
        }
    }
    if (auto food = game_.foodHint(state.colony)) {
        notices_.push_back(std::move(*food));
    }
    showNotices();
}

void PlayScreen::tellColonyMove(ColonyMove result, ColonyMover who) {
    const auto& state = *colonyScreen_;
    const auto& colony = game_.state().colonies[static_cast<std::size_t>(state.colony)];
    const auto& jobs = game_.rules().jobs;
    int skill =
        who.colonist >= 0 ? colony.skills[static_cast<std::size_t>(who.colonist)] : game_.unit(who.unit).profession;
    const std::string profession =
        skill >= 0 && static_cast<std::size_t>(skill) < jobs.size() ? jobs[static_cast<std::size_t>(skill)].name : "";
    // Read from the original: most come from the woman who also offers to
    // buy; the docks from the admiral, the siege from the colonial adviser.
    const char* section = nullptr;
    std::vector<std::string> strings;
    switch (result) {
    case ColonyMove::Full:
        section = "FULL";
        strings = {colony.name};
        break;
    case ColonyMove::Univ3:
        section = "UNIV3";
        break;
    case ColonyMove::College2:
        section = "COLLEGE2";
        break;
    case ColonyMove::School1:
        section = "SCHOOL1";
        break;
    case ColonyMove::NoDocks:
        section = "NODOCKS";
        break;
    case ColonyMove::NoTeacher:
        section = "NOTEACHER";
        break;
    case ColonyMove::NeedCollege:
        section = "NEEDCOLLEGE";
        strings = {profession};
        break;
    case ColonyMove::NeedUniversity:
        section = "NEEDUNIVERSITY";
        strings = {profession};
        break;
    case ColonyMove::Siege:
        section = "SIEGE";
        break;
    case ColonyMove::KeepStockade:
        section = "KEEPSTOCKADE";
        break;
    case ColonyMove::MoreThanThree:
        section = "MORETHANTHREE";
        break;
    default:
        return;
    }
    ask(section, -1, strings);
    if (!dialog_) {
        return;
    }
    question_ = "COLONYMOVE";
    if (result == ColonyMove::NoDocks) {
        placeNavalDialog(*dialog_);
    } else if (result == ColonyMove::Siege) {
        placeMapDialog(*dialog_);
    } else {
        placeBuyDialog(*dialog_);
    }
}

void PlayScreen::openOutsideMenu(bool fromMore) {
    auto& state = *colonyScreen_;
    const auto who = colonyMover();
    if (who.colonist < 0 && who.unit < 0) {
        return;
    }
    state.jobsMenu = -1;
    state.outsideMenu = 0;
    state.outsideFromMore = fromMore;
}

bool PlayScreen::handleOutsideMenu(Command command) {
    auto& state = *colonyScreen_;
    const auto who = colonyMover();
    const auto choices = game_.outsideChoices(state.colony, who);
    const int lines = static_cast<int>(choices.size()) + 1;
    switch (command) {
    case Command::MoveN:
    case Command::Up:
        state.outsideMenu = (state.outsideMenu + lines - 1) % lines;
        break;
    case Command::MoveS:
    case Command::Down:
        state.outsideMenu = (state.outsideMenu + 1) % lines;
        break;
    case Command::Confirm:
    case Command::Skip: {
        const int line = state.outsideMenu;
        if (line == 0) {
            // "(More)" goes back to the jobs inside; the present job keeps it.
            state.outsideMenu = -1;
            if (state.outsideFromMore && who.colonist >= 0) {
                const auto& save = game_.state();
                const auto& colony = save.colonies[static_cast<std::size_t>(state.colony)];
                const auto inside =
                    jobChoices(colony, save.map, game_.rules(), save.colonies, who.colonist, save.difficulty);
                state.jobsMenu =
                    jobsMenuClearLine(colony, who.colonist) + static_cast<int>(inside.size()); // on "(More)"
            }
            break;
        }
        const auto& picked = choices[static_cast<std::size_t>(line - 1)];
        if (!picked.affordable) {
            break; // greyed
        }
        state.outsideMenu = -1;
        tryColonyMove(who, picked.job);
        break;
    }
    case Command::Cancel:
        state.outsideMenu = -1;
        break;
    default:
        break;
    }
    return true;
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
    if (state.outsideMenu >= 0) {
        const int lines = static_cast<int>(game_.outsideChoices(state.colony, colonyMover()).size()) + 1;
        const auto box = outsideMenuBox(lines);
        const int line = (y - (box.firstLine - 1)) / 8;
        if (!inside(box) || y < box.firstLine - 1 || line >= lines) {
            state.outsideMenu = inside(box) ? state.outsideMenu : -1;
            return true;
        }
        state.outsideMenu = line;
        handleOutsideMenu(Command::Confirm);
        return true;
    }
    if (state.foreign) {
        return !(x >= 300 && y >= 178); // only the exit button
    }
    if (y < 7) {
        // Read from the original: a click on the line at the top asks for
        // a new name (up to 23 letters, the old one offered).
        ask("RENAMECOLONY", -1);
        if (dialog_ && art_ != nullptr) {
            makeInput(*art_, *dialog_, game_.state().colonies[static_cast<std::size_t>(state.colony)].name, 23);
        }
        return true;
    }
    {
        // Read from the original: pressing on a good of the warehouse picks
        // it and starts dragging it; in the harbour, pressing on a ship
        // picks it (pressing the one already picked opens its options when
        // let go), and on a full hold starts dragging its goods.
        const auto& colony = game_.state().colonies[static_cast<std::size_t>(state.colony)];
        if (const int good = warehouseGoodAt(x, y); good >= 0 && x < 300) {
            state.selectedGood = good;
            cargoDrag_ = CargoDrag{false, good};
            setColonyMessage(cargoLabel(kLabelLoadingGoods) + " " + goodName(good));
            return true;
        }
        const auto ships = shipsInPort(game_, colony);
        if (!ships.empty() && inHarbour(x, y)) {
            state.selectedShip = std::clamp(state.selectedShip, 0, static_cast<int>(ships.size()) - 1);
            const int shipHit = harbourShipAt(x, y, static_cast<int>(ships.size()));
            if (state.selectedOutside >= 0 && (shipHit >= 0 || harbourHoldAt(x, y) >= 0)) {
                // A unit picked outside, then a ship or its holds: it goes
                // aboard (when it fits).
                const int k = shipHit >= 0 ? shipHit : state.selectedShip;
                if (game_.boardInColony(state.selectedOutside, ships[static_cast<std::size_t>(k)])) {
                    state.selectedShip = k;
                    state.harbourChosen = true;
                    state.selectedOutside = -1;
                }
                return true;
            }
            if (const int k = shipHit; k >= 0) {
                shipPressedAgain_ = k == state.selectedShip;
                state.selectedShip = k;
                state.harbourChosen = true;
                return true;
            }
            const auto& carrier = game_.unit(ships[static_cast<std::size_t>(state.selectedShip)]);
            if (const int hold = harbourHoldAt(x, y); hold >= 0 && hold < carrier.holds) {
                const int good = holdGood(carrier, hold);
                cargoDrag_ = CargoDrag{true, good};
                setColonyMessage(cargoLabel(kLabelUnloadingGoods) + " " + goodName(good));
            } else if (hold >= carrier.holds) {
                // Someone aboard: back ashore, picked outside.
                int at = carrier.holds;
                for (const int p : game_.passengersInColony(ships[static_cast<std::size_t>(state.selectedShip)])) {
                    const int size = game_.unitSize(p);
                    if (hold >= at && hold < at + size) {
                        game_.leaveShipInColony(p);
                        state.selectedOutside = p;
                        state.selectedColonist = -1;
                        break;
                    }
                    at += size;
                }
            }
            return true;
        }
    }
    {
        // Read from the original: a click picks a colonist (or a unit
        // outside); a click on a building, a square or the stockade then
        // moves them there.
        const auto& colony = game_.state().colonies[static_cast<std::size_t>(state.colony)];
        const auto hit = colonyHitAt(game_, colony, x, y, art_);
        const auto who = colonyMover();
        switch (hit.kind) {
        case ColonyHit::Kind::Colonist:
            state.selectedColonist = hit.colonist;
            state.selectedOutside = -1;
            state.harbourChosen = false;
            return true;
        case ColonyHit::Kind::Outside:
            if (hit.unit >= 0) {
                state.selectedOutside = hit.unit;
                state.selectedColonist = -1;
            } else {
                openOutsideMenu(false);
            }
            return true;
        case ColonyHit::Kind::Building:
            tryColonyMove(who, hit.job);
            return true;
        case ColonyHit::Kind::CustomHouse:
            // Read from the original: which goods it sells, ticked.
            ask("CUSTOM", -1);
            if (dialog_) {
                for (int good = 0; good < kGoodsKinds; ++good) {
                    const auto g = static_cast<std::size_t>(good);
                    addOption(*dialog_, g < game_.rules().cargo.size() ? game_.rules().cargo[g].name : std::string());
                    dialog_->checked.push_back(exportTicked(colony, good));
                }
            }
            return true;
        case ColonyHit::Kind::Square:
            if (who.colonist >= 0 || who.unit >= 0) {
                tryColonyMove(who, game_.jobForSquare(state.colony, who, hit.square), hit.square);
            }
            return true;
        case ColonyHit::Kind::None:
            break;
        }
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

bool PlayScreen::releaseColonyScreen(int x, int y, bool shift) {
    if (!colonyScreen_ || dialog_) {
        cargoDrag_.reset();
        shipPressedAgain_ = false;
        return false;
    }
    auto& state = *colonyScreen_;
    const auto& colony = game_.state().colonies[static_cast<std::size_t>(state.colony)];
    const auto ships = shipsInPort(game_, colony);
    const int count = static_cast<int>(ships.size());
    if (shipPressedAgain_) {
        shipPressedAgain_ = false;
        if (count > 0 && harbourShipAt(x, y, count) == state.selectedShip) {
            openShipOptions(ships[static_cast<std::size_t>(state.selectedShip)], state.selectedShip == 0);
        }
        return true;
    }
    if (!cargoDrag_) {
        return false;
    }
    const auto drag = *cargoDrag_;
    cargoDrag_.reset();
    state.topMessage.clear();
    if (count == 0) {
        return true;
    }
    state.selectedShip = std::clamp(state.selectedShip, 0, count - 1);
    if (!drag.fromHold && inHarbour(x, y)) {
        if (const int k = harbourShipAt(x, y, count); k >= 0) {
            state.selectedShip = k;
        }
        state.harbourChosen = true;
        startLoad(ships[static_cast<std::size_t>(state.selectedShip)], drag.good, shift);
    } else if (drag.fromHold && warehouseGoodAt(x, y) >= 0) {
        startUnload(ships[static_cast<std::size_t>(state.selectedShip)], drag.good, shift);
    }
    return true;
}

void PlayScreen::setColonyMessage(std::string text) {
    if (colonyScreen_) {
        colonyScreen_->topMessage = std::move(text);
        colonyMessageMs_ = 0;
    }
}

// Read from the original: loading takes up to 100 (no more than the
// warehouse has, nor than there is room for); Shift asks how much first.
void PlayScreen::startLoad(int carrier, int good, bool askAmount) {
    if (!colonyScreen_) {
        return;
    }
    const int colonyIndex = colonyScreen_->colony;
    const auto& colony = game_.state().colonies[static_cast<std::size_t>(colonyIndex)];
    const int stock = colony.stock[static_cast<std::size_t>(good)];
    if (stock < 1) {
        setColonyMessage(cargoLabel(kLabelNotEnough) + " " + goodName(good) + " " + cargoLabel(kLabelToFill));
        return;
    }
    const int room = game_.colonyCargoRoom(carrier, good);
    if (room <= 0) {
        setColonyMessage(cargoLabel(kLabelNoRoom) + " " + goodName(good) + "!");
        return;
    }
    const int amount = std::min({room, 100, stock});
    if (askAmount && art_ != nullptr) {
        ask("HOWMUCH1", carrier, {goodName(good), unitTypeName(carrier)}, -1, {amount});
        if (dialog_) {
            makeInput(*art_, *dialog_, std::to_string(amount), 3);
            cargoMove_ = CargoMove{carrier, good, amount};
        }
        return;
    }
    const int moved = game_.loadInColony(carrier, colonyIndex, good, amount);
    if (moved > 0) {
        setColonyMessage(std::to_string(moved) + " " + goodName(good) + " " + cargoLabel(kLabelMovedTo) + " " +
                         unitTypeName(carrier));
    }
}

// Read from the original: unloading empties the first hold of the good;
// Shift asks how much first, and the rest stays aboard.
void PlayScreen::startUnload(int carrier, int good, bool askAmount) {
    if (!colonyScreen_) {
        return;
    }
    const int aboard = game_.colonyCargoHold(carrier, good);
    if (aboard < 0) {
        setColonyMessage(cargoLabel(kLabelNo) + " " + goodName(good) + " " + cargoLabel(kLabelInHold));
        return;
    }
    if (askAmount && art_ != nullptr) {
        const auto& colony = game_.state().colonies[static_cast<std::size_t>(colonyScreen_->colony)];
        ask("HOWMUCH2", carrier, {goodName(good), unitTypeName(carrier), colony.name}, -1, {aboard});
        if (dialog_) {
            makeInput(*art_, *dialog_, std::to_string(aboard), 3);
            cargoMove_ = CargoMove{carrier, good, aboard};
        }
        return;
    }
    checkWarehouse(carrier, good, aboard);
}

// Read from the original: past what the warehouse keeps (food aside) it
// warns that goods will spoil, and unloads only when told to anyway.
void PlayScreen::checkWarehouse(int carrier, int good, int amount) {
    const auto& colony = game_.state().colonies[static_cast<std::size_t>(colonyScreen_->colony)];
    const int stock = colony.stock[static_cast<std::size_t>(good)];
    const int limit = storageLimit(colony);
    if (good != cargo::kFood && stock + amount > limit && art_ != nullptr) {
        ask("WAREHOUSEFULL", carrier, {colony.name, goodName(good)}, -1, {stock, limit, amount});
        if (dialog_) {
            cargoMove_ = CargoMove{carrier, good, amount};
            return;
        }
    }
    finishUnload(carrier, good, amount);
}

void PlayScreen::finishUnload(int carrier, int good, int amount) {
    if (!colonyScreen_) {
        return;
    }
    const int colonyIndex = colonyScreen_->colony;
    const int moved = game_.unloadInColony(carrier, colonyIndex, good, amount);
    if (moved > 0) {
        setColonyMessage(std::to_string(moved) + " " + goodName(good) + " " + cargoLabel(kLabelMovedTo) + " " +
                         game_.state().colonies[static_cast<std::size_t>(colonyIndex)].name);
    }
    continueUnloadAll();
}

// "Unload all cargo.": every hold in turn, each as if dragged ashore.
void PlayScreen::continueUnloadAll() {
    while (!unloadAll_.empty() && !dialog_ && colonyScreen_) {
        const int carrier = unloadAll_.front();
        const auto& u = game_.unit(carrier);
        if (u.holds <= 0) {
            unloadAll_.clear();
            return;
        }
        startUnload(carrier, holdGood(u, 0), false);
        if (game_.unit(carrier).holds == u.holds && !dialog_) {
            unloadAll_.clear(); // nothing moved: stop rather than loop
        }
    }
}

// Read from the original: a click on the ship already picked. "Move to
// front." is left out for the first, "Clear orders." without orders,
// "Sentry." when sentried, "Anchor in harbor" when fortified and for a
// wagon train, "Unload all cargo." with nothing aboard.
void PlayScreen::openShipOptions(int carrier, bool first) {
    if (art_ == nullptr) {
        return;
    }
    const auto& u = game_.unit(carrier);
    ask("COLONYUNIT", carrier, {game_.unitName(carrier), ""});
    if (!dialog_) {
        return;
    }
    question_ = "COLONYSHIP";
    unitOptions_.clear();
    for (int option = kShipFront; option <= kShipNoChange; ++option) {
        if ((option == kShipFront && first) || (option == kShipClear && u.orders == SaveUnit::kNoOrders) ||
            (option == kShipSentry && u.orders == SaveUnit::kSentry) ||
            (option == kShipAnchor && (u.orders == SaveUnit::kFortify || u.orders == SaveUnit::kFortified ||
                                       u.type == unit_type::kWagonTrain)) ||
            (option == kShipUnloadAll && u.holds == 0)) {
            continue;
        }
        addOption(*dialog_, textLine(art_->gameText, "SHIPOPTIONS", option, ""));
        unitOptions_.push_back(option);
    }
    placeUnitDialog(*dialog_, carrier);
}

// Read from the original: a ship with units aboard is not disbanded
// (DISBANDSHIP); anything else asks first, naming its type (SUREDISBAND).
void PlayScreen::disbandActive() {
    const int active = game_.activeUnit();
    if (active < 0) {
        return;
    }
    if (game_.isShip(active) && !game_.passengers(active).empty()) {
        tell({"DISBANDSHIP", {}});
        return;
    }
    ask("SUREDISBAND", active, {unitTypeName(active)});
}

// Read from the original: "What cargo shall we throw overboard" lists
// "Nothing" and then each hold, "100 Muskets"; the hold picked is emptied.
void PlayScreen::dumpCargo() {
    const int active = game_.activeUnit();
    if (active < 0 || art_ == nullptr || game_.unit(active).holds <= 0) {
        return;
    }
    ask("OVERBOARD", active);
    if (!dialog_) {
        return;
    }
    addOption(*dialog_, textLine(art_->labelText, "MISC", kLabelNothingToDump, "Nothing"));
    const auto& u = game_.unit(active);
    for (int k = 0; k < u.holds; ++k) {
        addOption(*dialog_, std::to_string(holdAmount(u, k)) + " " + goodName(holdGood(u, k)));
    }
}

// Read from the original: a ship in port clicked, "European harbor options
// for Frigate:". "Set sail" is left out for a damaged ship (under repair),
// "Unload all cargo." when nothing is aboard.
void PlayScreen::openEuropeShipOptions(int ship) {
    if (art_ == nullptr) {
        return;
    }
    const auto& u = game_.unit(ship);
    ask("EUROPESHIPCLICK", ship, {unitTypeName(ship)});
    if (!dialog_) {
        return;
    }
    unitOptions_.clear();
    for (int option = kEuroShipFront; option <= kEuroShipNoChange; ++option) {
        if ((option == kEuroShipSail && (u.raw[4] & SaveUnit::kDamaged) != 0) ||
            (option == kEuroShipUnloadAll && u.holds == 0)) {
            continue;
        }
        addOption(*dialog_, textLine(art_->gameText, "EUROPESHIPOPTIONS", option, ""));
        unitOptions_.push_back(option);
    }
    placeEuropeDialog(*dialog_);
}

// Read from the original: "Unload all cargo." in Europe sells the holds,
// the last first, leaving the boycotted goods aboard.
void PlayScreen::unloadAllInEurope(int ship) {
    auto& save = game_.state();
    const auto& nation = save.nations[static_cast<std::size_t>(game_.human())];
    for (int k = game_.unit(ship).holds - 1; k >= 0; --k) {
        const int good = holdGood(game_.unit(ship), k);
        if (game_.isBoycotted(game_.human(), good)) {
            continue;
        }
        const Trade t = sellGoods(save, game_.rules(), ship, good);
        if (t.amount > 0 && europeScreen_) {
            europeScreen_->receipt =
                TradeReceipt{true, t.amount, goodName(good), t.price, t.gold + t.tax, nation.taxRate, t.tax};
            tellPriceMove(good, t.priceMove);
        }
        k = std::min(k, game_.unit(ship).holds);
    }
}

// Read from the original: a unit on the docks clicked, "European dock
// options:". Arming, equipping and selling are offered by the unit's type
// (not to converts), not for boycotted goods, and greyed when the gold is
// short; the prices are 50 muskets, 100 tools or 50 horses.
void PlayScreen::openDockOptions(int unit, bool first) {
    if (art_ == nullptr) {
        return;
    }
    const auto& u = game_.unit(unit);
    ask("EUROPEARM", unit);
    if (!dialog_) {
        return;
    }
    using Gear = Game::DockGear;
    using namespace unit_type;
    constexpr int kConvert = 27; // the skill of Indian converts
    constexpr int kJesuit = 24;  // Jesuit missionaries keep their calling
    const bool convert = u.profession == kConvert;
    const int gold = game_.state().nations[static_cast<std::size_t>(game_.human())].gold;
    const auto boycotted = [&](int good) { return game_.isBoycotted(game_.human(), good); };
    const int muskets = (u.type == kSoldier || u.type == kDragoon) ? game_.dockGearPrice(Gear::Muskets, false)
                                                                   : game_.dockGearPrice(Gear::Muskets, true);
    const int tools =
        u.type == kPioneer ? game_.dockGearPrice(Gear::Tools, false) : game_.dockGearPrice(Gear::Tools, true);
    const int horses = (u.type == kScout || u.type == kDragoon) ? game_.dockGearPrice(Gear::Horses, false)
                                                                : game_.dockGearPrice(Gear::Horses, true);
    unitOptions_.clear();
    for (int option = kDockStayOff; option <= kDockNoChange; ++option) {
        bool shown = false;
        int cost = 0;
        switch (option) {
        case kDockStayOff:
            shown = u.orders == SaveUnit::kSentry;
            break;
        case kDockBoard:
            shown = u.orders != SaveUnit::kSentry;
            break;
        case kDockFront:
            shown = !first;
            break;
        case kDockArm:
            shown = (u.type == kColonist || u.type == kScout) && !convert && !boycotted(cargo::kMuskets);
            cost = muskets;
            break;
        case kDockSellMuskets:
            shown = (u.type == kSoldier || u.type == kDragoon) && !boycotted(cargo::kMuskets);
            break;
        case kDockTools:
            shown = u.type == kColonist && !convert && !boycotted(cargo::kTools);
            cost = tools;
            break;
        case kDockSellTools:
            shown = u.type == kPioneer && !boycotted(cargo::kTools);
            break;
        case kDockHorses:
            shown = (u.type == kColonist || u.type == kSoldier) && !convert && !boycotted(cargo::kHorses);
            cost = horses;
            break;
        case kDockSellHorses:
            shown = (u.type == kScout || u.type == kDragoon) && !boycotted(cargo::kHorses);
            break;
        case kDockBless:
            shown = u.type == kColonist && !convert;
            break;
        case kDockUnbless:
            shown = u.type == kMissionary && u.profession != kJesuit;
            break;
        default:
            shown = true;
            break;
        }
        if (!shown) {
            continue;
        }
        std::string text = textLine(art_->gameText, "ARMOPTIONS", option, "");
        for (const auto& [key, value] : {std::pair{"%NUMBER0", muskets}, {"%NUMBER1", tools}, {"%NUMBER2", horses}}) {
            if (const auto at = text.find(key); at != std::string::npos) {
                text.replace(at, std::string(key).size(), std::to_string(value));
            }
        }
        addOption(*dialog_, text, {}, cost > gold);
        unitOptions_.push_back(option);
    }
    placeEuropeDialog(*dialog_);
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
    if (state.outsideMenu >= 0) {
        return handleOutsideMenu(command);
    }
    if (state.jobsMenu >= 0) {
        const auto choices = jobChoices(colony, save.map, game_.rules(), save.colonies, state.selectedColonist, save.difficulty);
        // An expert's menu starts with "(CLEAR SPECIALTY)"; the last line is "(More)".
        const int clear = jobsMenuClearLine(colony, state.selectedColonist);
        const int lines = clear + static_cast<int>(choices.size()) + 1;
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
            if (state.jobsMenu < clear) {
                // Read from the original: it asks first, naming the specialty.
                state.jobsMenu = -1;
                const int skillOf = colony.skills[static_cast<std::size_t>(state.selectedColonist)];
                const auto& jobs = game_.rules().jobs;
                const std::string expert = skillOf >= 0 && static_cast<std::size_t>(skillOf) < jobs.size()
                                               ? jobs[static_cast<std::size_t>(skillOf)].expertName
                                               : std::string{};
                ask("LOBOTOMIZE", -1, {expert});
                placeBuyDialog(*dialog_);
                return true;
            }
            if (state.jobsMenu < clear + static_cast<int>(choices.size())) {
                const auto& picked = choices[static_cast<std::size_t>(state.jobsMenu - clear)];
                state.jobsMenu = -1;
                tryColonyMove(ColonyMover{state.selectedColonist, -1}, picked.job, picked.square);
                return true;
            }
            // "(More)": the jobs outside the colony.
            state.jobsMenu = -1;
            openOutsideMenu(true);
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
        if (state.selectedOutside >= 0) {
            openOutsideMenu(false);
        } else if (state.selectedColonist >= 0 && state.selectedColonist < people) {
            // The jobs menu opens on the colonist's present job.
            const auto choices = jobChoices(colony, save.map, game_.rules(), save.colonies, state.selectedColonist, save.difficulty);
            const int current = colony.jobs[static_cast<std::size_t>(state.selectedColonist)];
            const int clear = jobsMenuClearLine(colony, state.selectedColonist);
            state.jobsMenu = clear;
            for (std::size_t i = 0; i < choices.size(); ++i) {
                if (choices[i].job == current) {
                    state.jobsMenu = clear + static_cast<int>(i);
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
            state.selectedOutside = -1;
            state.selectedColonist = (state.selectedColonist + 1) % people;
        }
        break;
    case Command::MoveW:
    case Command::Up:
    case Command::PreviousUnit:
        if (people > 0) {
            state.selectedOutside = -1;
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
    const auto& map = game_.state().map;
    if (slide_ || x < 1 || y < 1 || x > map.width - 2 || y > map.height - 2) {
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

void PlayScreen::zoomTo(int zoom) {
    // Seen: the window is then centred on the active unit (zooming in from
    // the whole map too), or stays where it is with none.
    setZoom(view_, game_, zoom);
    const int active = game_.activeUnit();
    if (active >= 0 && game_.onMap(active)) {
        centreOn(view_, game_, game_.unit(active).x, game_.unit(active).y);
    }
}

void PlayScreen::scrollView(int dx, int dy) { viceroy::scrollView(view_, game_, dx, dy); }

bool PlayScreen::handle(Command command) {
    if (!dialog_ && notices_.empty()) {
        view_.topLine.clear();
    }
    if (slide_) {
        // Input waits until the unit has arrived.
        return true;
    }
    if (view_.hiddenTerrain) {
        view_.hiddenTerrain = false; // seen: any key brings the map back
        return true;
    }
    if (signing_) {
        // Read from the original: a key finishes the letter at once.
        ++signing_->stroke;
        signing_->frame = 0;
        signing_->elapsed = 0;
        if (signing_->stroke >= signing_->strokes.size()) {
            endSigning();
        }
        return true;
    }
    if (congressHall_ >= 0) {
        // Any key leaves the hall.
        congressHall_ = -1;
        showNotices();
        return true;
    }
    if (ending_ != 0) {
        advanceEnding();
        return true;
    }
    if (pedia_) {
        return handlePedia(command);
    }
    if (report_ != 0) {
        // Any key turns the page, and closes the report after the last.
        const int pages = report_ == 7   ? navalReportPages(game_, game_.human())
                          : report_ == 5 ? economyReportPages(game_, game_.human())
                          : report_ == 6 ? colonyAdviserPages(game_, game_.human())
                                         : 1;
        if (reportPage_ + 1 < pages) {
            ++reportPage_;
            return true;
        }
        if (report_ == 4 && reportPage_ > 0) {
            // Read from the original: a zoomed profession goes back to the
            // grid.
            reportPage_ = 0;
            return true;
        }
        report_ = 0;
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
    if (routeScreen_ >= 0) {
        return handleRouteScreen(command);
    }
    if (europeScreen_) {
        return handleEuropeScreen(command);
    }
    if (colonyScreen_) {
        return handleColonyScreen(command);
    }
    if (command == Command::Europe) {
        if (game_.independenceDeclared()) {
            tell({"EUROPENOTAVAIL", {}});
            return true;
        }
        openEurope();
        return true;
    }
    switch (command) {
    case Command::ReligionReport:
        openReport(2);
        return true;
    case Command::TerrainHelp:
        openTerrainPage();
        return true;
    case Command::CongressReport:
        openReport(3);
        return true;
    case Command::NavalReport:
        openReport(7);
        return true;
    case Command::ForeignReport:
        openReport(8);
        return true;
    case Command::LaborReport:
        openReport(4);
        return true;
    case Command::EconomyReport:
        openReport(5);
        return true;
    case Command::ColonyAdviser:
        openReport(6);
        return true;
    case Command::NativeReport:
        openReport(9);
        return true;
    case Command::ScoreReport:
        openReport(10);
        return true;
    default:
        break;
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
            if (!wellInside(view_, cursorX_, cursorY_)) {
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
            cursorX_ = view_.originX + zoomColumns(view_.zoom) / 2;
            cursorY_ = view_.originY + zoomRows(view_.zoom) / 2;
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
        autosave();
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
        // Read from the original: a ship of the player's that arrived in
        // Europe opens its screen once the news is read.
        europeAfterNotices_ = game_.takeEuropeArrival();
        for (auto& notice : notices) {
            notices_.push_back(std::move(notice));
        }
        showNotices();
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
    case Command::Disband:
        disbandActive();
        break;
    case Command::Dump:
        dumpCargo();
        break;
    case Command::Fortify:
        if (active >= 0 && !game_.isShip(active)) {
            game_.setOrders(active, SaveUnit::kFortify);
            advance();
        }
        break;
    case Command::BuildColony:
        if (active >= 0 && game_.independenceDeclared()) {
            // Read from the original: the first thing it checks.
            tell({"NOCOLONIESEITHER", {}});
            break;
        }
        if (active >= 0) {
            buildColony(active);
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
    case Command::ZoomIn:
        zoomTo(view_.zoom - 1);
        break;
    case Command::ZoomOut:
        zoomTo(view_.zoom + 1);
        break;
    case Command::GoTo:
        if (active >= 0) {
            openGoTo(0);
        }
        break;
    case Command::Train: // T: Begin Trade Route
        if (active >= 0 && (game_.isShip(active) || game_.unit(active).type == unit_type::kWagonTrain)) {
            beginTradeRoute();
        }
        break;
    case Command::Purchase: // P
        work(SaveUnit::kPlow);
        break;
    case Command::Recruit: // R
        work(SaveUnit::kRoad);
        break;
    case Command::Save:
        showSaveSlots();
        break;
    default:
        break;
    }
    return true;
}

bool PlayScreen::save() { return saveTo(savePath_); }

bool PlayScreen::saveTo(const std::filesystem::path& path) {
    const Bytes bytes = writeSaveGame(game_.state());
    const auto temp = path.string() + ".new";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            say("Could not save the game.");
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        say("Could not save the game.");
        return false;
    }
    savePath_ = path;
    return true;
}

// The box of slots, measured on the original: the small letters, as wide
// as the widest slot needs (8 more than its text) and centred on the
// screen.
Dialog saveSlotBox(const Art& art, std::string_view section, const std::vector<SaveSlot>& slots) {
    Dialog d = makeDialog(art, section);
    d.small = true;
    for (const auto& slot : slots) {
        addOption(d, slot.label);
        d.width = std::max(d.width, Screen::textWidth(art.tiny, slot.label) + 8);
    }
    placeCentredDialog(d);
    d.x = (Screen::kWidth - d.width - 6 + 1) / 2; // an odd width leaves the spare pixel on the left
    return d;
}

namespace {

// A savegame's name as the messages give it: "COLONY03.SAV".
std::string slotFileName(const std::filesystem::path& path) {
    std::string name = path.filename().string();
    for (auto& c : name) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return name;
}

// "Loaded COLONY03.SAV successfully.", or why the game could not be read.
Dialog loadMessage(const Art& art, const std::filesystem::path& path, bool good) {
    Dialog d = makeDialog(art, good ? "LOADGOOD" : "LOADNOT", {slotFileName(path)});
    placeCentredDialog(d);
    return d;
}

} // namespace

void PlayScreen::autosave() {
    // Read from the original: with the game option on, each new turn is
    // saved in COLONY09.SAV, and from the third turn on the spring of a
    // year ending in 0 in COLONY08.SAV. The slot played stays the same.
    if (!game_.gameOption(game_option::kAutosave) || savePath_.empty()) {
        return;
    }
    const auto& state = game_.state();
    const bool decade = state.season == 0 && state.year % 10 == 0 && state.turn > 2;
    const std::string name = decade ? "COLONY08.SAV" : "COLONY09.SAV";
    const auto folder = savePath_.parent_path();
    const auto found = findFileNoCase(folder, name);
    const auto playing = savePath_;
    saveTo(found ? *found : folder / name);
    savePath_ = playing;
}

void PlayScreen::showSaveSlots() {
    // Seen: the first eight slots (the last two are the autosaves), the bar
    // on the first.
    if (art_ == nullptr) {
        return;
    }
    ask("SAVEGAME", -1);
    if (dialog_) {
        *dialog_ = saveSlotBox(*art_, "SAVEGAME", listSaves(savePath_.parent_path(), *art_, 8));
    }
}

void PlayScreen::showLoadSlots() {
    if (art_ == nullptr) {
        return;
    }
    ask("LOADGAME", -1);
    if (dialog_) {
        *dialog_ = saveSlotBox(*art_, "LOADGAME", listSaves(savePath_.parent_path(), *art_));
    }
}

void PlayScreen::showLoaded() {
    ask("LOADGOOD", -1, {slotFileName(savePath_)});
    if (dialog_) {
        placeCentredDialog(*dialog_);
    }
}

void PlayScreen::loadFailed() {
    ask("LOADNOT", -1, {slotFileName(std::exchange(loadPath_, {}))});
    if (dialog_) {
        placeCentredDialog(*dialog_);
    }
}

void PlayScreen::tick(std::uint64_t milliseconds) {
    clock_ += milliseconds;
    if (colonyScreen_ && !colonyScreen_->topMessage.empty() && !cargoDrag_) {
        // Measured: a message about goods moved stays about two seconds.
        constexpr std::uint64_t kColonyMessageMs = 2000;
        colonyMessageMs_ += milliseconds;
        if (colonyMessageMs_ >= kColonyMessageMs) {
            colonyScreen_->topMessage.clear();
        }
    }
    playMusic(game_.takeMusicCues());
    view_.blinkOn = (clock_ / kBlinkMs) % 2 == 0;
    if (dialog_) {
        dialog_->shownMs += milliseconds;
        dialog_->overlayFrame = kingArmFrame(dialog_->shownMs);
    }
    if (signing_) {
        // Read from the original: each picture of the pen stays at least
        // five ticks of the game's clock (1,193,182 / 1,960 Hz), about
        // 8 ms.
        constexpr std::uint64_t kPenFrameMs = 9;
        signing_->elapsed += milliseconds;
        while (signing_ && signing_->elapsed >= kPenFrameMs) {
            signing_->elapsed -= kPenFrameMs;
            if (++signing_->frame >= signing_->strokes[signing_->stroke].frames) {
                signing_->frame = 0;
                if (++signing_->stroke >= signing_->strokes.size()) {
                    endSigning();
                }
            }
        }
    }
    if (slide_) {
        // Fast Piece Slide: twice as fast (by eye).
        const std::uint64_t slideMs = game_.gameOption(game_option::kFastSlide) ? kSlideMs / 2 : kSlideMs;
        slide_->elapsed += milliseconds;
        const auto t = std::min(slide_->elapsed, slideMs);
        const int left = static_cast<int>((slideMs - t) * kSquare / slideMs);
        view_.slideX = -slide_->dx * left;
        view_.slideY = -slide_->dy * left;
        view_.blinkOn = true;
        if (slide_->elapsed >= slideMs + (slide_->advanceAfter ? kPauseMs : 0)) {
            finishSlide();
        }
    }
    // Read from the original: with the game option End of Turn off, the
    // turn ends by itself once no unit waits for orders and nothing else
    // is on the screen. A short pause first, so the last move is seen.
    const bool idle = game_.activeUnit() < 0 && !game_.gameOption(game_option::kEndOfTurn) && !dialog_ &&
                      !view_.hiddenTerrain && notices_.empty() && !menu_ && !colonyScreen_ && !europeScreen_ &&
                      !reportUp() && routeScreen_ < 0 && !congressHallUp() && !signing_ && !slide_ && !attack_ &&
                      !viewMode_ && !game_.gameOver();
    idleMs_ = idle ? idleMs_ + milliseconds : 0;
    if (idle && idleMs_ >= kPauseMs) {
        idleMs_ = 0;
        handle(Command::EndTurn);
    }
}

void PlayScreen::endSigning() {
    signing_.reset();
    showNotices();
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
    if (signing_) {
        drawSigning(screen, art, signing_->strokes, signing_->stroke, signing_->frame);
        return;
    }
    if (congressHall_ >= 0) {
        std::vector<bool> fathers(static_cast<std::size_t>(Game::kFoundingFathers));
        for (int f = 0; f < Game::kFoundingFathers; ++f) {
            fathers[static_cast<std::size_t>(f)] = game_.hasFather(game_.human(), f);
        }
        drawCongressHall(screen, art, fathers);
        return;
    }
    if (ending_ == 11) {
        drawExploits(screen, art, game_, game_.human(), exploits_);
        return;
    }
    if (ending_ == 12) {
        drawHallOfFame(screen, art, hall_, hallPlace_);
        return;
    }
    if (pedia_) {
        drawPedia(screen, art, *pedia_, game_.human(), colonyArt_);
        return;
    }
    if (report_ != 0) {
        drawReport(screen, art, game_, game_.human(), report_, reportPage_);
        return;
    }
    drawScreen(screen, art);
    if (menu_ && !colonyScreen_ && !europeScreen_ && routeScreen_ < 0) {
        drawMenuBar(screen, art, art.menus, &*menu_);
    }
    if (analysisUp_ && attack_) {
        drawCombatAnalysis(screen, art, game_,
                           attack_->settlement >= 0 ? game_.analyseVillageAttack(attack_->attacker, attack_->settlement)
                           : attack_->colony >= 0
                               ? game_.analyseColonyAttack(attack_->attacker, attack_->colony, attack_->colonist)
                               : game_.analyseAttack(attack_->attacker, attack_->defender));
    }
    if (dialog_ && dialog_->woodcut > 0) {
        drawWoodcut(screen, art, dialog_->woodcut);
        return;
    }
    if (dialog_) {
        drawDialog(screen, art, *dialog_);
        if (dialog_->unit >= 0 && dialog_->unit < game_.unitCount()) {
            drawUnitInColonyList(screen, art, game_, dialog_->unit, dialog_->x + 5, dialog_->y + 6);
        }
    }
}

void PlayScreen::drawScreen(Screen& screen, const Art& art) const {
    if (routeScreen_ >= 0) {
        drawTradeRouteScreen(screen, art, game_, routeScreen_);
        return;
    }
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
        const int size = zoomSquare(view_.zoom);
        const int px = kMapArea.x + (lx - view_.originX) * size;
        const int py = kMapArea.y + (ly - view_.originY) * size;
        screen.setClip(kMapArea);
        screen.frame({px, py, size, size}, 0x0F);
        screen.resetClip();
    }
}

// --- Load menu ---------------------------------------------------------------

std::string saveTitle(const SaveGame& save, const Art& art, bool brackets) {
    const std::string difficulty = textLine(art.names, "DIFFICULTY", save.difficulty, "");
    const auto human = static_cast<std::size_t>(save.humanNation);
    const std::string name = human < save.players.size() ? save.players[human].name : "";
    const std::string nation = textLine(art.names, "NATIONALITY", save.humanNation, "");
    const std::string when = textLine(art.names, "SEASONS", save.season, save.season == 0 ? "Spring" : "Autumn") + " " +
                             std::to_string(save.year);
    const std::string who = difficulty + " " + name + " of the " + nation;
    return brackets ? who + " (" + when + ")" : who + ", " + when;
}

std::vector<SaveSlot> listSaves(const std::filesystem::path& folder, const Art& art, int count) {
    std::vector<SaveSlot> slots;
    for (int i = 0; i < count; ++i) {
        const std::string name = "COLONY0" + std::to_string(i) + ".SAV";
        SaveSlot slot{folder / name, "(EMPTY)", true};
        if (const auto path = findFileNoCase(folder, name)) {
            slot.path = *path;
            try {
                slot.label = saveTitle(readSaveGame(readFile(*path)), art);
                slot.empty = false;
            } catch (const FormatError&) {
                // Not a save that can be read: an empty slot.
            }
        }
        slots.push_back(std::move(slot));
    }
    return slots;
}

LoadMenu::LoadMenu(const Art& art, std::vector<SaveSlot> slots)
    : art_(&art), slots_(std::move(slots)), list_(saveSlotBox(art, "LOADGAME", slots_)) {}

namespace {
// Measured on the original: on the main menu's wooden screen the list
// has no box; the title is at row 67 and the slots 6 rows apart from row
// 75, the chosen one on a bar 8 high, a pixel narrower at each end than in
// a box.
constexpr int kTitleListTop = 67;
constexpr int kTitleListRows = 75;
constexpr int kTitleListStep = 6;
} // namespace

std::optional<SaveSlot> LoadMenu::pick(int row) {
    if (row < 0 || row >= static_cast<int>(slots_.size())) {
        return std::nullopt;
    }
    const auto& slot = slots_[static_cast<std::size_t>(row)];
    if (slot.empty) {
        return std::nullopt; // seen: an empty slot does nothing
    }
    bool good = true;
    try {
        (void)readSaveGame(readFile(slot.path));
    } catch (const FormatError&) {
        good = false;
    }
    message_ = loadMessage(*art_, slot.path, good);
    if (good) {
        loaded_ = slot;
    }
    return std::nullopt;
}

std::optional<SaveSlot> LoadMenu::handle(Command command) {
    if (message_) {
        if (command == Command::Confirm || command == Command::Cancel || command == Command::Skip) {
            message_.reset();
            return std::exchange(loaded_, std::nullopt);
        }
        return std::nullopt;
    }
    switch (command) {
    case Command::Up:
    case Command::MoveN:
        stepSelection(list_, -1);
        break;
    case Command::Down:
    case Command::MoveS:
        stepSelection(list_, 1);
        break;
    case Command::Confirm:
        return pick(list_.selected);
    default:
        break;
    }
    return std::nullopt;
}

std::optional<SaveSlot> LoadMenu::click(int x, int y) {
    if (message_) {
        return handle(Command::Confirm);
    }
    const int width = list_.width + 6;
    if (x < list_.x + 4 || x >= list_.x + width - 4 || y < kTitleListRows - 1) {
        return std::nullopt;
    }
    const int row = (y - kTitleListRows + 1) / kTitleListStep;
    if (row >= static_cast<int>(list_.options.size())) {
        return std::nullopt;
    }
    list_.selected = row;
    return pick(row);
}

void LoadMenu::draw(Screen& screen, const Art& art) const {
    screen.clear(kBlack);
    if (art.woodPanel.palette) {
        screen.setColours(*art.woodPanel.palette, 16, 255);
    }
    screen.blit(art.woodPanel.image, 0, 0);
    constexpr TextColours kListColours{kGreen, 0x80, 0x2F};
    const int width = list_.width + 6;
    for (const auto& line : list_.lines) {
        screen.text(art.tiny, list_.x + 5, kTitleListTop, line, kListColours);
    }
    // Seen: each slot is drawn after the one above, its bar over that
    // one's last row of pixels, and the list is cut off a row early.
    const int bottom = kTitleListRows + kTitleListStep * static_cast<int>(list_.options.size()) - 1;
    screen.setClip({0, 0, Screen::kWidth, bottom});
    for (std::size_t i = 0; i < list_.options.size(); ++i) {
        const int y = kTitleListRows + kTitleListStep * static_cast<int>(i);
        if (static_cast<int>(i) == list_.selected) {
            screen.fill({list_.x + 5, y - 1, width - 10, 8}, 0x8A);
        }
        screen.text(art.tiny, list_.x + 9, y, list_.options[i], kListColours);
    }
    screen.resetClip();
    if (message_) {
        drawDialog(screen, art, *message_);
    }
}

} // namespace viceroy

// --- The menu bar -------------------------------------------------------------

namespace viceroy {
namespace {

// Rows of the menus in MENU.TXT that Viceroy acts on.
namespace game_menu {
constexpr int kGameOptions = 0;
constexpr int kColonyReport = 1;
constexpr int kSoundOptions = 2;
constexpr int kPickMusic = 3;
constexpr int kSave = 4;
constexpr int kLoad = 5;
constexpr int kDeclare = 6;
constexpr int kRetire = 7;
constexpr int kExit = 8;
} // namespace game_menu
namespace view_menu {
constexpr int kMovePieces = 0;
constexpr int kViewPieces = 1;
constexpr int kEuropeanStatus = 2;
constexpr int kFindColony = 3;
constexpr int kZoomIn = 4;
constexpr int kZoomOut = 5;
constexpr int kZoomLevel = 6; // 120 x 96 down to 15 x 12 (7, 8, 9)
constexpr int kHiddenTerrain = 10;
constexpr int kCentre = 11;
} // namespace view_menu
namespace reports_menu {
constexpr int kReligion = 1;
constexpr int kCongress = 2;
constexpr int kLabor = 3;
constexpr int kEconomy = 4;
constexpr int kColony = 5;
constexpr int kNaval = 6;
constexpr int kForeign = 7;
constexpr int kIndian = 8;
constexpr int kScore = 9;
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
constexpr int kDisband = 19;
} // namespace orders_menu
namespace trade_menu {
constexpr int kEdit = 0;
constexpr int kCreate = 1;
constexpr int kDelete = 2;
} // namespace trade_menu
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
        colonyScreen_ || europeScreen_ || routeScreen_ >= 0) {
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
    if (art_ == nullptr || slide_ || dialog_ || colonyScreen_ || europeScreen_ || routeScreen_ >= 0) {
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
        pickMenuEntry(entry);
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
            pickMenuEntry(i);
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
        } else if (question_ == "CUSTOM" && colonyScreen_) {
            for (int good = 0; good < kGoodsKinds; ++good) {
                const auto g = static_cast<std::size_t>(good);
                game_.setExport(colonyScreen_->colony, good, g < d.checked.size() && d.checked[g]);
            }
        } else if (question_ == "GAMEOPTIONS") {
            // Read from the original: the eight options are set anew, the
            // other bits of the two bytes kept.
            int bits = game_.gameOptionBits() & game_option::kKept;
            for (std::size_t i = 0; i < std::size(game_option::kOrder); ++i) {
                const bool on = i < d.checked.size() && d.checked[i];
                const int bit = game_option::kOrder[i];
                if (on != (bit == game_option::kNoWaterCycling)) {
                    bits |= bit;
                }
            }
            game_.setGameOptionBits(bits);
        } else if (question_ == "COLONYOPTIONS") {
            // Read from the original: the ten options are set anew, the
            // other bits of the two bytes kept.
            int bits = game_.colonyReportBits() & ~0x3FF;
            for (std::size_t i = 0; i < std::size(colony_report::kOrder); ++i) {
                if (i >= d.checked.size() || !d.checked[i]) {
                    bits |= colony_report::kOrder[i];
                }
            }
            game_.setColonyReportBits(bits);
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

void PlayScreen::showGameOptions() {
    ask("GAMEOPTIONS", -1);
    if (!dialog_) {
        return;
    }
    dialog_->checked.assign(dialog_->options.size(), false);
    for (std::size_t i = 0; i < dialog_->checked.size() && i < std::size(game_option::kOrder); ++i) {
        const int bit = game_option::kOrder[i];
        dialog_->checked[i] = game_.gameOption(bit) != (bit == game_option::kNoWaterCycling);
    }
}

void PlayScreen::showColonyReportOptions() {
    // Read from the original: a ticked option is one whose bit is clear.
    ask("COLONYOPTIONS", -1);
    if (!dialog_) {
        return;
    }
    dialog_->checked.assign(dialog_->options.size(), false);
    for (std::size_t i = 0; i < dialog_->checked.size() && i < std::size(colony_report::kOrder); ++i) {
        dialog_->checked[i] = game_.colonyReportOn(colony_report::kOrder[i]);
    }
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

void PlayScreen::openReport(int report) {
    // Read from the original: after the declaration there is no foreign
    // affairs to report.
    if (report == 8 && game_.independenceDeclared()) {
        tell({"FOREIGNNOTAVAIL", {}});
        return;
    }
    report_ = report;
    reportPage_ = 0;
}

void PlayScreen::retire() {
    // Read from the original: once scoring is complete the game just ends.
    constexpr std::uint8_t kScored = 0x10;
    if ((game_.state().header[18] & kScored) != 0) {
        quit_ = true;
        return;
    }
    startEnding(true);
}

void PlayScreen::startEnding(bool thenQuit) {
    endingQuits_ = thenQuit;
    menu_.reset();
    ending_ = 10;
    report_ = 10;
    reportPage_ = 0;
}

void PlayScreen::advanceEnding() {
    const auto& save = game_.state();
    const int human = game_.human();
    if (ending_ == 10) {
        const int score = colonizationScore(game_, human);
        exploits_ = exploitsFor(score, save.difficulty);
        // The place in the Hall of Fame, kept in HALLFAME.DAT beside the
        // saved games as the original keeps it beside its own.
        HallEntry entry;
        entry.name = save.players[static_cast<std::size_t>(human)].name;
        entry.nation = human;
        entry.declared = (save.header[18] & 0x01) != 0;
        entry.won = (save.header[18] & 0x08) != 0;
        entry.year = save.year;
        entry.season = save.season;
        entry.difficulty = save.difficulty;
        entry.score = score;
        entry.rating = exploits_.rating;
        entry.level = exploits_.level;
        const auto file = savePath_.empty() ? std::filesystem::path() : savePath_.parent_path() / "HALLFAME.DAT";
        Bytes old;
        if (!file.empty()) {
            std::ifstream in(file, std::ios::binary);
            old.assign(std::istreambuf_iterator<char>(in), {});
        }
        hall_ = readHallOfFame(old);
        hallPlace_ = enterHallOfFame(hall_, entry);
        if (!file.empty()) {
            const Bytes bytes = writeHallOfFame(hall_);
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        if (exploits_.level >= 0) {
            ending_ = 11;
            // Read from the original: a tune for the honour, the grandest
            // for the last step.
            const int tune = exploits_.level >= 23 ? 0x24 : exploits_.level > 6 ? 0x25 : 0x21;
            playMusic({{MusicCue::Kind::Sound, tune}});
        } else {
            ending_ = 12;
        }
        report_ = 0;
        return;
    }
    if (ending_ == 11) {
        ending_ = 12;
        return;
    }
    ending_ = 0;
    if (endingQuits_) {
        quit_ = true;
    } else {
        showNotices();
    }
}

void PlayScreen::clickReport(int x, int y) {
    if (ending_ != 0) {
        advanceEnding();
        return;
    }
    if (report_ == 4 && reportPage_ == 0) {
        // Read from the original: a click on a profession zooms on it; one
        // anywhere else closes the report.
        const int hit = laborReportHit(x, y);
        if (hit >= 0) {
            reportPage_ = hit + 1;
        } else {
            report_ = 0;
        }
        return;
    }
    handle(Command::Confirm);
}

void PlayScreen::openPedia(int part) {
    pedia_ = PediaState{};
    pedia_->part = part;
    pediaFromMap_ = false;
}

void PlayScreen::openTerrainPage() {
    // Seen: F1 shows the page of the active unit's square.
    int x = -1;
    int y = -1;
    if (viewMode_) {
        x = cursorX_;
        y = cursorY_;
    } else if (const int active = game_.activeUnit(); active >= 0 && game_.onMap(active)) {
        x = game_.unit(active).x;
        y = game_.unit(active).y;
    }
    const auto& map = game_.state().map;
    if (!map.inside(x, y)) {
        return;
    }
    pedia_ = PediaState{};
    pedia_->part = pedia::kTerrain;
    pedia_->page = pediaTerrainOf(map.at(x, y));
    pediaFromMap_ = true;
}

bool PlayScreen::handlePedia(Command command) {
    auto& p = *pedia_;
    if (p.page >= 0) {
        // Seen: any key goes back to the index, or to the map after F1.
        if (pediaFromMap_) {
            pedia_.reset();
        } else {
            p.page = -1;
        }
        return true;
    }
    const int count = art_ != nullptr ? static_cast<int>(pediaEntries(*art_, p.part).size()) : 0;
    const auto move = [&](int by) {
        if (p.selected + by >= 0 && p.selected + by < count) {
            p.selected += by;
        }
    };
    switch (command) {
    case Command::MoveN:
    case Command::Up:
        move(-1);
        break;
    case Command::MoveS:
    case Command::Down:
        move(1);
        break;
    case Command::MoveW:
    case Command::Left:
        move(-kPediaRows);
        break;
    case Command::MoveE:
    case Command::Right:
        move(kPediaRows);
        break;
    case Command::Confirm:
        if (art_ != nullptr && p.selected < count) {
            p.page = pediaEntries(*art_, p.part)[static_cast<std::size_t>(p.selected)].id;
        }
        break;
    case Command::Cancel:
    case Command::Menu:
        pedia_.reset();
        break;
    default:
        break;
    }
    return true;
}

void PlayScreen::clickPedia(int x, int y) {
    if (!pedia_ || art_ == nullptr) {
        return;
    }
    if (pedia_->page >= 0) {
        handlePedia(Command::Confirm);
        return;
    }
    const int hit = pediaIndexHit(*art_, *pedia_, x, y);
    if (hit == kPediaExit) {
        pedia_.reset();
    } else if (hit >= 0) {
        pedia_->selected = hit;
        handlePedia(Command::Confirm);
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
        case game_menu::kGameOptions:
            showGameOptions();
            return true;
        case game_menu::kColonyReport:
            showColonyReportOptions();
            return true;
        case game_menu::kSoundOptions:
            showSoundOptions();
            return true;
        case game_menu::kPickMusic:
            showPickMusic();
            return true;
        case game_menu::kSave:
            showSaveSlots();
            return true;
        case game_menu::kLoad:
            showLoadSlots();
            return true;
        case game_menu::kDeclare:
            tell(game_.independenceQuestion());
            return true;
        case game_menu::kRetire:
            ask("RETIRE", -1);
            return true;
        case game_menu::kExit:
            // Seen: "Exit to DOS?", the bar on No.
            ask("DOS", -1);
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
        case view_menu::kHiddenTerrain:
            view_.hiddenTerrain = true;
            return true;
        case view_menu::kZoomIn:
            return handle(Command::ZoomIn);
        case view_menu::kZoomOut:
            return handle(Command::ZoomOut);
        case view_menu::kZoomLevel:
        case view_menu::kZoomLevel + 1:
        case view_menu::kZoomLevel + 2:
        case view_menu::kZoomLevel + 3:
            zoomTo(kZoomLevels - 1 - (entry - view_menu::kZoomLevel));
            return true;
        case view_menu::kEuropeanStatus:
            return handle(Command::Europe); // seen: the home port's screen
        case view_menu::kFindColony:
            // Seen: "Where the heck is . . .", an empty field to type in.
            ask("FINDCITY", -1);
            if (dialog_ && art_ != nullptr) {
                makeInput(*art_, *dialog_, "", 23);
            }
            return true;
        default:
            notYet();
            return true;
        }
    }
    if (section == "PEDIA") {
        if (entry >= 0 && entry < pedia::kParts) {
            openPedia(entry);
        } else {
            notYet();
        }
        return true;
    }
    if (section == "REPORTS") {
        switch (entry) {
        case reports_menu::kReligion:
            return handle(Command::ReligionReport);
        case reports_menu::kCongress:
            return handle(Command::CongressReport);
        case reports_menu::kLabor:
            return handle(Command::LaborReport);
        case reports_menu::kEconomy:
            return handle(Command::EconomyReport);
        case reports_menu::kColony:
            return handle(Command::ColonyAdviser);
        case reports_menu::kNaval:
            return handle(Command::NavalReport);
        case reports_menu::kForeign:
            return handle(Command::ForeignReport);
        case reports_menu::kIndian:
            return handle(Command::NativeReport);
        case reports_menu::kScore:
            return handle(Command::ScoreReport);
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
        case kTradeRoute:
            beginTradeRoute();
            return true;
        case kDump:
            return handle(Command::Dump);
        case kDisband:
            return handle(Command::Disband);
        case kReturnToEurope: {
            const int active = game_.activeUnit();
            if (game_.independenceDeclared()) {
                tell({"EUROPENOTLEAVE", {}});
                return true;
            }
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
    if (section == "TRADE") {
        switch (entry) {
        case trade_menu::kEdit:
            editTradeRoute();
            return true;
        case trade_menu::kCreate:
            createTradeRoute();
            return true;
        case trade_menu::kDelete:
            deleteTradeRoute();
            return true;
        default:
            break;
        }
    }
    notYet();
    return true;
}

} // namespace viceroy
