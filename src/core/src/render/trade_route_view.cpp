// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/render/trade_route_view.hpp"

#include "viceroy/game/game.hpp"

#include <string>
#include <vector>

namespace viceroy {

namespace {

constexpr std::uint8_t kBlack = 0x00;
constexpr std::uint8_t kWhite = 0x0F;
constexpr std::uint8_t kGrey = 0x22; // the screen's ground (measured)
constexpr int kGoodsIcon = 22;       // ICONS.SS: the goods, 9 × 12
constexpr TextColours kInk{kWhite, kNoColour, kNoColour};

// Measured on the original.
constexpr int kTitleY = 5;
constexpr int kLabelX = 10;
constexpr int kValueX = 55;
constexpr int kNameY = 25;
constexpr int kTypeY = 33;
constexpr int kHeadY = 49;
constexpr int kDestinationHeadX = 21;
constexpr int kNumberX = 10;
constexpr int kStopNameX = 22;
constexpr int kRowTextDy = 8;
constexpr int kIconDy = 5;
constexpr int kIconDx = 10;
constexpr int kOkTextX = 292;
constexpr int kOkTextY = 177;

std::string label(const Art& art, int row, const char* fallback) {
    return textLine(art.labelText, "ROUTE", row, fallback);
}

int rowTop(int row) { return route_screen::kFirstRowY + route_screen::kRowHeight * row; }

int iconWidth(const Art& art, int good) {
    const int index = kGoodsIcon + good;
    if (index < 0 || static_cast<std::size_t>(index) >= art.icons.sprites.size()) {
        return 0;
    }
    return art.icons.sprites[static_cast<std::size_t>(index)].image.width;
}

// Measured: the icons stand two pixels apart, each as wide as it is.
int iconX(const Art& art, const std::vector<int>& goods, int first, std::size_t k) {
    int x = first;
    for (std::size_t i = 0; i < k; ++i) {
        x += iconWidth(art, goods[i]) + route_screen::kIconGap;
    }
    return x;
}

} // namespace

std::string routeStopName(const Art& art, const Game& game, int colony) {
    if (colony == Game::kEuropeStop) {
        return textLine(art.names, "HOMEPORT", game.human(), "Europe");
    }
    if (colony < 0 || static_cast<std::size_t>(colony) >= game.state().colonies.size()) {
        return "";
    }
    return game.state().colonies[static_cast<std::size_t>(colony)].name;
}

RouteScreenHit routeScreenHit(const Art& art, const Game& game, int route, int x, int y) {
    using namespace route_screen;
    RouteScreenHit hit;
    if (x >= kOk.x && x < kOk.x + kOk.w && y >= kOk.y && y < kOk.y + kOk.h) {
        hit.part = RouteScreenHit::Part::Ok;
        return hit;
    }
    if (y <= kFirstRowY || y >= rowTop(kRows)) {
        return hit;
    }
    hit.row = (y - kFirstRowY - 1) / kRowHeight;
    if (x < kUnloadX) {
        hit.part = RouteScreenHit::Part::Destination;
        return hit;
    }
    const bool load = x > kLoadX;
    hit.part = load ? RouteScreenHit::Part::Load : RouteScreenHit::Part::Unload;
    const auto r = game.tradeRoute(route);
    if (hit.row < static_cast<int>(r.stops.size())) {
        const auto& stop = r.stops[static_cast<std::size_t>(hit.row)];
        const auto& goods = load ? stop.load : stop.unload;
        const int first = (load ? kLoadX : kUnloadX) + kIconDx;
        for (std::size_t k = 0; k < goods.size(); ++k) {
            const int left = iconX(art, goods, first, k);
            if (x >= left && x < left + iconWidth(art, goods[k])) {
                hit.good = static_cast<int>(k);
            }
        }
    }
    return hit;
}

void drawTradeRouteScreen(Screen& screen, const Art& art, const Game& game, int route) {
    using namespace route_screen;
    screen.clear(kGrey);
    const auto r = game.tradeRoute(route);
    const std::string title = label(art, 0, "EDIT TRADE ROUTE") + " " + std::to_string(route + 1);
    screen.text(art.tiny, 160 - (Screen::textWidth(art.tiny, title) - 1) / 2, kTitleY, title, kInk);
    screen.text(art.tiny, kLabelX, kNameY, label(art, 1, "Route Name:"), kInk);
    screen.text(art.tiny, kValueX, kNameY, r.name, kInk);
    screen.text(art.tiny, kLabelX, kTypeY, label(art, 2, "Route Type:"), kInk);
    screen.text(art.tiny, kValueX, kTypeY, r.sea ? label(art, 3, "Sea") : label(art, 4, "Land"), kInk);
    screen.text(art.tiny, kDestinationHeadX, kHeadY, label(art, 5, "Destination"), kInk);
    screen.text(art.tiny, kUnloadX + kIconDx, kHeadY, label(art, 6, "Unload Cargo"), kInk);
    screen.text(art.tiny, kLoadX + kIconDx, kHeadY, label(art, 7, "Load Cargo"), kInk);
    for (int k = 0; k <= kRows; ++k) {
        screen.fill({0, rowTop(k), Screen::kWidth, 1}, kBlack);
    }
    screen.fill({kUnloadX, kFirstRowY, 1, kRows * kRowHeight + 1}, kBlack);
    screen.fill({kLoadX, kFirstRowY, 1, kRows * kRowHeight + 1}, kBlack);
    const auto icon = [&](int good, int x, int y) {
        const int index = kGoodsIcon + good;
        if (index >= 0 && static_cast<std::size_t>(index) < art.icons.sprites.size()) {
            screen.blit(art.icons.sprites[static_cast<std::size_t>(index)].image, x, y);
        }
    };
    for (std::size_t s = 0; s < r.stops.size() && s < static_cast<std::size_t>(kRows); ++s) {
        const auto& stop = r.stops[s];
        const int top = rowTop(static_cast<int>(s));
        screen.text(art.tiny, kNumberX, top + kRowTextDy, std::to_string(s + 1) + ".", kInk);
        screen.text(art.tiny, kStopNameX, top + kRowTextDy, routeStopName(art, game, stop.colony), kInk);
        for (std::size_t k = 0; k < stop.unload.size(); ++k) {
            icon(stop.unload[k], iconX(art, stop.unload, kUnloadX + kIconDx, k), top + kIconDy);
        }
        for (std::size_t k = 0; k < stop.load.size(); ++k) {
            icon(stop.load[k], iconX(art, stop.load, kLoadX + kIconDx, k), top + kIconDy);
        }
    }
    screen.frame(kOk, kBlack);
    screen.text(art.tiny, kOkTextX, kOkTextY, "OK", kInk);
}

} // namespace viceroy
