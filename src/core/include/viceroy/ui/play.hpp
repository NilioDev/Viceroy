// SPDX-License-Identifier: GPL-3.0-or-later
//
// The screens the player moves through, as plain logic: what each command
// does, independent of whether it came from the keyboard, a gamepad or the
// mouse. The app translates input into commands and shows the Screen.
#pragma once

#include "viceroy/game/game.hpp"
#include "viceroy/render/colony_view.hpp"
#include "viceroy/render/dialog.hpp"
#include "viceroy/render/europe_view.hpp"
#include "viceroy/render/game_view.hpp"
#include "viceroy/render/menu_bar.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace viceroy {
namespace sound {
class BackgroundMusic;
} // namespace sound

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
    ViewMode, // V: a square cursor to look around; Enter opens a colony
    MoveMode, // M: back to moving units
    Europe,   // E: the home port
    Left,     // menus and screens that list things side by side
    Right,
    Recruit,  // R, P, T: the buttons in Europe; on the map R builds a road
              // and P plows or clears the forest
    Purchase,
    Train,
    Menu, // F10: the menu bar
    GoTo, // G: send the unit to a colony (a ship also to Europe)
    CongressReport, // F3
};

// The main map screen while playing.
class PlayScreen {
public:
    PlayScreen(Game game, std::filesystem::path savePath);

    // Returns false for Cancel when there is nothing to close, so the app
    // can go back to its menu.
    bool handle(Command command);
    // The player clicked this map square.
    void clickSquare(int x, int y);
    // A click on the small map: the view goes there.
    void clickMinimap(int x, int y);
    // Moves the view without moving anyone (the right stick).
    void scrollView(int dx, int dy);
    // Time passes: the active unit flashes.
    void tick(std::uint64_t milliseconds);
    void draw(Screen& screen, const Art& art) const;

    const Game& game() const { return game_; }
    Game& game() { return game_; }
    const ViewState& view() const { return view_; }
    bool waitingForEndOfTurn() const { return game_.activeUnit() < 0; }
    const std::string& message() const { return view_.message; }
    // Set by the app after loading Art: needed for colony names.
    void setColonyNames(const TextFile* names) {
        colonyNames_ = names;
        if (names != nullptr) {
            game_.setColonyNames(*names);
        }
    }
    void setColonyArt(const ColonyArt* art) { colonyArt_ = art; }
    // The colony screen, when one is open.
    const std::optional<ColonyViewState>& colonyScreen() const { return colonyScreen_; }
    void openColony(int index);
    // A click on the colony screen: the panel buttons, CHANGE, and the list
    // of things to build. Returns true when it was used.
    bool clickColonyScreen(int x, int y);
    // The home port in Europe, when it is shown.
    const std::optional<EuropeViewState>& europeScreen() const { return europeScreen_; }
    void openEurope();
    // A question in a message box, when one is up.
    const std::optional<Dialog>& dialog() const { return dialog_; }
    bool combatAnalysisUp() const { return analysisUp_; }
    // The hall of the Continental Congress with a new father in it.
    bool congressHallUp() const { return congressHall_ >= 0; }
    bool reportUp() const { return congressReport_; }
    // Set by the app after loading Art: needed for message boxes.
    void setArt(const Art* art) { art_ = art; }
    // The music the Sound Options and Pick Music change (none: they only
    // change the savegame).
    void setMusic(sound::BackgroundMusic* music);
    // The colony opened last, or -1.
    int lastColony() const { return lastColony_; }
    // A line in the panel, as the game's own messages.
    void showMessage(std::string text) { say(std::move(text)); }
    bool viewMode() const { return viewMode_; }
    int cursorX() const { return cursorX_; }
    int cursorY() const { return cursorY_; }
    // Writes the game to the slot it was loaded from. Returns false and
    // sets the message on failure.
    bool save();
    // Units glide from square to square, and the game pauses a moment
    // before the next unit when one has used up its moves. Off by default
    // (tests); the app turns it on.
    void setAnimations(bool on) { animations_ = on; }
    bool animating() const { return slide_.has_value(); }

    // The menu bar. Needs setArt. A menu opened from the keyboard or a
    // gamepad starts with its first entry highlighted.
    void openMenu(int index, bool keyboard);
    void closeMenu() { menu_.reset(); }
    const std::optional<OpenMenu>& menu() const { return menu_; }
    // A click while playing on the main screen: the menu bar, an open
    // menu, or neither (false: the click is for the map).
    bool clickMenu(int x, int y);
    // A letter typed while a menu is open picks the entry with that gold
    // letter.
    void menuLetter(char letter);
    // A letter typed while a box of options is open turns that option on
    // or off.
    void dialogLetter(char letter);
    // Exit to DOS was picked.
    bool quitRequested() const { return quit_; }
    // Load Game was picked with the mouse: back to the load menu.
    bool loadRequested() const { return quitToLoad_; }

private:
    struct Slide {
        int unit = -1;
        int dx = 0; // the step taken
        int dy = 0;
        std::uint64_t elapsed = 0;
        bool advanceAfter = false; // the unit's moves are used up
    };
    void finishSlide();
    std::vector<EntryState> menuStates(int index) const;
    bool handleMenu(Command command);
    bool pickMenuEntry(int entry); // false: back to the load menu
    void stepMenu(int delta);
    void notYet();
    bool handleCheckboxes(Command command);
    void showSoundOptions();
    void showPickMusic();
    void pickTune(int tune);
    void playMusic(const std::vector<MusicCue>& cues);
    void moveActive(Step step);
    void activate(int index);
    void advance(); // to the next unit that needs orders
    void say(std::string text);
    bool handleColonyScreen(Command command);
    bool handleBuildList(Command command);
    void openBuildList();
    void openBuy();
    // Go To: the list of colonies (and Europe for a ship), ten to a page.
    void openGoTo(int page);
    void goTo(int choice);
    bool handleEuropeScreen(Command command);
    bool handleDialog(Command command);
    void drawScreen(Screen& screen, const Art& art) const;
    void ask(std::string_view section, int unit, std::vector<std::string> strings = {}, int nation = -1,
             std::vector<int> numbers = {});
    void tellPriceMove(int good, int move);
    void askBackTaxes(int good);
    void openRecruit();
    void openPurchase();
    void openTrain();
    void answer(const std::string& question, int unit, int choice);
    void work(int orders); // P and R
    // A message box with the frontiersman; waits its turn behind others.
    void tell(GameNotice notice);
    void showNotices(); // the next waiting message, when no box is up
    // A fight, step by step: the treaty question, the tired men, the
    // Combat Analysis box, then the result.
    void startAttack(int attacker, int defender, int colony = -1);
    void attackVillage(int unit);
    void continueAttack();
    void resolveAttack();
    std::string adjective(int nation) const; // "French"

    Game game_;
    ViewState view_;
    std::filesystem::path savePath_;
    std::vector<int> waiting_; // units put off with Wait this turn
    std::uint64_t clock_ = 0;
    const TextFile* colonyNames_ = nullptr;
    const ColonyArt* colonyArt_ = nullptr;
    std::optional<ColonyViewState> colonyScreen_;
    std::optional<EuropeViewState> europeScreen_;
    std::optional<Dialog> dialog_;
    sound::BackgroundMusic* music_ = nullptr;
    std::string question_; // the GAME.TXT section the dialog asks
    int questionUnit_ = -1;
    std::vector<int> choices_; // what each choice of a list stands for
    int goToPage_ = 0;
    // The rows of @UNITOPTIONS a unit's options box offers, top to bottom.
    enum UnitOption { kUnitFront, kUnitClear, kUnitSentry, kUnitFortify, kUnitNoChange };
    std::vector<int> unitOptions_;
    void openUnitOptions(int unit, bool first);
    // A unit at a native settlement: what each option of the greeting does.
    enum VillageOption {
        kVillageTrade,
        kVillageLive,
        kVillageMission,
        kVillageSpeak,
        kVillageTribute,
        kVillageIncite,
        kVillageDenounce,
        kVillageAttack,
        kVillageCancel
    };
    std::vector<int> villageOptions_;
    int village_ = -1;
    int welcomeTribe_ = -1; // the tribe whose welcome is up
    int scoutColony_ = -1;  // the colony a scout has reached (SCOUTCOLONY)
    int congressHall_ = -1; // the father just come into the hall, shown until a key
    bool congressReport_ = false; // F3, shown until a key or a click
    int inciteTarget_ = -1; // the nation a missionary would have the tribe attack
    void startIncite(int unit);
    void scoutColony(int unit, int colony, int choice);
    void endIncite(int unit);
    void enterVillage(int unit, int settlement);
    void showLesson(int unit, const std::string& section, const std::vector<std::string>& strings);
    // What the chief answers a scout, shown after his greeting.
    struct PendingChief {
        int unit = -1; // -1 when the scout is gone
        std::string section;
        std::vector<std::string> strings;
        std::vector<int> numbers;
    };
    std::optional<PendingChief> chief_;
    void speakWithChief(int unit);
    void showChiefAnswer();
    void foundMission(int unit);
    void demandTribute(int unit);
    void placeNative(int tribe, int mood = -1);
    // A trade at a settlement, step by step.
    struct VillageTrade {
        int unit = -1;
        Game::VillageSale sale;
        Game::VillagePurchase purchase;
        std::vector<int> holds;    // the TRADEWHICH list
        std::vector<int> forSale;  // the BUYWHICH list
        int sold = -1;             // the good offered, -1 for none
        int amount = 0;            // and how much
    };
    std::optional<VillageTrade> trade_;
    void startTrade(int unit);
    void offerHold(int hold);
    void showSale();
    void afterSale();
    void offerToBuy();
    void showPurchase();
    void endTrade(const std::string& section, std::vector<std::string> strings, std::vector<int> numbers = {});
    std::string goodName(int good) const;
    std::vector<GameNotice> notices_; // messages still to show, first first
    const Art* art_ = nullptr;
    int lastColony_ = -1;
    bool viewMode_ = false;
    int cursorX_ = 0;
    int cursorY_ = 0;
    bool animations_ = false;
    std::optional<Slide> slide_;
    std::optional<OpenMenu> menu_;
    bool quit_ = false;
    bool quitToLoad_ = false;
    struct PendingAttack {
        int attacker = -1;
        int defender = -1;
        bool tiredAsked = false;
        int colony = -1;   // an undefended colony: its colonist defends
        int colonist = -1;
        int settlement = -1; // a native settlement: a brave or its people defend
    };
    int defenderNation(const PendingAttack& a) const;
    int colonyAfterNotices_ = -1; // a colony just taken opens once its news is read
    std::optional<PendingAttack> attack_;
    bool advanceAfterNotices_ = false; // the next unit once the news is read
    bool analysisUp_ = false; // the Combat Analysis box waits for a key
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
    // A click on a row picks that savegame.
    std::optional<SaveSlot> click(int x, int y);
    void draw(Screen& screen, const Art& art) const;
    bool empty() const { return slots_.empty(); }

private:
    std::vector<SaveSlot> slots_;
    int selected_ = 0;
};

} // namespace viceroy
