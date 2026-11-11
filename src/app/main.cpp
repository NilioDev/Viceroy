// SPDX-License-Identifier: GPL-3.0-or-later
//
// For now: a viewer that shows the map of the player's most recent
// savegame with the original terrain tiles, at the original 320×200
// resolution scaled to the window. The game itself is built up step by step.
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "viceroy/data/image.hpp"
#include "viceroy/data/reader.hpp"
#include "viceroy/data/savegame.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/version.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr int kScreenW = 320;
constexpr int kScreenH = 200;
constexpr int kTile = 16;

struct App {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    std::string status;
    std::optional<viceroy::SaveGame> save;
    std::vector<SDL_Texture*> terrainTiles;
    float viewX = 0; // top-left square shown
    float viewY = 0;
    float moveX = 0; // scroll direction from keys or gamepad
    float moveY = 0;
    Uint64 lastTicks = 0;
};

std::filesystem::path defaultGameFolder() {
    const char* base = SDL_GetBasePath();
    std::filesystem::path root = base ? std::filesystem::path(base) : std::filesystem::current_path();
    if (std::filesystem::exists(root / "gamedata")) {
        return root / "gamedata";
    }
    return std::filesystem::current_path() / "gamedata";
}

std::vector<std::uint8_t> readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

SDL_Texture* makeTexture(SDL_Renderer* renderer, const viceroy::IndexedImage& image, const viceroy::Palette& palette) {
    SDL_Surface* surface = SDL_CreateSurface(image.width, image.height, SDL_PIXELFORMAT_RGBA32);
    if (surface == nullptr) {
        return nullptr;
    }
    auto* pixels = static_cast<Uint8*>(surface->pixels);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const auto index = image.pixels[static_cast<std::size_t>(y * image.width + x)];
            Uint8* p = pixels + y * surface->pitch + x * 4;
            const auto& c = palette[index];
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = index == viceroy::kTransparent ? 0 : 255;
        }
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    if (texture != nullptr) {
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    }
    return texture;
}

std::optional<std::filesystem::path> newestSave(const std::filesystem::path& folder) {
    std::optional<std::filesystem::path> best;
    std::filesystem::file_time_type bestTime{};
    for (int i = 0; i < 10; ++i) {
        const auto path = viceroy::findFileNoCase(folder, "COLONY0" + std::to_string(i) + ".SAV");
        if (!path) {
            continue;
        }
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(*path, ec);
        if (!best || (!ec && time > bestTime)) {
            best = path;
            bestTime = time;
        }
    }
    return best;
}

void loadGame(App& app, const std::filesystem::path& folder) {
    if (!viceroy::isGameFolder(folder)) {
        app.status = "Put your game files in the gamedata folder.";
        return;
    }
    try {
        const auto palette = viceroy::readVgaPalette(readAll(*viceroy::findFileNoCase(folder, "VICEROY.PAL")));
        const auto terrainPath = viceroy::findFileNoCase(folder, "TERRAIN.SS");
        if (!terrainPath) {
            app.status = "TERRAIN.SS is missing.";
            return;
        }
        const auto sheet = viceroy::readSpriteSheet(readAll(*terrainPath));
        for (const auto& sprite : sheet.sprites) {
            app.terrainTiles.push_back(makeTexture(app.renderer, sprite.image, sheet.palette.value_or(palette)));
        }
        const auto savePath = newestSave(folder);
        if (!savePath) {
            app.status = "No savegame found to show.";
            return;
        }
        app.save = viceroy::readSaveGame(readAll(*savePath));
        app.status = savePath->filename().string() + "  " + std::to_string(app.save->year) +
                     (app.save->season == 0 ? " spring" : " autumn");
    } catch (const viceroy::FormatError& e) {
        app.status = std::string("Cannot read the game files: ") + e.what();
        app.save.reset();
    }
}

// Which of the base terrain tiles to draw for a square. Forested squares use
// the tile of their open land for now; forests, hills, mountains, rivers and
// coastlines are drawn on top in a later step.
std::size_t baseTile(std::uint8_t terrain) {
    switch (terrain) {
    case 24:
        return 9; // arctic
    case 25:
        return 10; // ocean
    case 26:
        return 11; // sea lane
    default:
        return terrain & 7u;
    }
}

void clampView(App& app) {
    if (!app.save) {
        return;
    }
    const float maxX = static_cast<float>(app.save->map.width - kScreenW / kTile);
    const float maxY = static_cast<float>(app.save->map.height - kScreenH / kTile + 1);
    app.viewX = std::clamp(app.viewX, 0.0f, std::max(0.0f, maxX));
    app.viewY = std::clamp(app.viewY, 0.0f, std::max(0.0f, maxY));
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
    loadGame(*app, argc > 1 ? std::filesystem::path(argv[1]) : defaultGameFolder());
    SDL_Log("%s", app->status.c_str());
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
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const float on = event->type == SDL_EVENT_KEY_DOWN ? 1.0f : 0.0f;
        switch (event->key.key) {
        case SDLK_ESCAPE:
            return SDL_APP_SUCCESS;
        case SDLK_LEFT:
        case SDLK_KP_4:
            app->moveX = -on;
            break;
        case SDLK_RIGHT:
        case SDLK_KP_6:
            app->moveX = on;
            break;
        case SDLK_UP:
        case SDLK_KP_8:
            app->moveY = -on;
            break;
        case SDLK_DOWN:
        case SDLK_KP_2:
            app->moveY = on;
            break;
        default:
            break;
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        const float on = event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? 1.0f : 0.0f;
        switch (event->gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            app->moveX = -on;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            app->moveX = on;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            app->moveY = -on;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            app->moveY = on;
            break;
        default:
            break;
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        const float value = static_cast<float>(event->gaxis.value) / 32767.0f;
        const float dead = std::abs(value) < 0.25f ? 0.0f : value;
        if (event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
            app->moveX = dead;
        } else if (event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
            app->moveY = dead;
        }
        break;
    }
    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* app = static_cast<App*>(appstate);
    const Uint64 now = SDL_GetTicks();
    const float seconds = static_cast<float>(now - app->lastTicks) / 1000.0f;
    app->lastTicks = now;
    constexpr float kSquaresPerSecond = 12.0f;
    app->viewX += app->moveX * kSquaresPerSecond * seconds;
    app->viewY += app->moveY * kSquaresPerSecond * seconds;
    clampView(*app);

    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
    SDL_RenderClear(app->renderer);
    if (app->save && !app->terrainTiles.empty()) {
        const auto& map = app->save->map;
        const int firstX = static_cast<int>(app->viewX);
        const int firstY = static_cast<int>(app->viewY);
        const float offX = (app->viewX - static_cast<float>(firstX)) * kTile;
        const float offY = (app->viewY - static_cast<float>(firstY)) * kTile;
        for (int y = firstY; y <= firstY + kScreenH / kTile + 1 && y < map.height; ++y) {
            for (int x = firstX; x <= firstX + kScreenW / kTile + 1 && x < map.width; ++x) {
                const auto tile = baseTile(map.at(x, y).terrain);
                if (tile >= app->terrainTiles.size() || app->terrainTiles[tile] == nullptr) {
                    continue;
                }
                const SDL_FRect dst{static_cast<float>((x - firstX) * kTile) - offX,
                                    static_cast<float>((y - firstY) * kTile) - offY, kTile, kTile};
                SDL_RenderTexture(app->renderer, app->terrainTiles[tile], nullptr, &dst);
            }
        }
    }
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 160);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    const SDL_FRect bar{0, kScreenH - 10, kScreenW, 10};
    SDL_RenderFillRect(app->renderer, &bar);
    SDL_SetRenderDrawColor(app->renderer, 236, 196, 92, 255);
    SDL_RenderDebugText(app->renderer, 2, kScreenH - 9, app->status.c_str());
    SDL_RenderPresent(app->renderer);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    auto* app = static_cast<App*>(appstate);
    for (auto* texture : app->terrainTiles) {
        SDL_DestroyTexture(texture);
    }
    delete app;
}
