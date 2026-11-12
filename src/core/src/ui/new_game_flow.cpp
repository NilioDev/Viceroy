// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/ui/new_game_flow.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/game_files.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string_view>

namespace viceroy {

namespace {

Bytes readFile(const std::filesystem::path& folder, std::string_view name) {
    const auto path = findFileNoCase(folder, name);
    if (!path) {
        throw FormatError("missing " + std::string(name));
    }
    std::ifstream in(*path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), {});
}

void copyPicture(Screen& screen, const IndexedImage& image) {
    for (int y = 0; y < std::min(image.height, Screen::kHeight); ++y) {
        for (int x = 0; x < std::min(image.width, Screen::kWidth); ++x) {
            screen.pixel(x, y, image.pixels[static_cast<std::size_t>(y * image.width + x)]);
        }
    }
}

// Rows of the MISC section of LABELS.TXT.
constexpr int kClickWhenFinished = 161;
constexpr int kChoose = 162;
constexpr int kDifficultyLevel = 163;
constexpr int kEasiest = 165; // to Toughest
constexpr int kSelect = 170;
constexpr int kEuropeanPower = 171;
constexpr int kImmigration = 173; // Cooperation, Conquest, Trade

constexpr int kGreen = 254; // the headings, in both pictures' palettes
constexpr int kShadow = 0;
constexpr int kEdge = 253;

// The five men and the four flags: where each picture's frame goes.
constexpr std::array<Rect, 5> kDifficultyFrames{{
    {128, 7, 68, 90}, {233, 7, 68, 90}, {23, 103, 68, 90}, {128, 103, 68, 90}, {233, 103, 68, 90}}};
constexpr std::array<int, 5> kDifficultyColours{10, 9, 14, 13, 12};
constexpr std::array<Rect, 4> kNationFrames{{{112, 13, 88, 82}, {211, 13, 88, 82}, {112, 104, 88, 82}, {211, 104, 88, 82}}};
constexpr int kDepartureFrames = 10;
// Measured on the original: a new picture every 9.6 seconds, coming in
// over about 1.3 seconds.
constexpr std::uint64_t kDepartureFrameMs = 9600;
constexpr std::uint64_t kDissolveMs = 1300;
constexpr int kDepartureWidth = 310; // @width of BUILD1
constexpr int kDepartureTop = 30;    // @y
constexpr int kDepartureLetter = 14; // in the pictures' palettes
constexpr int kDepartureEdge = 54;
constexpr int kDepartureShadow = 47;
constexpr int kPageWidth = 300;
// The scroll in the king's room (@x, @y and @width of VICEROY).
constexpr int kScrollLeft = 232;
constexpr int kScrollTop = 21;
constexpr int kScrollWidth = 78;
constexpr int kScrollStep = 8;
constexpr int kInk = 0;
constexpr int kInk2 = 242; // the soft edge of the letters
constexpr std::uint8_t kNameFrame = 0x44;
constexpr std::uint8_t kNameMark = 0x8A;

std::string label(const Art& art, int row) {
    return textLine(art.labelText, "MISC", row, "");
}

std::string upper(std::string s) {
    for (auto& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

// Text centred on a column, in one colour with a black shadow one pixel to
// the right.
void shadowed(Screen& screen, const Font& font, int centre, int y, const std::string& s, int colour, int edge = kNoColour) {
    const int x = centre - (Screen::textWidth(font, s) + 1) / 2 + 1;
    screen.text(font, x + 1, y, s, {kShadow, kNoColour, kNoColour});
    screen.text(font, x, y, s, {colour, edge, kNoColour});
}

void centred(Screen& screen, const Font& font, int centre, int y, const std::string& s, int colour) {
    const int x = centre - (Screen::textWidth(font, s) + 1) / 2 + 1;
    screen.text(font, x, y, s, {colour, kNoColour, kNoColour});
}


// A picture drawn where its sheet places it: the sheet's point is the
// middle of its bottom row.
void drawPlaced(Screen& screen, const SpriteSheet& sheet) {
    if (sheet.sprites.empty()) {
        return;
    }
    const auto& sprite = sheet.sprites.front();
    screen.blit(sprite.image, sprite.x - sprite.image.width / 2, sprite.y + 1 - sprite.image.height);
}

struct PageLine {
    std::string text;
    bool centred = false;
};

// A GAME.TXT section laid out as a page: "^^" lines are centred on their
// own ("^^_" and "^" leave a blank line), the rest is joined and wrapped to
// `limit`. %STRING0, ... are filled in.
std::vector<PageLine> pageLines(const Art& art, const Font& font, std::string_view section, int limit,
                                const std::vector<std::string>& strings = {}) {
    std::vector<PageLine> out;
    const auto* found = art.gameText.find(section);
    if (found == nullptr) {
        return out;
    }
    std::string paragraph;
    const auto flush = [&] {
        for (auto& l : wrapMarkedText(font, paragraph, limit)) {
            out.push_back({std::move(l), false});
        }
        paragraph.clear();
    };
    bool started = false;
    for (std::string line : found->lines) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.rfind('@', 0) == 0) {
            continue;
        }
        for (std::size_t i = strings.size(); i-- > 0;) {
            const std::string key = "%STRING" + std::to_string(i);
            for (auto at = line.find(key); at != std::string::npos; at = line.find(key, at + strings[i].size())) {
                line.replace(at, key.size(), strings[i]);
            }
        }
        if (line.empty()) {
            if (started) {
                break;
            }
            continue;
        }
        started = true;
        if (line[0] == '^') {
            flush();
            std::size_t at = line.rfind("^^", 0) == 0 ? 2 : 1;
            std::string text = line.substr(at);
            if (text == "_") {
                text.clear();
            }
            out.push_back({text, true});
            continue;
        }
        const auto first = line.find_first_not_of('_');
        paragraph += (paragraph.empty() ? "" : " ") + (first == std::string::npos ? std::string() : line.substr(first));
    }
    flush();
    return out;
}

void drawPage(Screen& screen, const Font& font, const std::vector<PageLine>& lines, int left, int width, int top,
              int step) {
    bool gold = false;
    int y = top;
    for (const auto& l : lines) {
        const int x = l.centred ? left + (width - markedWidth(font, l.text) + 1) / 2 : left;
        drawMarkedText(screen, font, x, y, l.text, gold);
        y += step;
    }
}

} // namespace

NewGameArt loadNewGameArt(const std::filesystem::path& folder) {
    NewGameArt art;
    art.difficulty = readPicture(readFile(folder, "DIFFICUL.PIK"));
    art.nations = readPicture(readFile(folder, "NATIONS.PIK"));
    art.panel = readPicture(readFile(folder, "WOODPANL.PIK"));
    art.audience = readPicture(readFile(folder, "KINGLSS1.PIK"));
    art.king = readSpriteSheet(readFile(folder, "KING1.SS"));
    for (const char* name : {"ENGLND1.SS", "FRANCE1.SS", "SPAIN1.SS", "DUTCH1.SS"}) {
        art.banners.push_back(readSpriteSheet(readFile(folder, name)));
    }
    art.script = readFont(readFile(folder, "FONTKING.FF"));
    for (int i = 1; i <= kDepartureFrames; ++i) {
        char name[16];
        std::snprintf(name, sizeof name, "LEVN%04d.PIK", i);
        art.departure.push_back(readPicture(readFile(folder, name)));
    }
    return art;
}

NewGameFlow::NewGameFlow(const Art& art) : art_(art), mapChoice_(makeDialog(art, "AMERICA")) {
    mapChoice_.x = (Screen::kWidth - (mapChoice_.width + 6)) / 2;
    mapChoice_.y = (Screen::kHeight - dialogHeight(mapChoice_)) / 2;
}

NewGameSetup NewGameFlow::setup() const {
    NewGameSetup s;
    s.humanNation = nation_;
    s.difficulty = difficulty_;
    for (int n = 0; n < kEuropeanNations; ++n) {
        const auto i = static_cast<std::size_t>(n);
        s.leaders[i] = n == nation_ ? name_ : textLine(art_.names, "LEADERNAME", n, "");
        s.countries[i] = textLine(art_.names, "COLONYNAME", n, "");
    }
    return s;
}

void NewGameFlow::next() {
    switch (step_) {
    case Step::MapChoice:
        // Only the original map for now; the map editor's maps come later.
        step_ = mapChoice_.selected == 0 ? Step::Difficulty : Step::Cancelled;
        break;
    case Step::Difficulty:
        step_ = Step::Nation;
        break;
    case Step::Nation:
        name_ = textLine(art_.names, "LEADERNAME", nation_, "");
        step_ = Step::Name;
        break;
    case Step::Name:
        step_ = Step::NationPage1;
        break;
    case Step::NationPage1:
        step_ = Step::NationPage2;
        break;
    case Step::NationPage2:
        step_ = Step::Audience;
        break;
    case Step::Audience:
        elapsed_ = 0;
        step_ = Step::Departure;
        break;
    case Step::Departure:
        step_ = Step::Done;
        break;
    case Step::Done:
    case Step::Cancelled:
        break;
    }
}

void NewGameFlow::handle(Command command) {
    const bool back = command == Command::MoveN || command == Command::Up || command == Command::MoveW ||
                      command == Command::Left;
    const bool forward = command == Command::MoveS || command == Command::Down || command == Command::MoveE ||
                         command == Command::Right;
    if (command == Command::Cancel) {
        step_ = Step::Cancelled;
        return;
    }
    if (command == Command::Confirm || (command == Command::Skip && step_ != Step::Name)) {
        next();
        return;
    }
    switch (step_) {
    case Step::MapChoice:
        if (back || forward) {
            stepSelection(mapChoice_, back ? -1 : 1);
        }
        break;
    case Step::Difficulty:
        // Every key steps through the five in turn.
        if (back || forward) {
            difficulty_ = (difficulty_ + (back ? 4 : 1)) % 5;
        }
        break;
    case Step::Nation:
        // The flags lie in a square: up and down change the row.
        if (command == Command::MoveN || command == Command::Up || command == Command::MoveS ||
            command == Command::Down) {
            nation_ ^= 2;
        } else if (back || forward) {
            nation_ = (nation_ + (back ? 3 : 1)) % 4;
        }
        break;
    default:
        break;
    }
}

void NewGameFlow::click(int x, int y) {
    const auto inside = [&](const Rect& r) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; };
    switch (step_) {
    case Step::MapChoice:
        if (const int option = dialogOptionAt(mapChoice_, x, y); option >= 0) {
            mapChoice_.selected = option;
            next();
        }
        break;
    case Step::Difficulty:
        for (int i = 0; i < 5; ++i) {
            if (inside(kDifficultyFrames[static_cast<std::size_t>(i)])) {
                difficulty_ = i;
                return;
            }
        }
        if (inside({0, 76, 115, 16})) {
            next();
        }
        break;
    case Step::Nation:
        for (int i = 0; i < 4; ++i) {
            if (inside(kNationFrames[static_cast<std::size_t>(i)])) {
                nation_ = i;
                return;
            }
        }
        if (inside({0, 177, 115, 16})) {
            next();
        }
        break;
    case Step::Name:
        break;
    default:
        next();
        break;
    }
}

void NewGameFlow::type(char c) {
    constexpr std::size_t kMaxName = 23;
    if (step_ == Step::Name && c >= ' ' && static_cast<unsigned char>(c) < 0x7F && name_.size() < kMaxName) {
        name_ += c;
    }
}

void NewGameFlow::backspace() {
    if (step_ == Step::Name && !name_.empty()) {
        name_.pop_back();
    }
}

void NewGameFlow::tick(std::uint64_t milliseconds) {
    elapsed_ += milliseconds;
    if (step_ == Step::Departure && elapsed_ >= kDepartureFrames * kDepartureFrameMs) {
        step_ = Step::Done;
    }
}

void NewGameFlow::draw(Screen& screen, const Art& art, const NewGameArt& pictures) const {
    screen.resetColours();
    switch (step_) {
    case Step::MapChoice:
        screen.clear(0);
        drawDialog(screen, art, mapChoice_);
        break;
    case Step::Difficulty: {
        copyPicture(screen, pictures.difficulty.image);
        if (pictures.difficulty.palette) {
            screen.setColours(*pictures.difficulty.palette, 0, 255);
        }
        shadowed(screen, art.labels, 57, 16, label(art, kChoose), kGreen, kEdge);
        shadowed(screen, art.labels, 57, 29, label(art, kDifficultyLevel), kGreen, kEdge);
        centred(screen, art.tiny, 57, 81, "(" + label(art, kClickWhenFinished) + ")", kGreen);
        const auto& r = kDifficultyFrames[static_cast<std::size_t>(difficulty_)];
        const int colour = kDifficultyColours[static_cast<std::size_t>(difficulty_)];
        screen.frame(r, static_cast<std::uint8_t>(colour));
        const int middle = r.x + r.w / 2;
        shadowed(screen, art.tiny, middle, r.y + 38, upper(textLine(art.names, "DIFFICULTY", difficulty_, "")) + ":", colour);
        shadowed(screen, art.tiny, middle, r.y + 46, label(art, kEasiest + difficulty_), colour);
        break;
    }
    case Step::Nation: {
        copyPicture(screen, pictures.nations.image);
        if (pictures.nations.palette) {
            screen.setColours(*pictures.nations.palette, 0, 255);
        }
        shadowed(screen, art.labels, 56, 36, label(art, kSelect), kGreen, kEdge);
        shadowed(screen, art.labels, 56, 49, label(art, kEuropeanPower), kGreen, kEdge);
        centred(screen, art.tiny, 56, 182, "(" + label(art, kClickWhenFinished) + ")", kGreen);
        const auto& r = kNationFrames[static_cast<std::size_t>(nation_)];
        const int colour = nationColour(art, nation_);
        screen.frame(r, static_cast<std::uint8_t>(colour));
        const int middle = r.x + r.w / 2;
        shadowed(screen, art.tiny, middle, r.y + 2, upper(textLine(art.names, "COUNTRY", nation_, "")) + ":", colour);
        shadowed(screen, art.tiny, middle, r.y + 74, label(art, kImmigration + nation_), colour);
        break;
    }
    case Step::Name: {
        copyPicture(screen, pictures.panel.image);
        drawPage(screen, art.labels, pageLines(art, art.labels, "LEADERNAME", 296), 0, Screen::kWidth, 88, 10);
        constexpr Rect kField{79, 98, 168, 14};
        screen.frame(kField, kNameFrame);
        const int w = Screen::textWidth(art.labels, name_);
        screen.fill({kField.x + 2, kField.y + 2, w + 1, kField.h - 4}, kNameMark);
        bool gold = false;
        drawMarkedText(screen, art.labels, kField.x + 3, kField.y + 3, name_, gold);
        break;
    }
    case Step::NationPage1:
    case Step::NationPage2: {
        copyPicture(screen, pictures.panel.image);
        const std::string section =
            "NATION" + std::to_string(nation_) + (step_ == Step::NationPage1 ? "A" : "B");
        const auto lines = pageLines(art, art.labels, section, kPageWidth - 4);
        constexpr int kStep = 10;
        const int top = (Screen::kHeight - kStep * static_cast<int>(lines.size())) / 2;
        drawPage(screen, art.labels, lines, (Screen::kWidth - kPageWidth) / 2, kPageWidth, top, kStep);
        break;
    }
    case Step::Audience: {
        copyPicture(screen, pictures.audience.image);
        if (pictures.audience.palette) {
            screen.setColours(*pictures.audience.palette, 0, 255);
        }
        const auto banner = static_cast<std::size_t>(nation_);
        if (banner < pictures.banners.size()) {
            drawPlaced(screen, pictures.banners[banner]);
        }
        drawPlaced(screen, pictures.king);
        // The Dutch have a stadtholder instead of a king.
        const bool dutch = nation_ == 3;
        std::string country = textLine(art.names, "COUNTRY", nation_, "");
        if (dutch) {
            country = "the " + country;
        }
        const auto* section = art.gameText.find(dutch ? "VICEROY2" : "VICEROY");
        int y = kScrollTop;
        std::string paragraph;
        const auto flush = [&] {
            for (const auto& l : wrapMarkedText(pictures.script, paragraph, kScrollWidth)) {
                screen.text(pictures.script, kScrollLeft, y, l, {kInk2, kNoColour, kInk});
                y += kScrollStep;
            }
            paragraph.clear();
        };
        if (section != nullptr) {
            for (std::string line : section->lines) {
                if (line.rfind('@', 0) == 0) {
                    continue;
                }
                if (line.empty()) {
                    if (y > kScrollTop) {
                        break;
                    }
                    continue;
                }
                for (auto at = line.find("%COUNTRY"); at != std::string::npos; at = line.find("%COUNTRY")) {
                    line.replace(at, 8, country);
                }
                if (line[0] == '^') {
                    flush();
                    const std::string text = line.substr(line.rfind("^^", 0) == 0 ? 2 : 1);
                    const int w = Screen::textWidth(pictures.script, text);
                    screen.text(pictures.script, kScrollLeft + (kScrollWidth - w + 1) / 2, y, text,
                                {kInk2, kNoColour, kInk});
                    y += kScrollStep;
                } else {
                    paragraph += (paragraph.empty() ? "" : " ") + line;
                }
            }
        }
        flush();
        break;
    }
    case Step::Departure: {
        const int frame = static_cast<int>(std::min<std::uint64_t>(elapsed_ / kDepartureFrameMs, kDepartureFrames - 1));
        const auto into = elapsed_ - static_cast<std::uint64_t>(frame) * kDepartureFrameMs;
        Screen previous;
        if (frame > 0) {
            drawDeparture(previous, art, pictures, frame - 1);
        }
        drawDeparture(screen, art, pictures, frame);
        // The new picture comes in pixel by pixel, in a scattered order.
        if (into < kDissolveMs) {
            const auto shown = static_cast<std::uint32_t>(Screen::kWidth * Screen::kHeight * into / kDissolveMs);
            std::uint32_t at = 0;
            for (std::uint32_t i = 0; i < Screen::kWidth * Screen::kHeight; ++i) {
                at = (at * 4097U + 12345U) % (Screen::kWidth * Screen::kHeight);
                if (i >= shown) {
                    const int x = static_cast<int>(at % Screen::kWidth);
                    const int y = static_cast<int>(at / Screen::kWidth);
                    screen.pixel(x, y, previous.at(x, y));
                }
            }
        }
        break;
    }
    default:
        screen.clear(0);
        break;
    }
}

void NewGameFlow::drawDeparture(Screen& screen, const Art& art, const NewGameArt& pictures, int frame) const {
    const auto i = static_cast<std::size_t>(frame);
    if (i < pictures.departure.size()) {
        copyPicture(screen, pictures.departure[i].image);
        if (pictures.departure[i].palette) {
            screen.setColours(*pictures.departure[i].palette, 0, 255);
        }
    }
    // The words for each picture: BUILD1 to BUILD10.
    const bool dutch = nation_ == 3;
    const std::string country = (dutch ? "the " : "") + textLine(art.names, "COUNTRY", nation_, "");
    std::vector<std::string> strings;
    switch (frame) {
    case 1:
        strings = {textLine(art.names, "DIFFICULTY", difficulty_, ""), name_};
        break;
    case 2:
        strings = {textLine(art.names, "HOMEPORT", nation_, "")};
        break;
    case 3:
        strings = {country, dutch ? "Stadtholder" : "King"};
        break;
    case 6:
        strings = {country};
        break;
    default:
        break;
    }
    const auto lines = pageLines(art, art.labels, "BUILD" + std::to_string(frame + 1), kDepartureWidth, strings);
    int y = kDepartureTop;
    for (const auto& l : lines) {
        const int w = markedWidth(art.labels, l.text);
        screen.text(art.labels, (Screen::kWidth - kDepartureWidth) / 2 + (kDepartureWidth - w + 1) / 2, y, l.text,
                    {kDepartureLetter, kDepartureEdge, kDepartureShadow});
        y += 10;
    }
}

} // namespace viceroy
