// SPDX-License-Identifier: GPL-3.0-or-later
//
// The screens the player moves through, as plain logic: what each command
// does, independent of whether it came from the keyboard, a gamepad or the
// mouse. The app translates input into commands and shows the Screen.
#pragma once

#include "viceroy/game/game.hpp"
#include "viceroy/render/game_view.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace viceroy {

enum class Command {
    MoveN,
    MoveNE,
    MoveE,
    MoveSE,
    MoveS,
    MoveSW,
    MoveW,
    MoveNW,
    Confirm,     // Enter / A: end the turn when asked, pick in a menu
    Cancel,      // Escape / B: leave a menu
    Skip,        // Space: no orders this turn
    Wait,        // W: come back to this unit later
    Sentry,      // S
    Fortify,     // F
    BuildColony, // B
    EndTurn,     // ends the turn even with units left to move
    NextUnit,
    PreviousUnit,
    Centre, // C: centre the map on the active unit
    Save,
    Up, // menus
    Down,
};

// The main map screen while playing.
class PlayScreen {
public:
    PlayScreen(Game game, std::filesystem::path savePath);

    void handle(Command command);
    // Time passes: the active unit flashes.
    void tick(std::uint64_t milliseconds);
    void draw(Screen& screen, const Art& art) const;

    const Game& game() const { return game_; }
    Game& game() { return game_; }
    const ViewState& view() const { return view_; }
    bool waitingForEndOfTurn() const { return game_.activeUnit() < 0; }
    const std::string& message() const { return view_.message; }
    // Set by the app after loading Art: needed for colony names.
    void setColonyNames(const TextFile* names) { colonyNames_ = names; }
    // Writes the game to the slot it was loaded from. Returns false and
    // sets the message on failure.
    bool save();

private:
    void moveActive(Step step);
    void activate(int index);
    void advance(); // to the next unit that needs orders
    void say(std::string text);

    Game game_;
    ViewState view_;
    std::filesystem::path savePath_;
    std::vector<int> waiting_; // units put off with Wait this turn
    std::uint64_t clock_ = 0;
    const TextFile* colonyNames_ = nullptr;
};

// One line of the load menu.
struct SaveSlot {
    std::filesystem::path path;
    std::string label; // "COLONY03  Dutch  Spring 1632"
};

// The savegames in a folder, COLONY00 to COLONY09, that can be read.
std::vector<SaveSlot> listSaves(const std::filesystem::path& folder, const Art& art);

class LoadMenu {
public:
    explicit LoadMenu(std::vector<SaveSlot> slots) : slots_(std::move(slots)) {}
    // Returns the chosen slot when the player confirms.
    std::optional<SaveSlot> handle(Command command);
    void draw(Screen& screen, const Art& art) const;
    bool empty() const { return slots_.empty(); }

private:
    std::vector<SaveSlot> slots_;
    int selected_ = 0;
};

} // namespace viceroy
