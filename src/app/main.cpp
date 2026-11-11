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

#include "viceroy/data/reader.hpp"
#include "viceroy/data/savegame.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/pack/library.hpp"
#include "viceroy/render/game_view.hpp"
#include "viceroy/render/screen.hpp"
#include "viceroy/ui/play.hpp"
#include "viceroy/version.hpp"

#include <algorithm>
#include <cmath>
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

enum class Mode { Welcome, Load, Play };

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
    std::unique_ptr<viceroy::LoadMenu> load;
    std::unique_ptr<viceroy::ColonyArt> colonyArt;
    std::unique_ptr<viceroy::PlayScreen> play;
    viceroy::Screen screen;
    Uint64 lastTicks = 0;

    // Left stick: the direction held and when it last fired.
    std::optional<Command> stickCommand;
    Uint64 stickFired = 0;
    float stickX = 0;
    float stickY = 0;
};

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
    app.play.reset();
    app.load = std::make_unique<viceroy::LoadMenu>(
        viceroy::listSaves(viceroy::pack::savesFolder(app.library), *app.art));
    app.mode = Mode::Load;
}

// Loads the pictures and rules from the library, then shows the load menu.
void openLibrary(App& app) {
    try {
        app.art = std::make_unique<viceroy::Art>(viceroy::loadArt(viceroy::pack::baseFolder(app.library)));
        app.colonyArt =
            std::make_unique<viceroy::ColonyArt>(viceroy::loadColonyArt(viceroy::pack::baseFolder(app.library)));
        showLoadMenu(app);
        app.status.clear();
    } catch (const std::exception& e) {
        app.art.reset();
        app.status = std::string("Cannot read the imported game: ") + e.what() + " Drop it here again.";
        app.mode = Mode::Welcome;
    }
}

void startGame(App& app, const viceroy::SaveSlot& slot) {
    try {
        viceroy::Game game(viceroy::readSaveGame(readAll(slot.path)), app.art->rules);
        app.play = std::make_unique<viceroy::PlayScreen>(std::move(game), slot.path);
        app.play->setColonyNames(&app.art->colonyNames);
        app.play->setColonyArt(app.colonyArt.get());
        app.mode = Mode::Play;
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
    case SDLK_V:
        return Command::ViewMode;
    case SDLK_M:
        return Command::MoveMode;
    case SDLK_E:
        return ctrl ? std::optional<Command>(Command::EndTurn) : std::nullopt;
    case SDLK_TAB:
        return (key.mod & SDL_KMOD_SHIFT) ? Command::PreviousUnit : Command::NextUnit;
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
        return Command::Save;
    case SDL_GAMEPAD_BUTTON_BACK:
        return Command::ViewMode;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        return Command::Centre;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return Command::Fortify;
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
    case Mode::Load:
        if (command == Command::Cancel) {
            break;
        }
        if (auto slot = app.load->handle(command == Command::Skip ? Command::Confirm : command)) {
            startGame(app, *slot);
        }
        break;
    case Mode::Play:
        if (!app.play->handle(command)) {
            showLoadMenu(app);
        }
        break;
    }
}

// A click on the map: open a colony there, or move the active unit one
// step towards it. Anywhere on the colony screen closes it for now.
void click(App& app, float wx, float wy) {
    float x = 0;
    float y = 0;
    SDL_RenderCoordinatesFromWindow(app.renderer, wx, wy, &x, &y);
    if (app.mode != Mode::Play) {
        if (app.mode != Mode::Welcome) {
            send(app, Command::Confirm);
        }
        return;
    }
    if (app.play->colonyScreen()) {
        // The exit button in the lower right corner.
        if (x >= 300.0f && y >= 178.0f) {
            send(app, Command::Cancel);
        }
        return;
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
    if (app.mode == Mode::Load) {
        app.load->draw(app.screen, *app.art);
        if (!app.status.empty()) {
            app.screen.text(app.art->tiny, 34, 186, app.status, {0x95, viceroy::kNoColour, viceroy::kNoColour});
        }
    } else {
        app.play->draw(app.screen, *app.art);
    }
    // Indexed pixels to colours.
    void* pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(app.frame, nullptr, &pixels, &pitch)) {
        const auto& indexed = app.screen.pixels();
        for (int y = 0; y < kScreenH; ++y) {
            auto* row = static_cast<Uint8*>(pixels) + y * pitch;
            for (int x = 0; x < kScreenW; ++x) {
                const auto& c = app.art->palette[indexed[static_cast<std::size_t>(y * kScreenW + x)]];
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
        if (event->key.key == SDLK_ESCAPE) {
            if (app->mode == Mode::Play) {
                send(*app, Command::Cancel);
                break;
            }
            return SDL_APP_SUCCESS;
        }
        if (app->mode == Mode::Welcome && event->key.key == SDLK_Z) {
            chooseZip(*app);
            break;
        }
        if (auto command = keyCommand(event->key)) {
            send(*app, *command);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event->button.button == SDL_BUTTON_LEFT) {
            click(*app, event->button.x, event->button.y);
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
        if (app->mode == Mode::Welcome) {
            if (event->gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
                chooseFolder(*app);
            } else if (event->gbutton.button == SDL_GAMEPAD_BUTTON_NORTH) {
                chooseZip(*app);
            }
            break;
        }
        if (app->mode == Mode::Load && event->gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
            send(*app, Command::Confirm);
            break;
        }
        if (app->mode == Mode::Play && event->gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE) {
            send(*app, Command::Cancel);
            break;
        }
        const bool playing = app->mode == Mode::Play;
        const bool pointing = playing && (app->play->colonyScreen() || app->play->viewMode());
        if (auto command = buttonCommand(event->gbutton.button, playing && app->play->waitingForEndOfTurn(), pointing)) {
            send(*app, *command);
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        const float value = static_cast<float>(event->gaxis.value) / 32767.0f;
        if (event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
            app->stickX = value;
        } else if (event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
            app->stickY = value;
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
