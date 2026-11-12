// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/ui/title.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/game_files.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace viceroy {

namespace {

Bytes readFile(const std::filesystem::path& folder, std::string_view name) {
    const auto path = findFileNoCase(folder, name);
    if (!path) {
        throw FormatError(std::string(name) + " is missing");
    }
    std::ifstream in(*path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), {});
}

// The heading names the program: ours, not the original's.
void nameProgram(Dialog& d) {
    if (d.lines.empty()) {
        return;
    }
    auto& line = d.lines.front();
    const auto open = line.find('{');
    const auto close = line.find('}', open);
    if (open != std::string::npos && close != std::string::npos) {
        line.replace(open + 1, close - open - 1, "Viceroy");
    }
}

constexpr TitleChoice kChoices[] = {TitleChoice::NewWorld, TitleChoice::America, TitleChoice::Customize,
                                    TitleChoice::Load, TitleChoice::HallOfFame};

std::optional<TitleChoice> choiceAt(int index) {
    if (index < 0 || index >= static_cast<int>(std::size(kChoices))) {
        return std::nullopt;
    }
    return kChoices[index];
}

// A whole picture: every pixel, the transparent index too.
void copyPicture(Screen& screen, const IndexedImage& image, int x, int y) {
    for (int yy = 0; yy < image.height; ++yy) {
        for (int xx = 0; xx < image.width; ++xx) {
            screen.pixel(x + xx, y + yy, image.pixels[static_cast<std::size_t>(yy * image.width + xx)]);
        }
    }
}

// The opening has its own palette. The message box is drawn with the
// game's colours, then each is swapped for the opening's colour the
// original uses there (measured on its main menu).
std::uint8_t openingColour(std::uint8_t c) {
    switch (c) {
    case 0x44: return 0xFE;
    case 0x80: return 0xFD;
    case 0x81: return 0xF5;
    case 0x82: return 0xF8;
    case 0x83: return 0xF6;
    case 0x84: return 0x2C;
    case 0x85: return 0x3C;
    case 0x86: return 0x2D;
    case 0x87: return 0x30;
    case 0x88: return 0x2E;
    case 0x89: return 0x35;
    case 0x8A: return 0x37;
    case 0x95: return 0xFC;
    default: return c;
    }
}

// The series of @OPENING, in OPENING.TXT's numbering.
constexpr int kSun = 1;
constexpr int kGuy = 7;

// The ship the view follows, measured frame by frame on the original:
// frame, left, top, sprite of OPENSHIP.SS. Its course wanders, so it is
// replayed as it was seen. After the last entry it has run aground.
struct ShipAt {
    int frame;
    int x;
    int y;
    int sprite;
};
constexpr ShipAt kShipCourse[] = {
    {4, 218, 77, 4}, {8, 218, 77, 0}, {12, 219, 73, 4}, {16, 222, 70, 7}, {19, 223, 67, 3}, {23, 223, 65, 7},
    {27, 223, 65, 3}, {31, 223, 63, 7}, {35, 223, 61, 2}, {38, 223, 59, 6}, {42, 223, 57, 2}, {46, 223, 56, 6},
    {50, 223, 56, 2}, {54, 223, 55, 5}, {57, 223, 53, 1}, {61, 223, 52, 5}, {65, 223, 51, 1}, {69, 223, 50, 5},
    {73, 223, 50, 0}, {76, 223, 52, 4}, {80, 221, 56, 0}, {84, 217, 64, 4}, {88, 217, 67, 0}, {92, 217, 67, 3},
    {95, 217, 69, 7}, {99, 215, 75, 3}, {103, 212, 81, 7}, {107, 212, 82, 2}, {111, 212, 82, 6}, {114, 212, 83, 2},
    {118, 212, 84, 6}, {122, 212, 84, 2}, {126, 212, 84, 5}, {130, 212, 84, 1}, {133, 212, 83, 5},
    {137, 212, 80, 1}, {141, 212, 78, 5}, {145, 212, 76, 0}, {149, 212, 75, 4}, {152, 212, 75, 0},
    {156, 212, 74, 4}, {160, 212, 72, 7}, {164, 212, 70, 3}, {168, 212, 66, 7}, {171, 212, 66, 3},
    {175, 212, 65, 7}, {179, 212, 64, 3}, {183, 212, 63, 6}, {187, 212, 62, 2}, {190, 212, 61, 6},
    {194, 212, 60, 2}, {198, 212, 60, 6}, {202, 212, 59, 1}, {206, 212, 59, 5}, {209, 212, 61, 1},
    {213, 212, 63, 5}, {217, 212, 66, 1}, {221, 212, 68, 4}, {225, 211, 71, 0}, {228, 211, 79, 4},
    {232, 211, 82, 7}, {236, 211, 86, 3}, {240, 211, 89, 7}, {244, 211, 90, 3}, {247, 211, 89, 7},
    {282, 211, 82, 1}, {285, 211, 84, 5}, {289, 211, 85, 1}, {293, 211, 86, 4}, {297, 211, 86, 0},
    {301, 211, 87, 4}, {304, 211, 87, 0}, {308, 211, 88, 4}, {312, 211, 88, 7}, {316, 211, 88, 3},
    {320, 211, 88, 7}, {323, 211, 86, 3}, {327, 211, 83, 6}, {331, 211, 81, 2}, {335, 211, 78, 6},
    {339, 211, 75, 2}, {342, 211, 72, 6}, {346, 211, 70, 1}, {350, 211, 70, 5}, {354, 211, 70, 1},
    {358, 211, 69, 5}, {361, 211, 69, 1}, {365, 211, 69, 4}, {369, 211, 68, 0}, {373, 211, 68, 4},
    {377, 211, 67, 0}, {380, 211, 67, 4}, {384, 211, 67, 0}, {388, 211, 67, 3}, {392, 211, 66, 7},
    {396, 211, 61, 3}, {399, 211, 58, 7}, {403, 211, 57, 3}, {407, 211, 58, 6}, {411, 211, 62, 2},
    {415, 211, 65, 6}, {418, 211, 69, 2}, {422, 211, 72, 6}, {426, 211, 73, 1}, {430, 211, 74, 5},
    {434, 211, 74, 1}, {437, 211, 75, 5}, {441, 211, 76, 1}, {445, 211, 77, 4}, {449, 211, 75, 0},
    {453, 211, 70, 4}, {456, 211, 67, 0}, {460, 211, 67, 4}, {464, 211, 65, 7}, {468, 211, 62, 3},
    {472, 211, 62, 7}, {475, 211, 61, 3}, {479, 211, 60, 7}, {483, 211, 59, 2}, {487, 211, 60, 6},
    {491, 211, 63, 2}, {494, 211, 67, 6}, {498, 211, 68, 1}, {502, 211, 71, 5}, {506, 211, 72, 1},
    {509, 211, 74, 5}, {513, 211, 76, 1}, {517, 211, 77, 4}, {521, 211, 79, 0}, {525, 211, 80, 4},
    {528, 211, 82, 0}, {532, 211, 84, 4}, {536, 211, 84, 7}, {540, 211, 84, 3}, {544, 211, 83, 7},
    {547, 211, 84, 3}, {551, 211, 85, 7}, {555, 211, 86, 2}, {559, 211, 87, 6}, {563, 211, 87, 2},
    {566, 211, 87, 6}, {570, 211, 87, 2}, {574, 211, 87, 5}, {578, 211, 87, 1}, {582, 211, 89, 5},
    {585, 211, 91, 1}, {589, 211, 91, 5}, {593, 211, 91, 0}, {597, 211, 92, 4}, {601, 211, 92, 0},
    {604, 211, 92, 4}, {608, 211, 92, 0}, {612, 211, 93, 3}, {616, 211, 95, 7}, {620, 211, 95, 3},
    {623, 211, 96, 7}, {627, 211, 97, 3}, {631, 211, 97, 6}, {635, 211, 98, 2}, {639, 211, 98, 6},
    {642, 209, 98, 2}, {646, 205, 98, 6}, {650, 202, 98, 1}, {654, 198, 98, 5}, {658, 194, 99, 1},
    {661, 190, 99, 5}, {665, 186, 100, 1}, {669, 183, 100, 4}, {673, 179, 101, 0}, {677, 175, 102, 4},
    {680, 171, 102, 0}, {684, 167, 102, 4}, {688, 164, 102, 7}, {692, 160, 102, 3}, {696, 156, 102, 7},
    {699, 152, 102, 3}, {703, 148, 101, 3},
};

constexpr Rect kMapBand{0, 24, 320, 132}; // where the old map shows

// A sprite's x is its middle, its y the row below it.
void drawSprite(Screen& screen, const Sprite& sprite, int middle, int bottom) {
    screen.blit(sprite.image, middle - sprite.image.width / 2, bottom - sprite.image.height + 1);
}

std::vector<const Sprite*> realSprites(const SpriteSheet& sheet) {
    std::vector<const Sprite*> out;
    for (const auto& s : sheet.sprites) {
        if (s.image.width > 0 && s.image.height > 0) {
            out.push_back(&s);
        }
    }
    return out;
}

int number(const std::vector<std::string>& fields, std::size_t i) {
    return i < fields.size() ? static_cast<int>(parseNumber(fields[i]).value_or(0)) : 0;
}

} // namespace

Intro::Intro(const TitleArt& art) {
    if (const auto* section = art.script.find("CREDITS")) {
        for (const auto& line : section->lines) {
            const auto f = splitFields(line);
            if (f.size() >= 4) {
                credits_.push_back({number(f, 0), number(f, 1), number(f, 2), number(f, 3) - 1});
            }
        }
    }
    if (const auto* section = art.script.find("OPENING")) {
        for (const auto& line : section->lines) {
            const auto f = splitFields(line);
            if (f.size() < 4) {
                continue;
            }
            const Animation a{number(f, 0), number(f, 1), number(f, 2), number(f, 3)};
            if (a.series < 0) {
                end_ = a.first; // END OF DEMO
                break;
            }
            animations_.push_back(a);
        }
    }
}

void Intro::tick(std::uint64_t milliseconds) {
    elapsed_ += milliseconds;
}

void Intro::skip() {
    elapsed_ = std::max<std::uint64_t>(elapsed_, static_cast<std::uint64_t>(end_) * kFrameMs);
}

bool Intro::finished() const {
    return frame() >= end_;
}

int Intro::frame() const {
    return static_cast<int>(elapsed_ / kFrameMs);
}

void Intro::draw(Screen& screen, const TitleArt& art) const {
    const int now = frame();
    if (art.map.palette) {
        screen.setColours(*art.map.palette, 0, 255);
    }
    copyPicture(screen, art.border.image, 0, 0);
    // The view starts at the map's right end, over Europe, and moves one
    // pixel west each frame until it reaches the coast.
    const int width = art.map.image.width;
    const int scroll = std::clamp(width - kMapBand.w - now, 0, std::max(0, width - kMapBand.w));
    for (int y = 0; y < std::min(kMapBand.h, art.map.image.height); ++y) {
        for (int x = 0; x < kMapBand.w && scroll + x < width; ++x) {
            screen.pixel(x, kMapBand.y + y, art.map.image.pixels[static_cast<std::size_t>(y * width + scroll + x)]);
        }
    }
    screen.setClip(kMapBand);
    for (const auto& a : animations_) {
        if (a.series < 0 || a.series >= static_cast<int>(art.animations.size()) || now < a.first) {
            continue;
        }
        const auto sprites = realSprites(art.animations[static_cast<std::size_t>(a.series)]);
        if (sprites.empty()) {
            continue;
        }
        const int count = static_cast<int>(sprites.size());
        const int since = now - a.first;
        int index = -1;
        if (a.series == kSun) {
            // The sun turns its face slowly, back and forth.
            const int step = count > 1 ? since / 20 % (2 * count - 2) : 0;
            index = step < count ? step : 2 * count - 2 - step;
        } else if (a.series == kGuy) {
            // Wades ashore, then stands waving: the last six over and over.
            index = since < count ? since : count - 6 + (since - count) % 6;
        } else if (a.repeats == 0) {
            // Plays once and stays: the ship aground, the title.
            index = std::min(since, count - 1);
        } else if (since < count * a.repeats) {
            index = since % count;
        }
        if (index >= 0) {
            const Sprite& sprite = *sprites[static_cast<std::size_t>(index)];
            const int x = a.baseX == 0 ? sprite.x : a.baseX + sprite.x - scroll;
            drawSprite(screen, sprite, x, sprite.y);
        }
    }
    // The ship, until it runs aground.
    const auto& ship = art.ship.sprites;
    if (!ship.empty() && now <= std::prev(std::end(kShipCourse))->frame) {
        const ShipAt* at = &kShipCourse[0];
        for (const auto& s : kShipCourse) {
            if (s.frame <= now) {
                at = &s;
            }
        }
        if (static_cast<std::size_t>(at->sprite) < ship.size()) {
            screen.blit(ship[static_cast<std::size_t>(at->sprite)].image, at->x, at->y);
        }
    }
    screen.resetClip();
    for (const auto& c : credits_) {
        if (now < c.first || now > c.last || c.sheet < 0 || c.sheet >= static_cast<int>(art.credits.size())) {
            continue;
        }
        const auto& sheet = art.credits[static_cast<std::size_t>(c.sheet)].sprites;
        if (c.sprite < 0 || c.sprite >= static_cast<int>(sheet.size())) {
            continue;
        }
        // Centred in the wood below the map.
        const auto& image = sheet[static_cast<std::size_t>(c.sprite)].image;
        screen.blit(image, (Screen::kWidth - image.width + 1) / 2, 184 - (image.height + 1) / 2);
    }
}

TitleArt loadTitleArt(const std::filesystem::path& folder) {
    TitleArt art;
    art.menu = readPicture(readFile(folder, "OPENMENU.PIK"));
    art.border = readPicture(readFile(folder, "OPENBORD.PIK"));
    art.map = readPicture(readFile(folder, "OPENING.PIK"));
    art.script = parseTextFile(readFile(folder, "OPENING.TXT"));
    art.ship = readSpriteSheet(readFile(folder, "OPENSHIP.SS"));
    for (const char* name : {"OPENCRD1.SS", "OPENCRD2.SS", "OPENCRD3.SS"}) {
        art.credits.push_back(readSpriteSheet(readFile(folder, name)));
    }
    for (const char* name : {"OPENWND1.SS", "OPENSUN.SS", "OPENMON1.SS", "OPENWND2.SS", "OPENMON2.SS",
                             "OPENMON3.SS", "OPENFISH.SS", "OPENGUY.SS", "OPENLOGO.SS", "OPENBONK.SS"}) {
        art.animations.push_back(readSpriteSheet(readFile(folder, name)));
    }
    return art;
}

MainMenu::MainMenu(const Art& art, const std::string& version, const std::string& date)
    : dialog_(makeDialog(art, "BEGINMENU", {version, date})) {
    nameProgram(dialog_);
}

std::optional<TitleChoice> MainMenu::handle(Command command) {
    switch (command) {
    case Command::MoveN:
    case Command::Up:
        stepSelection(dialog_, -1);
        break;
    case Command::MoveS:
    case Command::Down:
        stepSelection(dialog_, 1);
        break;
    case Command::Confirm:
    case Command::Skip:
        return choiceAt(dialog_.selected);
    default:
        break;
    }
    return std::nullopt;
}

std::optional<TitleChoice> MainMenu::click(int x, int y) {
    const int option = dialogOptionAt(dialog_, x, y);
    if (option < 0) {
        return std::nullopt;
    }
    dialog_.selected = option;
    return choiceAt(option);
}

void MainMenu::draw(Screen& screen, const Art& art, const TitleArt& title, const Intro* behind) const {
    if (behind != nullptr) {
        behind->draw(screen, title);
    } else {
        copyPicture(screen, title.menu.image, 0, 0);
    }
    // The menu's palette differs from the opening's in the box's colours
    // (0xFC to 0xFE); after the opening only those change.
    if (title.menu.palette) {
        screen.setColours(*title.menu.palette, behind != nullptr ? 0xFC : 0, behind != nullptr ? 0xFE : 255);
    }
    drawDialog(screen, art, dialog_);
    const Rect box{dialog_.x, dialog_.y, dialog_.width + 6, dialogHeight(dialog_)};
    const auto& wood = art.wood;
    for (int y = box.y; y < box.y + box.h; ++y) {
        for (int x = box.x; x < box.x + box.w; ++x) {
            const auto c = screen.at(x, y);
            // The wood itself takes other colours for two of its shades.
            const bool inside = x >= box.x + 3 && x < box.x + box.w - 3 && y >= box.y + 3 && y < box.y + box.h - 3;
            const bool isWood = inside && wood.width > 0 && wood.height > 0 &&
                                c == wood.pixels[static_cast<std::size_t>(((y - box.y) % wood.height) * wood.width +
                                                                          (x - box.x) % wood.width)];
            if (isWood && c == 0x8A) {
                screen.pixel(x, y, 0x36);
            } else if (isWood && c == 0x80) {
                screen.pixel(x, y, 0xF5);
            } else {
                screen.pixel(x, y, openingColour(c));
            }
        }
    }
    screen.frame({box.x + 1, box.y + 1, box.w - 2, box.h - 2}, 0x2E);
    if (!message_.empty()) {
        const int w = Screen::textWidth(art.tiny, message_);
        screen.text(art.tiny, (Screen::kWidth - w) / 2, 186, message_, {0x95, kNoColour, kNoColour});
    }
}

} // namespace viceroy
