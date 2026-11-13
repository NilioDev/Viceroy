// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/ui/play.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/game/colony.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game_files.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

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
        if (game_.movesLeft(index) <= 0) {
            advance();
        } else {
            activate(index);
        }
        break;
    case MoveResult::Boarded:
        say("Aboard ship.");
        advance();
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
        const int defender = game_.canAttack(index) ? game_.defenderAt(u.x + step.dx, u.y + step.dy, index) : -1;
        if (defender >= 0 && !game_.colonyAt(u.x + step.dx, u.y + step.dy) &&
            !game_.settlementAt(u.x + step.dx, u.y + step.dy)) {
            startAttack(index, defender);
        } else {
            say("Someone else is there.");
        }
        break;
    }
    case MoveResult::HighSeas:
        say("");
        ask("SAILHOME", index);
        if (dialog_) {
            // Measured: the naval adviser stands over the box's left end.
            dialog_->x = 64;
            dialog_->y = 108;
            dialog_->portrait = kNavalAdvisor;
            dialog_->portraitX = 60;
            dialog_->portraitY = 37;
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

void PlayScreen::startAttack(int attacker, int defender) {
    attack_ = PendingAttack{attacker, defender, false};
    const int us = game_.unit(attacker).nation;
    const int them = game_.unit(defender).nation;
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
    game_.attack(fight.attacker, fight.defender);
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
    }
    if (dialog_ || notices_.empty() || art_ == nullptr) {
        return;
    }
    GameNotice notice = notices_.front();
    notices_.erase(notices_.begin());
    for (const auto& [string, nation] : notice.adjectives) {
        if (string >= 0) {
            if (notice.strings.size() <= static_cast<std::size_t>(string)) {
                notice.strings.resize(static_cast<std::size_t>(string) + 1);
            }
            notice.strings[static_cast<std::size_t>(string)] = adjective(nation);
        }
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
        constexpr std::string_view kBattles[] = {"DEMOTE", "COLONISTCAPTURE", "COLONISTCAPTURE2", "WAGONCAPTURE",
                                                 "LOOTCAPTURE", "ARTILLERY", "ARTILLERY2", "VETERAN", "VALOR"};
        if (std::find(std::begin(kBattles), std::end(kBattles), notice.section) != std::end(kBattles)) {
            placeMapDialog(*dialog_);
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

void PlayScreen::answer(const std::string& question, int unit, int choice) {
    const bool yes = choice == 0;
    const auto picked = [&]() {
        return choice >= 0 && static_cast<std::size_t>(choice) < choices_.size() ? choices_[static_cast<std::size_t>(choice)]
                                                                                 : -1;
    }();
    choices_.clear();
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
    if (question == kColonyReport) {
        if (choice == 1) {
            openColony(unit);
        }
    } else if (question == "HAVETREATY") {
        // "Cancel Action." or "Break Treaty."
        if (choice == 1 && attack_) {
            const int us = game_.unit(attack_->attacker).nation;
            const int them = game_.unit(attack_->defender).nation;
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
        case kUnitFortify: game_.setOrders(unit, SaveUnit::kFortify); break; // measured: orders 5
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
    const auto options = buildOptions(colony, save.map, game_.rules());
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
    const auto options = buildOptions(colony, save.map, game_.rules());
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
        const int items = static_cast<int>(buildOptions(colony, save.map, game_.rules()).size());
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
    view_.blinkOn = (clock_ / kBlinkMs) % 2 == 0;
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
    if (done.advanceAfter) {
        advance();
    } else if (game_.activeUnit() == done.unit) {
        activate(done.unit);
    }
}

void PlayScreen::draw(Screen& screen, const Art& art) const {
    drawScreen(screen, art);
    if (menu_ && !colonyScreen_ && !europeScreen_) {
        drawMenuBar(screen, art, art.menus, &*menu_);
    }
    if (analysisUp_ && attack_) {
        drawCombatAnalysis(screen, art, game_, game_.analyseAttack(attack_->attacker, attack_->defender));
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
constexpr int kSave = 4;
constexpr int kLoad = 5;
constexpr int kExit = 8;
} // namespace game_menu
namespace view_menu {
constexpr int kMovePieces = 0;
constexpr int kViewPieces = 1;
constexpr int kCentre = 11;
} // namespace view_menu
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
