// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Viceroy app: opens a window, turns keyboard, mouse and gamepad input
// into commands for the current screen, and shows the 320×200 screen the
// core draws, scaled sharply to the window.
//
// On first start the player hands over their own copy of the game once (a
// folder or the zip it came in); it is checked and imported into Viceroy's
// own library and used from there on.
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "audio.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/data/savegame.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/pack/library.hpp"
#include "viceroy/render/game_view.hpp"
#include "viceroy/render/screen.hpp"
#include "viceroy/game/new_game.hpp"
#include "viceroy/ui/debug_menu.hpp"
#include "viceroy/ui/new_game_flow.hpp"
#include "viceroy/ui/play.hpp"
#include "viceroy/ui/title.hpp"
#include "viceroy/version.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using viceroy::Command;

constexpr int kScreenW = viceroy::Screen::kWidth;
constexpr int kScreenH = viceroy::Screen::kHeight;
constexpr Uint64 kStickRepeatMs = 220;
constexpr float kStickThreshold = 0.6f;

enum class Mode { Welcome, Intro, Title, Load, NewGame, Play };

struct App {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* frame = nullptr;
    std::string status;
    std::filesystem::path library;
    Mode mode = Mode::Welcome;
    bool dialogOpen = false;
    Uint32 pickedEvent = 0; // our event type for a picked file or folder

    std::unique_ptr<viceroy::Art> art;
    std::unique_ptr<viceroy::TitleArt> title;
    std::unique_ptr<viceroy::MainMenu> mainMenu;
    // The opening while it plays, and its last picture behind the menu
    // when it played to the end.
    std::unique_ptr<viceroy::Intro> intro;
    std::unique_ptr<viceroy::LoadMenu> load;
    std::unique_ptr<viceroy::NewGameArt> newGameArt;
    std::unique_ptr<viceroy::NewGameFlow> newGame;
    std::unique_ptr<viceroy::ColonyArt> colonyArt;
    std::unique_ptr<viceroy::PlayScreen> play;
    viceroy::DebugMenu debug;
    Audio audio;
    viceroy::Screen screen;
    Uint64 lastTicks = 0;

    // Left stick: the direction held and when it last fired.
    std::optional<Command> stickCommand;
    Uint64 stickFired = 0;
    float stickX = 0;
    float stickY = 0;
    // Right stick: scrolls the map, repeating while held.
    float lookX = 0;
    float lookY = 0;
    Uint64 lookFired = 0;

    // The original draws its own mouse pointer (CURSOR.SS sprite 0) into
    // the picture; ours shows once the mouse moves and hides again while
    // the gamepad is used.
    bool pointerShown = false;
    float pointerX = 0;
    float pointerY = 0;
};

void drawPointer(App& app) {
    if (!app.pointerShown || app.art->cursor.sprites.empty()) {
        return;
    }
    const auto& image = app.art->cursor.sprites.front().image;
    const int left = static_cast<int>(app.pointerX);
    const int top = static_cast<int>(app.pointerY);
    for (int j = 0; j < image.height; ++j) {
        for (int i = 0; i < image.width; ++i) {
            const auto c = image.pixels[static_cast<std::size_t>(j * image.width + i)];
            // The sheet marks the sprite's box with a pixel of colour 9 in
            // two corners; the original does not draw those.
            const bool marker = c == 0x09 && (i == image.width - 1 || j == image.height - 1);
            if (c != viceroy::kTransparent && !marker) {
                app.screen.pixel(left + i, top + j, c);
            }
        }
    }
}

// Where the imported game, savegames and later add-ons live.
std::filesystem::path libraryFolder() {
    std::filesystem::path folder = std::filesystem::current_path() / "library";
    if (char* pref = SDL_GetPrefPath("Viceroy", "Viceroy")) {
        folder = std::filesystem::path(pref) / "library";
        SDL_free(pref);
    }
    return folder;
}

// Where to look for game files to import without asking: next to the
// AppImage file (the program itself runs from a read-only mount), next to the
// program, in the current folder, and in the app's data folder.
std::optional<std::filesystem::path> findGameToImport() {
    std::vector<std::filesystem::path> candidates;
    if (const char* appImage = SDL_getenv("APPIMAGE")) {
        candidates.push_back(std::filesystem::path(appImage).parent_path() / "gamedata");
    }
    if (const char* base = SDL_GetBasePath()) {
        candidates.push_back(std::filesystem::path(base) / "gamedata");
    }
    candidates.push_back(std::filesystem::current_path() / "gamedata");
    if (char* pref = SDL_GetPrefPath("Viceroy", "Viceroy")) {
        candidates.push_back(std::filesystem::path(pref) / "gamedata");
        SDL_free(pref);
    }
    for (const auto& folder : candidates) {
        if (viceroy::isGameFolder(folder)) {
            return folder;
        }
    }
    return std::nullopt;
}

viceroy::Bytes readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

void showLoadMenu(App& app) {
    app.audio.setPlaying(false);
    app.play.reset();
    app.load = std::make_unique<viceroy::LoadMenu>(
        viceroy::listSaves(viceroy::pack::savesFolder(app.library), *app.art));
    app.mode = Mode::Load;
    app.debug = viceroy::DebugMenu{};
}

// The day this program was built, the way the original dates itself:
// "7-Feb-95".
std::string buildDate() {
    const std::string date = __DATE__; // "Nov 12 2026"
    const int day = std::atoi(date.substr(4, 2).c_str());
    return std::to_string(day) + "-" + date.substr(0, 3) + "-" + date.substr(9, 2);
}

// The original's main menu, or the load menu when its pictures are missing.
void showTitle(App& app) {
    if (!app.title) {
        showLoadMenu(app);
        return;
    }
    app.audio.setPlaying(false);
    app.play.reset();
    if (app.intro && !app.intro->finished()) {
        app.intro.reset(); // skipped: the still title picture instead
    }
    app.mainMenu = std::make_unique<viceroy::MainMenu>(*app.art, std::string(viceroy::kVersion), buildDate());
    app.mode = Mode::Title;
}

void beginPlay(App& app, viceroy::Game game, const std::filesystem::path& path) {
    app.play = std::make_unique<viceroy::PlayScreen>(std::move(game), path);
    app.play->setAnimations(true);
    app.play->setColonyNames(&app.art->colonyNames);
    app.play->setColonyArt(app.colonyArt.get());
    app.play->setArt(app.art.get());
    app.mode = Mode::Play;
    app.audio.music().options = viceroy::sound::SoundOptions::fromSave(app.play->game().soundOptionBits());
    app.play->setMusic(&app.audio.music());
    app.audio.setPlaying(true);
}

// Where a new game is saved: the first free slot, or else the one saved
// longest ago.
std::filesystem::path newGameSlot(const std::filesystem::path& folder) {
    std::filesystem::path oldest;
    std::filesystem::file_time_type oldestTime{};
    for (int i = 0; i < 10; ++i) {
        const std::string name = "COLONY0" + std::to_string(i) + ".SAV";
        const auto found = viceroy::findFileNoCase(folder, name);
        if (!found) {
            return folder / name;
        }
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(*found, ec);
        if (oldest.empty() || (!ec && t < oldestTime)) {
            oldest = *found;
            oldestTime = t;
        }
    }
    return oldest;
}

void startNewGame(App& app) {
    try {
        const auto base = viceroy::pack::baseFolder(app.library);
        const auto mapPath = viceroy::findFileNoCase(base, "AMER2.MP");
        if (!mapPath) {
            throw viceroy::FormatError("missing AMER2.MP");
        }
        auto setup = app.newGame->setup();
        setup.seed = static_cast<std::uint32_t>(SDL_GetTicksNS() ^ static_cast<Uint64>(std::time(nullptr)));
        auto save = viceroy::newAmericaGame(readAll(*mapPath), app.art->rules, setup);
        const auto folder = viceroy::pack::savesFolder(app.library);
        std::filesystem::create_directories(folder);
        beginPlay(app, viceroy::Game(std::move(save), app.art->rules), newGameSlot(folder));
    } catch (const std::exception& e) {
        SDL_Log("No new game: %s", e.what());
        showTitle(app);
        if (app.mainMenu) {
            app.mainMenu->say(std::string("Cannot start a game: ") + e.what());
        }
    }
    app.newGame.reset();
}

void pickTitle(App& app, viceroy::TitleChoice choice) {
    if (choice == viceroy::TitleChoice::Load) {
        showLoadMenu(app);
    } else if (choice == viceroy::TitleChoice::America) {
        try {
            if (!app.newGameArt) {
                app.newGameArt = std::make_unique<viceroy::NewGameArt>(
                    viceroy::loadNewGameArt(viceroy::pack::baseFolder(app.library)));
            }
            app.newGame = std::make_unique<viceroy::NewGameFlow>(*app.art);
            app.mode = Mode::NewGame;
        } catch (const std::exception& e) {
            app.mainMenu->say(std::string("Cannot start a game: ") + e.what());
        }
    } else {
        app.mainMenu->say("Not in Viceroy yet.");
    }
}

// After a key or click in the new game screens.
void afterNewGameStep(App& app) {
    if (!app.newGame) {
        return;
    }
    using Step = viceroy::NewGameFlow::Step;
    if (app.newGame->typing()) {
        SDL_StartTextInput(app.window);
    } else {
        SDL_StopTextInput(app.window);
    }
    if (app.newGame->step() == Step::Cancelled) {
        app.newGame.reset();
        showTitle(app);
    } else if (app.newGame->step() == Step::Done) {
        startNewGame(app);
    }
}

// Loads the pictures and rules from the library, then shows the main menu.
void openLibrary(App& app) {
    try {
        app.art = std::make_unique<viceroy::Art>(viceroy::loadArt(viceroy::pack::baseFolder(app.library)));
        app.colonyArt =
            std::make_unique<viceroy::ColonyArt>(viceroy::loadColonyArt(viceroy::pack::baseFolder(app.library)));
        try {
            app.title = std::make_unique<viceroy::TitleArt>(
                viceroy::loadTitleArt(viceroy::pack::baseFolder(app.library)));
        } catch (const std::exception& e) {
            SDL_Log("No opening: %s", e.what());
            app.title.reset();
        }
        if (app.title) {
            app.intro = std::make_unique<viceroy::Intro>(*app.title);
            app.mode = Mode::Intro;
        } else {
            showTitle(app);
        }
        app.status.clear();
        app.audio.open(viceroy::pack::baseFolder(app.library));
    } catch (const std::exception& e) {
        app.art.reset();
        app.status = std::string("Cannot read the imported game: ") + e.what() + " Drop it here again.";
        app.mode = Mode::Welcome;
    }
}

void startGame(App& app, const viceroy::SaveSlot& slot) {
    try {
        beginPlay(app, viceroy::Game(viceroy::readSaveGame(readAll(slot.path)), app.art->rules), slot.path);
    } catch (const viceroy::FormatError& e) {
        app.status = std::string("Cannot read this savegame: ") + e.what();
    }
}

// Imports the player's game from a folder or zip, then opens it.
void importAndOpen(App& app, const std::filesystem::path& source) {
    try {
        const auto result = viceroy::pack::importGame(source, app.library);
        SDL_Log("Imported %zu game files into %s", result.files.size(), app.library.string().c_str());
        openLibrary(app);
    } catch (const viceroy::pack::ImportError& e) {
        SDL_Log("Import failed: %s", e.what());
        app.status = e.what();
    }
}

// File dialogs answer on another thread; hand the choice to the event loop.
void SDLCALL onPicked(void* userdata, const char* const* files, int) {
    auto* app = static_cast<App*>(userdata);
    SDL_Event event{};
    event.type = app->pickedEvent;
    event.user.data1 = (files != nullptr && files[0] != nullptr) ? SDL_strdup(files[0]) : nullptr;
    SDL_PushEvent(&event);
}

void chooseFolder(App& app) {
    if (!app.dialogOpen) {
        app.dialogOpen = true;
        SDL_ShowOpenFolderDialog(onPicked, &app, app.window, nullptr, false);
    }
}

void chooseZip(App& app) {
    static const SDL_DialogFileFilter kZip{"Zip archive", "zip"};
    if (!app.dialogOpen) {
        app.dialogOpen = true;
        SDL_ShowOpenFileDialog(onPicked, &app, app.window, &kZip, 1, nullptr, false);
    }
}

// --- The welcome screen, shown before we have the game's own fonts ---------

void drawText(App& app, float y, const std::string& text) {
    const float x = (kScreenW - static_cast<float>(text.size()) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) / 2;
    SDL_RenderDebugText(app.renderer, std::max(2.0f, x), y, text.c_str());
}

void drawWelcome(App& app) {
    SDL_SetRenderDrawColor(app.renderer, 236, 196, 92, 255);
    drawText(app, 40, "Welcome to Viceroy");
    SDL_SetRenderDrawColor(app.renderer, 220, 220, 220, 255);
    drawText(app, 64, "Viceroy needs your own copy");
    drawText(app, 74, "of the DOS game, once.");
    drawText(app, 98, "Drop its folder or zip here,");
    drawText(app, 114, "Enter / (A): choose its folder");
    drawText(app, 126, "Z / (Y): choose its zip file");
    drawText(app, 150, "Esc: quit");

    // Why the last try failed, wrapped to the screen width.
    SDL_SetRenderDrawColor(app.renderer, 240, 120, 100, 255);
    constexpr std::size_t kColumns = kScreenW / SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE - 2;
    std::string line;
    float y = 170;
    std::size_t start = 0;
    while (start < app.status.size()) {
        auto end = app.status.find(' ', start);
        if (end == std::string::npos) {
            end = app.status.size();
        }
        const std::string word = app.status.substr(start, end - start);
        if (!line.empty() && line.size() + 1 + word.size() > kColumns) {
            drawText(app, y, line);
            y += 10;
            line.clear();
        }
        line += (line.empty() ? "" : " ") + word;
        start = end + 1;
    }
    if (!line.empty()) {
        drawText(app, y, line);
    }
}

// --- Input -------------------------------------------------------------------

std::optional<Command> keyCommand(const SDL_KeyboardEvent& key) {
    const bool ctrl = (key.mod & SDL_KMOD_CTRL) != 0;
    switch (key.key) {
    case SDLK_KP_8:
    case SDLK_UP:
        return Command::MoveN;
    case SDLK_KP_9:
    case SDLK_PAGEUP:
        return Command::MoveNE;
    case SDLK_KP_6:
    case SDLK_RIGHT:
        return Command::MoveE;
    case SDLK_KP_3:
    case SDLK_PAGEDOWN:
        return Command::MoveSE;
    case SDLK_KP_2:
    case SDLK_DOWN:
        return Command::MoveS;
    case SDLK_KP_1:
    case SDLK_END:
        return Command::MoveSW;
    case SDLK_KP_4:
    case SDLK_LEFT:
        return Command::MoveW;
    case SDLK_KP_7:
    case SDLK_HOME:
        return Command::MoveNW;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return Command::Confirm;
    case SDLK_SPACE:
        return Command::Skip;
    case SDLK_W:
        return Command::Wait;
    case SDLK_S:
        return ctrl ? Command::Save : Command::Sentry;
    case SDLK_F:
        return Command::Fortify;
    case SDLK_B:
        return Command::BuildColony;
    case SDLK_C:
        return Command::Centre;
    case SDLK_G:
        return Command::GoTo;
    case SDLK_V:
        return Command::ViewMode;
    case SDLK_M:
        return Command::MoveMode;
    case SDLK_E:
        return ctrl ? Command::EndTurn : Command::Europe;
    case SDLK_R:
        return Command::Recruit;
    case SDLK_P:
        return Command::Purchase;
    case SDLK_T:
        return Command::Train;
    case SDLK_TAB:
        return (key.mod & SDL_KMOD_SHIFT) ? Command::PreviousUnit : Command::NextUnit;
    case SDLK_F3:
        return Command::CongressReport;
    case SDLK_F10:
        return Command::Menu;
    default:
        return std::nullopt;
    }
}

// `pointing`: the colony screen or the view mode's cursor is up, so A and B
// confirm and go back.
std::optional<Command> buttonCommand(Uint8 button, bool waitingForEndOfTurn, bool pointing) {
    if (pointing && button == SDL_GAMEPAD_BUTTON_SOUTH) {
        return Command::Confirm;
    }
    if (pointing && button == SDL_GAMEPAD_BUTTON_EAST) {
        return Command::Cancel;
    }
    switch (button) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return Command::MoveN;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return Command::MoveS;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return Command::MoveW;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return Command::MoveE;
    case SDL_GAMEPAD_BUTTON_SOUTH: // A
        return waitingForEndOfTurn ? Command::Confirm : Command::Skip;
    case SDL_GAMEPAD_BUTTON_EAST: // B
        return Command::Sentry;
    case SDL_GAMEPAD_BUTTON_WEST: // X
        return Command::BuildColony;
    case SDL_GAMEPAD_BUTTON_NORTH: // Y
        return Command::EndTurn;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return Command::PreviousUnit;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return Command::NextUnit;
    case SDL_GAMEPAD_BUTTON_START:
        return Command::Menu;
    case SDL_GAMEPAD_BUTTON_BACK:
        return Command::ViewMode;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        return Command::Centre;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return Command::Fortify;
    case SDL_GAMEPAD_BUTTON_MISC1: // Share, Capture or the microphone button
        return Command::Europe;
    default:
        return std::nullopt;
    }
}

// The stick's direction in eight sectors, or nothing near the middle.
std::optional<Command> stickDirection(float x, float y) {
    if (std::hypot(x, y) < kStickThreshold) {
        return std::nullopt;
    }
    static constexpr Command kByAngle[] = {Command::MoveE, Command::MoveSE, Command::MoveS, Command::MoveSW,
                                           Command::MoveW, Command::MoveNW, Command::MoveN, Command::MoveNE};
    const float angle = std::atan2(y, x); // y grows downwards
    const long sector = std::lround(angle / (3.14159265f / 4.0f));
    return kByAngle[static_cast<std::size_t>((sector + 8) % 8)];
}

void send(App& app, Command command) {
    switch (app.mode) {
    case Mode::Welcome:
        if (command == Command::Confirm) {
            chooseFolder(app);
        }
        break;
    case Mode::Intro:
        showTitle(app);
        break;
    case Mode::Title:
        if (auto choice = app.mainMenu->handle(command)) {
            pickTitle(app, *choice);
        }
        break;
    case Mode::Load:
        if (command == Command::Cancel) {
            if (app.title) {
                showTitle(app);
            }
            break;
        }
        if (auto slot = app.load->handle(command == Command::Skip ? Command::Confirm : command)) {
            startGame(app, *slot);
        }
        break;
    case Mode::NewGame:
        app.newGame->handle(command);
        afterNewGameStep(app);
        break;
    case Mode::Play:
        if (app.debug.open()) {
            const std::string result = app.debug.handle(command, *app.play);
            if (!result.empty()) {
                app.play->showMessage(result);
            }
            break;
        }
        if (!app.play->handle(command)) {
            showLoadMenu(app);
        }
        break;
    }
}

// A click on the map: open a colony there, move the active unit to a square
// next to it, or bring the map round to any other square. A click on the
// small map moves the view there.
void click(App& app, float wx, float wy) {
    float x = 0;
    float y = 0;
    SDL_RenderCoordinatesFromWindow(app.renderer, wx, wy, &x, &y);
    if (app.mode == Mode::Intro) {
        showTitle(app);
        return;
    }
    if (app.mode == Mode::Title) {
        if (auto choice = app.mainMenu->click(static_cast<int>(x), static_cast<int>(y))) {
            pickTitle(app, *choice);
        }
        return;
    }
    if (app.mode == Mode::Load) {
        if (auto slot = app.load->click(static_cast<int>(x), static_cast<int>(y))) {
            startGame(app, *slot);
        }
        return;
    }
    if (app.mode == Mode::NewGame) {
        app.newGame->click(static_cast<int>(x), static_cast<int>(y));
        afterNewGameStep(app);
        return;
    }
    if (app.mode != Mode::Play) {
        if (app.mode != Mode::Welcome) {
            send(app, Command::Confirm);
        }
        return;
    }
    if (app.play->congressHallUp() || app.play->reportUp()) {
        send(app, Command::Confirm);
        return;
    }
    if (app.play->colonyScreen() || app.play->europeScreen()) {
        if (app.play->colonyScreen() && app.play->clickColonyScreen(static_cast<int>(x), static_cast<int>(y))) {
            return;
        }
        // The exit button in the lower right corner.
        if (x >= 300.0f && y >= 178.0f && !app.play->dialog()) {
            send(app, Command::Cancel);
        }
        if (app.play->europeScreen() && !app.play->dialog()) {
            static constexpr Command kButtons[] = {Command::Recruit, Command::Purchase, Command::Train};
            for (int i = 0; i < 3; ++i) {
                const auto& b = viceroy::kEuropeButtons[i];
                if (x >= static_cast<float>(b.x) && x < static_cast<float>(b.x + b.w) && y >= static_cast<float>(b.y) &&
                    y < static_cast<float>(b.y + b.h)) {
                    send(app, kButtons[i]);
                }
            }
        }
        return;
    }
    if (!app.debug.open() && app.play->clickMenu(static_cast<int>(x), static_cast<int>(y))) {
        if (app.play->loadRequested()) {
            showLoadMenu(app);
        }
        return;
    }
    if (!app.play->dialog() && !app.debug.open()) {
        if (const auto square = viceroy::minimapSquare(app.play->game(), app.play->view(), static_cast<int>(x),
                                                       static_cast<int>(y))) {
            app.play->clickMinimap(square->first, square->second);
            return;
        }
    }
    const auto& area = viceroy::kMapArea;
    if (x < static_cast<float>(area.x) || y < static_cast<float>(area.y) ||
        x >= static_cast<float>(area.x + area.w) || y >= static_cast<float>(area.y + area.h)) {
        if (app.play->game().activeUnit() < 0) {
            send(app, Command::Confirm);
        }
        return;
    }
    const auto& view = app.play->view();
    const int mx = view.originX + static_cast<int>(x - static_cast<float>(area.x)) / viceroy::kSquare;
    const int my = view.originY + static_cast<int>(y - static_cast<float>(area.y)) / viceroy::kSquare;
    app.play->clickSquare(mx, my);
}

void present(App& app) {
    SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
    SDL_RenderClear(app.renderer);
    if (app.mode == Mode::Welcome || !app.art) {
        drawWelcome(app);
        SDL_RenderPresent(app.renderer);
        return;
    }
    app.screen.resetColours();
    if (app.mode == Mode::Intro) {
        app.intro->draw(app.screen, *app.title);
    } else if (app.mode == Mode::Title) {
        app.mainMenu->draw(app.screen, *app.art, *app.title, app.intro.get());
    } else if (app.mode == Mode::NewGame) {
        app.newGame->draw(app.screen, *app.art, *app.newGameArt);
    } else if (app.mode == Mode::Load) {
        app.load->draw(app.screen, *app.art);
        if (!app.status.empty()) {
            app.screen.text(app.art->tiny, 34, 186, app.status, {0x95, viceroy::kNoColour, viceroy::kNoColour});
        }
    } else {
        app.play->draw(app.screen, *app.art);
        app.debug.draw(app.screen, *app.art);
    }
    drawPointer(app);
    // Indexed pixels to colours.
    void* pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(app.frame, nullptr, &pixels, &pitch)) {
        const auto& indexed = app.screen.pixels();
        for (int y = 0; y < kScreenH; ++y) {
            auto* row = static_cast<Uint8*>(pixels) + y * pitch;
            for (int x = 0; x < kScreenW; ++x) {
                const auto c = app.screen.colour(app.art->palette, indexed[static_cast<std::size_t>(y * kScreenW + x)]);
                row[x * 4 + 0] = c.r;
                row[x * 4 + 1] = c.g;
                row[x * 4 + 2] = c.b;
                row[x * 4 + 3] = 255;
            }
        }
        SDL_UnlockTexture(app.frame);
    }
    SDL_RenderTexture(app.renderer, app.frame, nullptr, nullptr);
    SDL_RenderPresent(app.renderer);
}

} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    auto* app = new App();
    *appstate = app;
    SDL_SetAppMetadata("Viceroy", std::string(viceroy::kVersion).c_str(), "io.github.niliodev.viceroy");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    if (!SDL_CreateWindowAndRenderer("Viceroy", kScreenW * 4, kScreenH * 4, SDL_WINDOW_RESIZABLE, &app->window,
                                     &app->renderer)) {
        SDL_Log("Could not open a window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderLogicalPresentation(app->renderer, kScreenW, kScreenH, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderVSync(app->renderer, 1);
    SDL_HideCursor();
    app->frame = SDL_CreateTexture(app->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, kScreenW,
                                   kScreenH);
    SDL_SetTextureScaleMode(app->frame, SDL_SCALEMODE_NEAREST);
    app->pickedEvent = SDL_RegisterEvents(1);
    app->library = libraryFolder();

    // A folder or zip on the command line is imported; otherwise use the
    // earlier import, or import a gamedata folder found nearby, or ask.
    if (argc > 1) {
        importAndOpen(*app, std::filesystem::path(argv[1]));
    } else if (viceroy::pack::hasBasePack(app->library)) {
        openLibrary(*app);
    } else if (const auto found = findGameToImport()) {
        importAndOpen(*app, *found);
    }
    SDL_Log("%s", app->mode == Mode::Welcome ? "Waiting for the game files." : "Game files ready.");
    app->lastTicks = SDL_GetTicks();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    auto* app = static_cast<App*>(appstate);
    switch (event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;
    case SDL_EVENT_GAMEPAD_ADDED:
        SDL_OpenGamepad(event->gdevice.which);
        break;
    case SDL_EVENT_DROP_FILE:
        if (event->drop.data != nullptr) {
            importAndOpen(*app, std::filesystem::path(event->drop.data));
        }
        break;
    case SDL_EVENT_KEY_DOWN:
        if (event->key.key == SDLK_F12 && app->mode == Mode::Play) {
            app->debug.toggle(*app->play);
            break;
        }
        if (app->mode == Mode::Intro) {
            showTitle(*app);
            break;
        }
        if (app->mode == Mode::Play && app->play->typing()) {
            if (event->key.key == SDLK_BACKSPACE) {
                app->play->eraseTyped();
                break;
            }
            if (event->key.key != SDLK_RETURN && event->key.key != SDLK_KP_ENTER && event->key.key != SDLK_ESCAPE) {
                break; // the letters come as text
            }
        }
        if (app->mode == Mode::NewGame && app->newGame->typing() && event->key.key == SDLK_BACKSPACE) {
            app->newGame->backspace();
            break;
        }
        if (app->mode == Mode::NewGame && app->newGame->typing() && event->key.key != SDLK_RETURN &&
            event->key.key != SDLK_KP_ENTER && event->key.key != SDLK_ESCAPE) {
            break; // the letters come as text
        }
        if (event->key.key == SDLK_ESCAPE) {
            if (app->mode == Mode::Play || app->mode == Mode::NewGame || (app->mode == Mode::Load && app->title)) {
                send(*app, Command::Cancel);
                break;
            }
            return SDL_APP_SUCCESS;
        }
        if (app->mode == Mode::Welcome && event->key.key == SDLK_Z) {
            chooseZip(*app);
            break;
        }
        if (app->mode == Mode::Play && !app->debug.open()) {
            // Alt and a title's gold letter open that menu; with a menu
            // open, a letter picks its entry.
            const SDL_Keycode k = event->key.key;
            const bool letter = k >= SDLK_A && k <= SDLK_Z;
            if (letter && (event->key.mod & SDL_KMOD_ALT) && app->art) {
                for (std::size_t i = 0; i < app->art->menus.size(); ++i) {
                    const auto& title = app->art->menus[i].title;
                    const auto mark = title.find('~');
                    if (mark != std::string::npos && mark + 1 < title.size() &&
                        std::toupper(static_cast<unsigned char>(title[mark + 1])) == static_cast<int>('A' + (k - SDLK_A))) {
                        app->play->openMenu(static_cast<int>(i), true);
                        break;
                    }
                }
                break;
            }
            if (letter && app->play->dialog() && app->play->dialog()->checkbox) {
                app->play->dialogLetter(static_cast<char>('A' + (k - SDLK_A)));
                break;
            }
            if (letter && app->play->menu()) {
                app->play->menuLetter(static_cast<char>('A' + (k - SDLK_A)));
                if (app->play->loadRequested()) {
                    showLoadMenu(*app);
                } else if (app->play->quitRequested()) {
                    return SDL_APP_SUCCESS;
                }
                break;
            }
        }
        if (auto command = keyCommand(event->key)) {
            send(*app, *command);
            if (app->mode == Mode::Play && app->play->quitRequested()) {
                return SDL_APP_SUCCESS;
            }
        }
        break;
    case SDL_EVENT_TEXT_INPUT:
        if (app->mode == Mode::NewGame && event->text.text != nullptr) {
            for (const char* c = event->text.text; *c != '\0'; ++c) {
                app->newGame->type(*c);
            }
        }
        if (app->mode == Mode::Play && app->play->typing() && event->text.text != nullptr) {
            for (const char* c = event->text.text; *c != '\0'; ++c) {
                app->play->type(*c);
            }
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        app->pointerShown = true;
        SDL_RenderCoordinatesFromWindow(app->renderer, event->motion.x, event->motion.y, &app->pointerX,
                                        &app->pointerY);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        app->pointerShown = true;
        SDL_RenderCoordinatesFromWindow(app->renderer, event->button.x, event->button.y, &app->pointerX,
                                        &app->pointerY);
        if (event->button.button == SDL_BUTTON_LEFT) {
            click(*app, event->button.x, event->button.y);
            if (app->mode == Mode::Play && app->play->quitRequested()) {
                return SDL_APP_SUCCESS;
            }
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
        app->pointerShown = false;
        if (app->mode == Mode::Intro) {
            showTitle(*app);
            break;
        }
        if (app->mode == Mode::Welcome) {
            if (event->gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
                chooseFolder(*app);
            } else if (event->gbutton.button == SDL_GAMEPAD_BUTTON_NORTH) {
                chooseZip(*app);
            }
            break;
        }
        if ((app->mode == Mode::Load || app->mode == Mode::Title || app->mode == Mode::NewGame) &&
            event->gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
            send(*app, Command::Confirm);
            break;
        }
        if ((app->mode == Mode::Load || app->mode == Mode::NewGame) && event->gbutton.button == SDL_GAMEPAD_BUTTON_EAST) {
            send(*app, Command::Cancel);
            break;
        }
        if (app->mode == Mode::Play && event->gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE) {
            send(*app, Command::Cancel);
            break;
        }
        const bool playing = app->mode == Mode::Play;
        // Back and Start together open the debug menu.
        if (playing && event->gbutton.button == SDL_GAMEPAD_BUTTON_START) {
            SDL_Gamepad* pad = SDL_GetGamepadFromID(event->gbutton.which);
            if (pad != nullptr && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK)) {
                app->play->closeMenu();
                app->debug.toggle(*app->play);
                break;
            }
        }
        if (playing && app->debug.open()) {
            if (event->gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
                send(*app, Command::Confirm);
            } else if (event->gbutton.button == SDL_GAMEPAD_BUTTON_EAST) {
                send(*app, Command::Cancel);
            } else if (event->gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                send(*app, Command::Up);
            } else if (event->gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                send(*app, Command::Down);
            }
            break;
        }
        const bool pointing =
            playing && (app->play->colonyScreen() || app->play->europeScreen() || app->play->dialog() ||
                        app->play->viewMode() || app->play->menu());
        if (auto command = buttonCommand(event->gbutton.button, playing && app->play->waitingForEndOfTurn(), pointing)) {
            send(*app, *command);
            if (app->mode == Mode::Play && app->play->quitRequested()) {
                return SDL_APP_SUCCESS;
            }
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        const float value = static_cast<float>(event->gaxis.value) / 32767.0f;
        if (event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
            app->stickX = value;
        } else if (event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
            app->stickY = value;
        } else if (event->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHTX || event->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHTY) {
            (event->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHTX ? app->lookX : app->lookY) = value;
            break;
        } else {
            break;
        }
        const auto direction = stickDirection(app->stickX, app->stickY);
        if (direction != app->stickCommand) {
            app->stickCommand = direction;
            if (direction) {
                send(*app, *direction);
                app->stickFired = SDL_GetTicks();
            }
        }
        break;
    }
    default:
        if (event->type == app->pickedEvent) {
            app->dialogOpen = false;
            if (auto* path = static_cast<char*>(event->user.data1)) {
                importAndOpen(*app, std::filesystem::path(path));
                SDL_free(path);
            }
        }
        break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* app = static_cast<App*>(appstate);
    const Uint64 now = SDL_GetTicks();
    const Uint64 elapsed = now - app->lastTicks;
    app->lastTicks = now;
    if (app->mode == Mode::Play) {
        app->play->tick(elapsed);
        app->audio.update(app->play->game().independenceDeclared());
    }
    if (app->mode == Mode::NewGame) {
        app->newGame->tick(elapsed);
        afterNewGameStep(*app);
    }
    if (app->mode == Mode::Intro) {
        app->intro->tick(elapsed);
        if (app->intro->finished()) {
            showTitle(*app);
        }
    }
    // The right stick scrolls the map on the main screen.
    if (app->mode == Mode::Play && std::hypot(app->lookX, app->lookY) >= kStickThreshold && !app->debug.open() &&
        !app->play->dialog() && !app->play->colonyScreen() && !app->play->europeScreen() &&
        now - app->lookFired >= kStickRepeatMs / 2) {
        app->lookFired = now;
        const auto axis = [](float v) { return v > kStickThreshold ? 1 : v < -kStickThreshold ? -1 : 0; };
        app->play->scrollView(axis(app->lookX), axis(app->lookY));
    }
    // Holding the stick keeps moving.
    if (app->stickCommand && now - app->stickFired >= kStickRepeatMs) {
        app->stickFired = now;
        send(*app, *app->stickCommand);
    }
    present(*app);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    auto* app = static_cast<App*>(appstate);
    if (app->frame != nullptr) {
        SDL_DestroyTexture(app->frame);
    }
    delete app;
}
