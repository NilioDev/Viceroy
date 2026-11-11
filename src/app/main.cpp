// SPDX-License-Identifier: GPL-3.0-or-later
//
// For now only opens a window and shows whether the game files were found.
// The game itself is built up step by step in later changes.
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "viceroy/game_files.hpp"
#include "viceroy/version.hpp"

#include <filesystem>
#include <string>

namespace {

struct App {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    bool haveGame = false;
};

std::filesystem::path defaultGameFolder() {
    const char* base = SDL_GetBasePath();
    std::filesystem::path root = base ? std::filesystem::path(base) : std::filesystem::current_path();
    return root / "gamedata";
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
    if (!SDL_CreateWindowAndRenderer("Viceroy", 1280, 800, SDL_WINDOW_RESIZABLE, &app->window, &app->renderer)) {
        SDL_Log("Could not open a window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    const std::filesystem::path folder = argc > 1 ? std::filesystem::path(argv[1]) : defaultGameFolder();
    app->haveGame = viceroy::isGameFolder(folder);
    SDL_Log("Game files %s in %s", app->haveGame ? "found" : "not found", folder.string().c_str());
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void*, SDL_Event* event) {
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* app = static_cast<App*>(appstate);
    SDL_SetRenderDrawColor(app->renderer, 18, 20, 26, 255);
    SDL_RenderClear(app->renderer);
    SDL_SetRenderDrawColor(app->renderer, 236, 196, 92, 255);
    SDL_SetRenderScale(app->renderer, 2.0f, 2.0f);
    SDL_RenderDebugText(app->renderer, 16, 16, "Viceroy");
    SDL_RenderDebugText(app->renderer, 16, 32,
                        app->haveGame ? "Game files found." : "Put your game files in the gamedata folder.");
    SDL_SetRenderScale(app->renderer, 1.0f, 1.0f);
    SDL_RenderPresent(app->renderer);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    auto* app = static_cast<App*>(appstate);
    delete app;
}
