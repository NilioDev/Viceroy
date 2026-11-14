// SPDX-License-Identifier: GPL-3.0-or-later
//
// The EDIT TRADE ROUTE screen, drawn as the original draws it: the route's
// name and type over a table of its stops, with the goods to unload and to
// load at each as icons. Measured on the original; see
// docs/rules/trade-routes.md.
#pragma once

#include "viceroy/render/game_view.hpp"
#include "viceroy/render/screen.hpp"

#include <string>

namespace viceroy {

class Game;

// Where things are on the screen.
namespace route_screen {
inline constexpr int kFirstRowY = 61; // the line over the first stop
inline constexpr int kRowHeight = 20;
inline constexpr int kRows = 4;
inline constexpr int kUnloadX = 115; // the lines between the columns
inline constexpr int kLoadX = 198;
inline constexpr int kIconGap = 2;
inline constexpr Rect kOk{280, 170, 30, 20};
} // namespace route_screen

// What a click on the screen points at.
struct RouteScreenHit {
    enum class Part { None, Destination, Unload, Load, Ok };
    Part part = Part::None;
    int row = -1;  // the stop
    int good = -1; // the place of a good's icon in its list, or -1
};
RouteScreenHit routeScreenHit(const Art& art, const Game& game, int route, int x, int y);

// The name a stop goes by on the screen: the colony's, or the home port's.
std::string routeStopName(const Art& art, const Game& game, int colony);

void drawTradeRouteScreen(Screen& screen, const Art& art, const Game& game, int route);

} // namespace viceroy
