// SPDX-License-Identifier: GPL-3.0-or-later
//
// Trade routes on the play screen: the TRADE menu (Edit, Create and Delete
// Trade Route), the EDIT TRADE ROUTE screen, and Begin Trade Route for a
// ship or wagon train. Seen in the original and read from it; see
// docs/rules/trade-routes.md.
#include "viceroy/render/trade_route_view.hpp"
#include "viceroy/ui/play.hpp"

#include <algorithm>

namespace viceroy {

namespace {

constexpr int kMore = -2;
constexpr int kDeleteStop = -3;
constexpr std::size_t kPageSize = 10;
constexpr int kAnyRoute = 0;
constexpr int kLandRoutes = 1;
constexpr int kSeaRoutes = 2;
constexpr int kLabelSea = 3;
constexpr int kLabelLand = 4;
constexpr int kLabelDeleteStop = 8;

bool routeOfKind(const Game::TradeRoute& r, int kind) { return kind == kAnyRoute || (kind == kSeaRoutes) == r.sea; }

} // namespace

void PlayScreen::openRouteList(std::string_view section, const std::string& question, int kind, int selected) {
    // Seen: "Select a trade route:" over the routes numbered from 1, the
    // number staying the route's own when some are left out.
    ask(section, -1);
    if (!dialog_) {
        return;
    }
    for (int r = 0; r < game_.tradeRouteCount(); ++r) {
        const auto route = game_.tradeRoute(r);
        if (!routeOfKind(route, kind)) {
            continue;
        }
        if (r == selected) {
            dialog_->selected = static_cast<int>(choices_.size());
        }
        addOption(*dialog_, std::to_string(r + 1) + ". " + route.name);
        choices_.push_back(r);
    }
    routeKind_ = kind;
    question_ = question;
}

void PlayScreen::editTradeRoute() {
    if (game_.tradeRouteCount() == 0) {
        tell({"TRADENONE", {}});
        return;
    }
    openRouteList("TRADESELECT", "ROUTEEDIT", kAnyRoute, 0);
}

void PlayScreen::deleteTradeRoute() {
    if (game_.tradeRouteCount() == 0) {
        tell({"TRADENONE", {}});
        return;
    }
    openRouteList("TRADEDELETE", "ROUTEDELETE", kAnyRoute, 0);
}

void PlayScreen::createTradeRoute() {
    if (game_.tradeRouteCount() >= Game::kMaxTradeRoutes) {
        GameNotice notice{"TRADEMANY", {}};
        notice.numbers = {Game::kMaxTradeRoutes};
        tell(notice);
        return;
    }
    routeDraft_ = Game::TradeRoute{};
    routeDraft_->stops.resize(1);
    routeRow_ = 0; // the first destination; 1 once the route is named
    openRouteColonies(0);
}

void PlayScreen::openRouteColonies(int page) {
    // Seen: "Select destination number 1 for route", the player's colonies
    // ten to a page; the second destination of a sea route only those by
    // the water.
    if (!routeDraft_ || art_ == nullptr) {
        return;
    }
    const bool second = routeRow_ >= 1;
    const int number = second ? 2 : 1;
    std::vector<int> entries;
    for (std::size_t c = 0; c < game_.state().colonies.size(); ++c) {
        const int colony = static_cast<int>(c);
        if (game_.state().colonies[c].nation != game_.human() ||
            (second && routeDraft_->sea && !game_.colonyOnCoast(colony))) {
            continue;
        }
        entries.push_back(colony);
    }
    const std::size_t pages = std::max<std::size_t>(1, (entries.size() + kPageSize - 1) / kPageSize);
    routePage_ = static_cast<int>(static_cast<std::size_t>(page) % pages);
    ask("TRADESTART", -1, {}, -1, {number});
    if (!dialog_) {
        return;
    }
    if (routePage_ > 0) {
        addOption(*dialog_, "(More)");
        choices_.push_back(kMore);
    }
    const std::size_t first = static_cast<std::size_t>(routePage_) * kPageSize;
    for (std::size_t i = first; i < std::min(entries.size(), first + kPageSize); ++i) {
        addOption(*dialog_, game_.state().colonies[static_cast<std::size_t>(entries[i])].name);
        choices_.push_back(entries[i]);
    }
    if (pages > 1 && routePage_ == 0) {
        addOption(*dialog_, "(More)");
        choices_.push_back(kMore);
    }
    dialog_->selected = 0;
    question_ = "ROUTESTART";
}

void PlayScreen::addDraftRoute() {
    if (!routeDraft_) {
        return;
    }
    const int route = game_.addTradeRoute(*routeDraft_);
    routeDraft_.reset();
    if (route >= 0) {
        routeScreen_ = route;
    }
}

void PlayScreen::openRoutePorts(int page) {
    // Seen: "Select a port to sail to:" with the home port first for a sea
    // route; for a stop already set the last page ends with "(Delete
    // Destination)".
    if (routeScreen_ < 0 || art_ == nullptr) {
        return;
    }
    const auto route = game_.tradeRoute(routeScreen_);
    std::vector<int> entries;
    if (route.sea) {
        entries.push_back(Game::kEuropeStop);
    }
    for (std::size_t c = 0; c < game_.state().colonies.size(); ++c) {
        const int colony = static_cast<int>(c);
        if (game_.state().colonies[c].nation == game_.human() && (!route.sea || game_.colonyOnCoast(colony))) {
            entries.push_back(colony);
        }
    }
    if (routeRow_ < static_cast<int>(route.stops.size())) {
        entries.push_back(kDeleteStop);
    }
    const std::size_t pages = std::max<std::size_t>(1, (entries.size() + kPageSize - 1) / kPageSize);
    routePage_ = static_cast<int>(static_cast<std::size_t>(page) % pages);
    const std::string section = route.sea ? "SAILPORT" : "TRAVELPLACE";
    Dialog d = makeDialog(*art_, section);
    choices_.clear();
    if (routePage_ > 0) {
        addOption(d, "(More)");
        choices_.push_back(kMore);
    }
    const std::size_t first = static_cast<std::size_t>(routePage_) * kPageSize;
    for (std::size_t i = first; i < std::min(entries.size(), first + kPageSize); ++i) {
        const int e = entries[i];
        if (e == Game::kEuropeStop) {
            const int nation = game_.human();
            addOption(d, textLine(art_->names, "HOMEPORT", nation, "") + " (" +
                             textLine(art_->names, "COUNTRY", nation, "") + ")");
        } else if (e == kDeleteStop) {
            addOption(d, textLine(art_->labelText, "ROUTE", kLabelDeleteStop, "(Delete Destination)"));
        } else {
            addOption(d, game_.state().colonies[static_cast<std::size_t>(e)].name);
        }
        choices_.push_back(e);
    }
    if (pages > 1 && routePage_ == 0) {
        addOption(d, "(More)");
        choices_.push_back(kMore);
    }
    d.optionLeft = 15;
    d.selected = route.sea && routePage_ == 0 && d.options.size() > 1 ? 1 : 0;
    placeCentredDialog(d);
    dialog_ = d;
    question_ = "ROUTEPORT";
    questionUnit_ = -1;
}

void PlayScreen::openRouteCargo(bool load) {
    // Seen: "Select a cargo to load at Jamestown." over the sixteen goods.
    if (routeScreen_ < 0 || art_ == nullptr) {
        return;
    }
    const auto route = game_.tradeRoute(routeScreen_);
    if (routeRow_ < 0 || routeRow_ >= static_cast<int>(route.stops.size())) {
        return;
    }
    const std::string where = routeStopName(*art_, game_, route.stops[static_cast<std::size_t>(routeRow_)].colony);
    Dialog d = makeDialog(*art_, load ? "CARGOLOAD" : "CARGOUNLOAD", {where});
    choices_.clear();
    for (int good = 0; good < kGoodsKinds; ++good) {
        addOption(d, goodName(good));
        choices_.push_back(good);
    }
    d.selected = 0;
    placeCentredDialog(d);
    dialog_ = d;
    question_ = load ? "ROUTELOAD" : "ROUTEUNLOAD";
    questionUnit_ = -1;
}

void PlayScreen::beginTradeRoute() {
    // Read from the original: a ship picks among the sea routes, a wagon
    // train among the land routes, then the stop to make for first.
    const int active = game_.activeUnit();
    if (active < 0 || art_ == nullptr) {
        return;
    }
    if (game_.tradeRouteCount() == 0) {
        tell({"TRADENONE", {}});
        return;
    }
    const int kind = game_.isShip(active) ? kSeaRoutes : kLandRoutes;
    bool any = false;
    for (int r = 0; r < game_.tradeRouteCount(); ++r) {
        any = any || routeOfKind(game_.tradeRoute(r), kind);
    }
    if (!any) {
        const std::string type = textLine(art_->labelText, "ROUTE", kind == kSeaRoutes ? kLabelSea : kLabelLand, "");
        tell({"TRADENONE2", {type}});
        return;
    }
    openRouteList("TRADESELECT", "ROUTEBEGIN", kind, game_.unitTradeRoute(active));
    questionUnit_ = active;
}

void PlayScreen::openRouteStops(int route) {
    // Seen: "Select a port to sail to:" over the route's stops.
    const int active = game_.activeUnit();
    if (active < 0 || art_ == nullptr) {
        return;
    }
    const auto r = game_.tradeRoute(route);
    Dialog d = makeDialog(*art_, "SAILPORT");
    choices_.clear();
    for (std::size_t s = 0; s < r.stops.size(); ++s) {
        addOption(d, routeStopName(*art_, game_, r.stops[s].colony));
        choices_.push_back(route * Game::kMaxRouteStops + static_cast<int>(s));
    }
    d.optionLeft = 15;
    d.selected = game_.unitTradeRoute(active) == route
                     ? std::min(game_.unitTradeStop(active), static_cast<int>(r.stops.size()) - 1)
                     : 0;
    d.selected = std::max(d.selected, 0);
    placeCentredDialog(d);
    dialog_ = d;
    question_ = "ROUTEFIRST";
    questionUnit_ = active;
}

bool PlayScreen::answerRoute(const std::string& question, int choice, int picked) {
    if (question.rfind("ROUTE", 0) != 0) {
        return false;
    }
    if (question == "ROUTEEDIT") {
        if (picked >= 0) {
            routeScreen_ = picked;
        }
    } else if (question == "ROUTEDELETE") {
        if (picked >= 0) {
            ask("SUREDELETE", -1, {game_.tradeRoute(picked).name});
            question_ = "ROUTESURE";
            routeRow_ = picked;
        }
    } else if (question == "ROUTESURE") {
        if (choice == 0) {
            game_.deleteTradeRoute(routeRow_);
        }
        routeRow_ = -1;
    } else if (question == "ROUTESTART") {
        if (!routeDraft_) {
            return true;
        }
        if (picked == kMore) {
            openRouteColonies(routePage_ + 1);
            return true;
        }
        if (picked < 0) {
            routeDraft_.reset();
            return true;
        }
        if (routeRow_ < 1) {
            // The first destination: a colony by the water asks which kind
            // of route; one inland makes a land route.
            routeDraft_->stops.front().colony = picked;
            if (game_.colonyOnCoast(picked)) {
                ask("TRADETYPE", -1);
                question_ = "ROUTETYPE";
            } else {
                routeDraft_->sea = false;
                answerRoute("ROUTETYPE", 1, -1);
            }
            return true;
        }
        // The second destination: the route is made and shown.
        Game::TradeStop stop;
        stop.colony = picked;
        routeDraft_->stops.push_back(stop);
        addDraftRoute();
    } else if (question == "ROUTETYPE") {
        if (!routeDraft_ || choice < 0) {
            routeDraft_.reset();
            return true;
        }
        routeDraft_->sea = choice == 0;
        // The name offered: the colony's and a word from TRADENAMES, with
        // " A", " B", ... added while another route has it (read from the
        // original).
        const auto* words = art_ != nullptr ? art_->gameText.find("TRADENAMES") : nullptr;
        std::string name = game_.state().colonies[static_cast<std::size_t>(routeDraft_->stops.front().colony)].name;
        if (words != nullptr && words->lines.size() > 1) {
            const int count = static_cast<int>(words->lines.size()) - 1;
            name += " " + words->lines[static_cast<std::size_t>(1 + game_.randomPick(count))];
        }
        bool suffixed = false;
        for (bool taken = true; taken;) {
            taken = false;
            for (int r = 0; r < game_.tradeRouteCount(); ++r) {
                taken = taken || game_.tradeRoute(r).name == name;
            }
            if (taken && !suffixed) {
                name += " A";
                suffixed = true;
            } else if (taken) {
                ++name.back();
            }
        }
        routeDraft_->name = name;
        ask("TRADENAME", -1);
        if (dialog_ && art_ != nullptr) {
            makeInput(*art_, *dialog_, name, static_cast<int>(Game::kRouteNameSize));
        }
        question_ = "ROUTENAME";
    } else if (question == "ROUTENAME") {
        if (!routeDraft_ || choice < 0) {
            routeDraft_.reset();
            return true;
        }
        if (!typed_.empty()) {
            routeDraft_->name = typed_.substr(0, Game::kRouteNameSize);
        }
        routeRow_ = 1;
        openRouteColonies(0);
    } else if (question == "ROUTEPORT") {
        if (picked == kMore) {
            openRoutePorts(routePage_ + 1);
            return true;
        }
        auto route = game_.tradeRoute(routeScreen_);
        if (picked == kDeleteStop) {
            if (routeRow_ >= 0 && routeRow_ < static_cast<int>(route.stops.size())) {
                route.stops.erase(route.stops.begin() + routeRow_);
            }
        } else if (picked >= 0 || picked == Game::kEuropeStop) {
            if (routeRow_ < static_cast<int>(route.stops.size())) {
                route.stops[static_cast<std::size_t>(routeRow_)].colony = picked;
            } else if (route.stops.size() < static_cast<std::size_t>(Game::kMaxRouteStops)) {
                Game::TradeStop stop;
                stop.colony = picked;
                route.stops.push_back(stop);
            }
        }
        game_.setTradeRoute(routeScreen_, route);
    } else if (question == "ROUTELOAD" || question == "ROUTEUNLOAD") {
        auto route = game_.tradeRoute(routeScreen_);
        if (picked >= 0 && routeRow_ >= 0 && routeRow_ < static_cast<int>(route.stops.size())) {
            auto& stop = route.stops[static_cast<std::size_t>(routeRow_)];
            auto& goods = question == "ROUTELOAD" ? stop.load : stop.unload;
            if (goods.size() < static_cast<std::size_t>(Game::kMaxStopGoods)) {
                goods.push_back(picked);
                game_.setTradeRoute(routeScreen_, route);
            }
        }
    } else if (question == "ROUTEBEGIN") {
        if (picked >= 0) {
            openRouteStops(picked);
        }
    } else if (question == "ROUTEFIRST") {
        const int active = game_.activeUnit();
        if (picked < 0 || active < 0) {
            return true;
        }
        game_.beginTradeRoute(active, picked / Game::kMaxRouteStops, picked % Game::kMaxRouteStops);
        // Seen: the carrier sets off at once.
        game_.moveTradeRouteUnits();
        for (auto& notice : game_.takeNotices()) {
            notices_.push_back(std::move(notice));
        }
        if (!game_.needsOrders(active)) {
            advance();
        }
    }
    return true;
}

bool PlayScreen::clickRouteScreen(int x, int y) {
    if (routeScreen_ < 0 || dialog_ || art_ == nullptr) {
        return false;
    }
    const auto hit = routeScreenHit(*art_, game_, routeScreen_, x, y);
    const auto route = game_.tradeRoute(routeScreen_);
    const int stops = static_cast<int>(route.stops.size());
    switch (hit.part) {
    case RouteScreenHit::Part::Ok:
        routeScreen_ = -1;
        return true;
    case RouteScreenHit::Part::Destination:
        // A stop's name changes the stop; an empty row adds one.
        routeRow_ = std::min(hit.row, stops);
        if (routeRow_ < Game::kMaxRouteStops) {
            openRoutePorts(0);
        }
        return true;
    case RouteScreenHit::Part::Unload:
    case RouteScreenHit::Part::Load: {
        if (hit.row >= stops) {
            return true;
        }
        const bool load = hit.part == RouteScreenHit::Part::Load;
        routeRow_ = hit.row;
        if (hit.good >= 0) {
            // A good's icon takes it off the list.
            auto changed = route;
            auto& goods = load ? changed.stops[static_cast<std::size_t>(hit.row)].load
                               : changed.stops[static_cast<std::size_t>(hit.row)].unload;
            goods.erase(goods.begin() + hit.good);
            game_.setTradeRoute(routeScreen_, changed);
            return true;
        }
        openRouteCargo(load);
        return true;
    }
    case RouteScreenHit::Part::None:
        break;
    }
    return false;
}

bool PlayScreen::handleRouteScreen(Command command) {
    // Return or Escape leaves the screen as OK does.
    if (command == Command::Confirm || command == Command::Cancel) {
        routeScreen_ = -1;
    }
    return true;
}

} // namespace viceroy
