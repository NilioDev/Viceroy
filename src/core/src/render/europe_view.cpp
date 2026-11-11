// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/render/europe_view.hpp"

#include "viceroy/game/europe.hpp"

#include <algorithm>
#include <string>

namespace viceroy {

namespace {

constexpr std::uint8_t kBlack = 0x00;
constexpr std::uint8_t kYellow = 0x0E;
constexpr std::uint8_t kWhite = 0x0F;
constexpr std::uint8_t kPriceText = 0x2F;
constexpr std::uint8_t kButtonLight = 0x39;
constexpr std::uint8_t kButtonDark = 0x30;
constexpr std::uint8_t kGreen = 0x44;
constexpr std::uint8_t kBoxTitle = 0x45;
constexpr std::uint8_t kSelected = 0x0A; // the frame round the chosen ship

constexpr int kGoodsIcon = 22;
constexpr int kEmptyHold = 122;

// The three boxes over the water: ships coming, ships going, the port.
constexpr int kBoxLeft[3]{1, 72, 143};
constexpr int kBoxWidth = 70;
constexpr int kPortWidth = 80;
constexpr int kUnitTop = 146;
constexpr int kUnitStep = 17;
constexpr int kHoldsLeft = 147;
constexpr int kHoldsTop = 165;
constexpr int kDocksCentre = 239;
constexpr int kDocksTop = 138;

// LABELS.TXT @MISC rows.
constexpr int kExpectedSoon = 9;
constexpr int kBoundFor = 10;
constexpr int kNoShipsInPort = 11;
constexpr int kLoading = 22;

int spriteWidth(const SpriteSheet& sheet, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= sheet.sprites.size()) {
        return 0;
    }
    return sheet.sprites[static_cast<std::size_t>(index)].image.width;
}

// Box titles are centred as the original does it, which leans a pixel
// to the right.
void boxTitle(Screen& screen, const Font& font, int left, int width, int y, const std::string& s) {
    screen.text(font, left + (width + 2 - Screen::textWidth(font, s)) / 2, y, s, {kBoxTitle, kNoColour, kNoColour});
}

void centred(Screen& screen, const Font& font, int cx, int y, const std::string& s, std::uint8_t colour) {
    screen.text(font, cx - Screen::textWidth(font, s) / 2, y, s, {colour, kNoColour, kNoColour});
}

void drawTopLine(Screen& screen, const Art& art, const Game& game) {
    screen.tile(art.wood, {0, 0, Screen::kWidth, 7});
    screen.fill({0, 7, Screen::kWidth, 1}, kBlack);
    const auto& save = game.state();
    const int human = game.human();
    const auto& nation = save.nations[static_cast<std::size_t>(human)];
    const std::string port = textLine(art.names, "HOMEPORT", human, "");
    const std::string country = textLine(art.names, "COUNTRY", human, "");
    const std::string text = port + ", " + country + ".  " + seasonName(art, save.season) + ", " +
                             std::to_string(save.year) + ".  Tax: " + std::to_string(nation.taxRate) +
                             "% Gold: " + std::to_string(nation.gold) + "$";
    centred(screen, art.tiny, 160, 1, text, kGreen);
}

void drawButtons(Screen& screen, const Art& art) {
    for (int i = 0; i < 3; ++i) {
        const Rect& r = kEuropeButtons[i];
        screen.fill({r.x, r.y, r.w, 1}, kButtonLight);
        screen.fill({r.x, r.y, 1, r.h}, kButtonLight);
        screen.fill({r.x + 1, r.y + r.h - 1, r.w - 1, 1}, kButtonDark);
        screen.fill({r.x + r.w - 1, r.y + 1, 1, r.h - 1}, kButtonDark);
        const std::string label = textLine(art.labelText, "EUROLABEL", i, "");
        if (label.empty()) {
            continue;
        }
        const int width = Screen::textWidth(art.tiny, label);
        const int x = r.x + (r.w - width) / 2 + 1;
        const std::string first = label.substr(0, 1);
        const int after = screen.text(art.tiny, x, r.y + 2, first, {kYellow, kNoColour, kNoColour});
        screen.text(art.tiny, after, r.y + 2, label.substr(1), {kWhite, kNoColour, kNoColour});
    }
}

// A unit as the port shows it: a black shadow two pixels to the left,
// then the orders box (beside a ship's mast, at a person's feet), then the
// unit itself.
void drawUnit(Screen& screen, const Art& art, const Game& game, int index, int x, int y) {
    const int icon = unitIcon(game, index);
    if (icon < 0 || static_cast<std::size_t>(icon) >= art.icons.sprites.size()) {
        return;
    }
    const auto& image = art.icons.sprites[static_cast<std::size_t>(icon)].image;
    for (int j = 0; j < image.height; ++j) {
        for (int i = 0; i < image.width; ++i) {
            if (image.pixels[static_cast<std::size_t>(j * image.width + i)] != kTransparent) {
                screen.pixel(x - 2 + i, y + j, kBlack);
            }
        }
    }
    const auto& u = game.unit(index);
    const bool ship = isShipType(game.rules(), u.type);
    const Rect box = ship ? Rect{x + image.width - 7, y, 7, 9} : Rect{x + image.width - 2, y + 7, 7, 9};
    const std::uint8_t colour = nationColour(art, u.nation);
    screen.fill(box, colour);
    screen.frame(box, kBlack);
    static constexpr const char* kLetters = "-STGLFFBPR-";
    const char text[2] = {(u.orders >= 0 && u.orders < 11) ? kLetters[u.orders] : '-', 0};
    // Units with orders show the letter in the darker half of the nation's
    // colour; the dash of a unit without orders is black.
    const std::uint8_t letter = u.orders != SaveUnit::kNoOrders && colour >= 8 && colour < 16
                                    ? static_cast<std::uint8_t>(colour - 8)
                                    : kBlack;
    screen.text(art.tiny, box.x + (box.w - Screen::textWidth(art.tiny, text)) / 2 + 1, box.y + 2, text,
                {letter, kNoColour, kNoColour});
    screen.blit(image, x, y);
}

// Units stand on slots 17 pixels apart; each is centred on its slot.
void drawCentredUnit(Screen& screen, const Art& art, const Game& game, int index, int centre, int y) {
    drawUnit(screen, art, game, index, centre - spriteWidth(art.icons, unitIcon(game, index)) / 2, y);
}

// A ship and the land units it carries, from left to right; returns the
// centre of the next slot.
int drawShipAndCargo(Screen& screen, const Art& art, const Game& game, int ship, int centre) {
    const auto& save = game.state();
    drawCentredUnit(screen, art, game, ship, centre, kUnitTop);
    centre += 14;
    // Its passengers, walking back along the chain from the ship.
    const int count = game.unitCount();
    int at = save.units[static_cast<std::size_t>(ship)].prevInTransport;
    for (int steps = 0; steps < count && at >= 0 && at < count; ++steps) {
        const auto& u = save.units[static_cast<std::size_t>(at)];
        if (u.x != save.units[static_cast<std::size_t>(ship)].x || isShipType(game.rules(), u.type)) {
            break;
        }
        drawCentredUnit(screen, art, game, at, centre, kUnitTop);
        centre += kUnitStep;
        at = u.prevInTransport;
    }
    return centre + 3;
}

void drawSailing(Screen& screen, const Art& art, const Game& game, int box, EuropePlace place) {
    const int left = kBoxLeft[box];
    screen.setClip({left, 118, kBoxWidth, 50});
    int centre = left + 10;
    for (int ship : unitsInEurope(game.state(), game.rules(), game.human(), place, true)) {
        centre = drawShipAndCargo(screen, art, game, ship, centre);
    }
    screen.resetClip();
}

void drawBoxTitles(Screen& screen, const Art& art, const Game& game, const EuropeViewState& view) {
    const int human = game.human();
    boxTitle(screen, art.tiny, kBoxLeft[0], kBoxWidth, 120, textLine(art.labelText, "MISC", kExpectedSoon, "Expected Soon"));
    boxTitle(screen, art.tiny, kBoxLeft[1], kBoxWidth, 120, textLine(art.labelText, "MISC", kBoundFor, "Bound For"));
    boxTitle(screen, art.tiny, kBoxLeft[1], kBoxWidth, 127, textLine(art.names, "COLONYNAME", human, ""));
    std::string port = textLine(art.labelText, "MISC", kNoShipsInPort, "No Ships In Port");
    if (view.ship >= 0) {
        const auto& types = game.rules().units;
        const int type = game.unit(view.ship).type;
        const std::string name =
            type >= 0 && static_cast<std::size_t>(type) < types.size() ? types[static_cast<std::size_t>(type)].name : "";
        port = textLine(art.labelText, "MISC", kLoading, "Loading") + ":  " + name;
    }
    boxTitle(screen, art.tiny, kBoxLeft[2], kPortWidth, 120, port);
}

void drawPort(Screen& screen, const Art& art, const Game& game, const EuropeViewState& view) {
    const auto& save = game.state();
    const auto ships = unitsInEurope(save, game.rules(), game.human(), EuropePlace::InPort, true);
    int centre = kBoxLeft[2] + 12;
    for (int ship : ships) {
        const int x = centre - spriteWidth(art.icons, unitIcon(game, ship)) / 2;
        drawUnit(screen, art, game, ship, x, kUnitTop);
        if (ship == view.ship) {
            screen.frame({x - 3, kUnitTop - 1, 18, 18}, kSelected);
        }
        centre += kUnitStep;
    }
    // The holds of the chosen ship; the rest stay shut.
    int holds = 0;
    if (view.ship >= 0) {
        const int type = game.unit(view.ship).type;
        if (type >= 0 && static_cast<std::size_t>(type) < game.rules().units.size()) {
            holds = game.rules().units[static_cast<std::size_t>(type)].cargoSlots;
        }
    }
    // The picture shows open holds; the ones the ship lacks are crates.
    for (int k = holds; k < 6; ++k) {
        blitSprite(screen, art.icons, kEmptyHold, kHoldsLeft + 12 * k, kHoldsTop);
    }

    // The docks: land units in port.
    int dockCentre = kDocksCentre;
    const auto docks = unitsInEurope(save, game.rules(), game.human(), EuropePlace::InPort, false);
    for (auto it = docks.rbegin(); it != docks.rend(); ++it) {
        drawCentredUnit(screen, art, game, *it, dockCentre, kDocksTop);
        dockCentre += kUnitStep;
    }
}

void drawMarket(Screen& screen, const Art& art, const Game& game, const EuropeViewState& view) {
    const auto& nation = game.state().nations[static_cast<std::size_t>(game.human())];
    for (int good = 0; good < kGoodsKinds; ++good) {
        const int centre = 10 + 19 * good;
        const int icon = kGoodsIcon + good;
        blitSprite(screen, art.icons, icon, centre - spriteWidth(art.icons, icon) / 2, 181);
        const std::string price =
            std::to_string(bidPrice(nation, good)) + "/" + std::to_string(askPrice(game.rules(), nation, good));
        centred(screen, art.tiny, centre, 194, price, kPriceText);
    }
    if (view.good >= 0 && view.good < kGoodsKinds) {
        screen.frame({19 * view.good, 179, 20, 21}, kYellow);
    }
    screen.text(art.tiny, 306, 179, "Exit", {kWhite, kNoColour, kNoColour});
}

} // namespace

void drawEuropeScreen(Screen& screen, const Art& art, const ColonyArt& colonyArt, const Game& game,
                      const EuropeViewState& view) {
    screen.resetClip();
    screen.clear(kBlack);
    screen.blit(colonyArt.europe, 0, 0);
    drawTopLine(screen, art, game);
    drawButtons(screen, art);
    drawBoxTitles(screen, art, game, view);
    drawSailing(screen, art, game, 0, EuropePlace::ToEurope);
    drawSailing(screen, art, game, 1, EuropePlace::ToNewWorld);
    drawPort(screen, art, game, view);
    drawMarket(screen, art, game, view);
}

} // namespace viceroy
