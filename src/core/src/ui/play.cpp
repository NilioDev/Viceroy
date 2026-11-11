// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/ui/play.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/game/colony.hpp"
#include "viceroy/game_files.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace viceroy {

namespace {

constexpr std::uint64_t kBlinkMs = 250;
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
    if (active >= 0 && active < game_.unitCount() && game_.needsOrders(active)) {
        activate(active);
    } else {
        activate(game_.nextUnitNeedingOrders(-1));
    }
    if (game_.activeUnit() < 0) {
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
    } else {
        say("End of Turn. Press Enter.");
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
    switch (game_.move(index, step)) {
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
    case MoveResult::Occupied:
        say("Someone else is there.");
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

bool PlayScreen::handleColonyScreen(Command command) {
    auto& state = *colonyScreen_;
    auto& save = game_.state();
    auto& colony = save.colonies[static_cast<std::size_t>(state.colony)];
    const int people = colony.population;
    if (state.jobsMenu >= 0) {
        const int lines = static_cast<int>(jobChoices(colony, save.map, game_.rules(), save.colonies,
                                                      state.selectedColonist).size());
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
            assignJob(colony, save.map, game_.rules(), save.colonies, state.selectedColonist, state.jobsMenu);
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
    case Command::Confirm:
    case Command::Skip:
        if (state.selectedColonist >= 0 && state.selectedColonist < people) {
            // The jobs menu opens on the colonist's present job.
            state.jobsMenu = std::clamp<int>(colony.jobs[static_cast<std::size_t>(state.selectedColonist)], 0, 17);
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
    const int active = game_.activeUnit();
    if (active < 0) {
        return;
    }
    const auto& u = game_.unit(active);
    const int dx = std::clamp(x - u.x, -1, 1);
    const int dy = std::clamp(y - u.y, -1, 1);
    for (const auto& step : kSteps) {
        if (step.dx == dx && step.dy == dy) {
            moveActive(step);
            return;
        }
    }
}

bool PlayScreen::handle(Command command) {
    if (colonyScreen_) {
        return handleColonyScreen(command);
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
    case Command::EndTurn:
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
        break;
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
}

void PlayScreen::draw(Screen& screen, const Art& art) const {
    if (colonyScreen_ && colonyArt_ != nullptr) {
        drawColonyScreen(screen, art, *colonyArt_, game_, *colonyScreen_);
        return;
    }
    drawGameScreen(screen, art, game_, view_);
    if (viewMode_ && view_.blinkOn) {
        const int px = kMapArea.x + (cursorX_ - view_.originX) * kSquare;
        const int py = kMapArea.y + (cursorY_ - view_.originY) * kSquare;
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
