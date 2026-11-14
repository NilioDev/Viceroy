// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/render/dialog.hpp"

#include "viceroy/data/text_table.hpp"
#include "viceroy/game/europe.hpp"
#include "viceroy/game/natives.hpp"
#include "viceroy/render/game_view.hpp"
#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>

namespace viceroy {

namespace {

constexpr std::uint8_t kBlack = 0x00;
constexpr std::uint8_t kRing = 0x86;
constexpr std::uint8_t kEdgeDark = 0x80;
constexpr std::uint8_t kEdgeLight = 0x8A;
constexpr std::uint8_t kBar = 0x8A;
// @COLORS in NAMES.TXT: basic 68, hilite 149, enhance 128, shadow 47;
// font pixel values 1, 2 and 3 take the letter, enhance and shadow.
constexpr TextColours kBasic{0x44, 0x80, 0x2F};
constexpr TextColours kCheckColours{0x95, 0x80, 0x2F}; // seen in the Sound Options
constexpr TextColours kSmallChosen{0x95, 0x80, 0x2F};
constexpr TextColours kHilite{0x95, 0x80, 0x2F};
constexpr TextColours kGrey{0x08, 0x80, 0x2F};
constexpr std::uint8_t kGold = 0x95;

constexpr int kTextLeft = 5;
constexpr int kUnitIndent = 22; // a unit's picture beside the text
constexpr int kTextTop = 6;
constexpr int kLineStep = 10;
constexpr int kOptionGap = 4;
constexpr int kOptionStep = 12;
// With @smallfont.
constexpr int kSmallLineStep = 6;
constexpr int kSmallOptionStep = 8;
// The values on the right end this far inside the box.
constexpr int kValueRight = 13;

void replaceAll(std::string& text, const std::string& key, const std::string& value) {
    for (auto at = text.find(key); at != std::string::npos; at = text.find(key, at + value.size())) {
        text.replace(at, key.size(), value);
    }
}

std::string fill(std::string text, const std::vector<std::string>& strings, const std::vector<int>& numbers) {
    // Highest first, so %STRING1 is not taken for %STRING10.
    for (std::size_t i = strings.size(); i-- > 0;) {
        replaceAll(text, "%STRING" + std::to_string(i), strings[i]);
    }
    for (std::size_t i = numbers.size(); i-- > 0;) {
        replaceAll(text, "%NUMBER" + std::to_string(i), std::to_string(numbers[i]));
    }
    replaceAll(text, "%%", "%"); // a percent sign
    return text;
}

const Font& fontOf(const Art& art, const Dialog& d) {
    return d.small ? art.tiny : art.labels;
}

// Width of text without the braces.
int plainWidth(const Font& font, const std::string& s) {
    std::string plain;
    for (const char c : s) {
        if (c != '{' && c != '}') {
            plain += c;
        }
    }
    return Screen::textWidth(font, plain);
}

std::vector<std::string> wrap(const Font& font, const std::string& text, int limit) {
    // The lines are drawn with one space between words. Measured: where
    // the text file breaks a line the original measures two spaces, so
    // "Your Excellency. Our fur traders" + "cannot" (one pixel short of
    // the limit with one space) goes onto the next line; spaces inside a
    // line count once.
    std::vector<std::string> lines;
    std::string line;     // as drawn
    std::string measured; // with the line breaks as two spaces
    std::size_t i = 0;
    while (i < text.size()) {
        bool lineBreak = false;
        while (i < text.size() && (text[i] == ' ' || text[i] == '\n')) {
            lineBreak = lineBreak || text[i] == '\n';
            ++i;
        }
        std::size_t end = text.find_first_of(" \n", i);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string word = text.substr(i, end - i);
        i = end;
        if (word.empty()) {
            continue;
        }
        if (line.empty()) {
            line = word;
            measured = word;
            continue;
        }
        const std::string candidate = measured + (lineBreak ? "  " : " ") + word;
        if (plainWidth(font, candidate) > limit) {
            lines.push_back(line);
            line = word;
            measured = word;
        } else {
            line += " " + word;
            measured = candidate;
        }
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

// Draws a line, switching to gold between braces; returns nothing.
void drawMarkedLine(Screen& screen, const Font& font, int x, int y, const std::string& line, bool& gold,
                    const TextColours& colours = kBasic) {
    std::string run;
    auto flush = [&] {
        x = screen.text(font, x, y, run, gold ? kHilite : colours);
        run.clear();
    };
    for (const char c : line) {
        if (c == '{' || c == '}') {
            flush();
            gold = c == '{';
        } else {
            run += c;
        }
    }
    flush();
}

int boxWidth(const Dialog& d) {
    return d.width + 6 + d.indent;
}

} // namespace

int markedWidth(const Font& font, const std::string& text) {
    return plainWidth(font, text);
}

std::vector<std::string> wrapMarkedText(const Font& font, const std::string& text, int limit) {
    return wrap(font, text, limit);
}

void drawMarkedText(Screen& screen, const Font& font, int x, int y, const std::string& line, bool& gold) {
    drawMarkedLine(screen, font, x, y, line, gold);
}

Dialog makeDialog(const Art& art, std::string_view section, const std::vector<std::string>& strings,
                  const std::vector<int>& numbers, int nation) {
    Dialog d;
    std::string country;
    if (nation >= 0) {
        country = countryName(art, nation);
    }
    const auto* found = art.gameText.find(section);
    if (found == nullptr) {
        return d;
    }
    std::vector<std::string> paragraphs; // a line starting with ^ starts a new one
    bool inOptions = false;
    bool seenText = false;
    int fixedY = -1;
    for (const auto& raw : found->lines) {
        std::string line = fill(raw, strings, numbers);
        if (nation >= 0) {
            replaceAll(line, "%COUNTRY", country);
        }
        if (!line.empty() && line[0] == '@') {
            if (line == "@smallfont") {
                d.small = true;
            } else if (line == "@checkbox") {
                d.checkbox = true;
            } else if (line == "@options") {
                inOptions = true;
            } else if (line.rfind("@y=", 0) == 0) {
                fixedY = std::atoi(line.c_str() + 3);
            } else if (line.rfind("@width=", 0) == 0) {
                d.width = std::max(40, std::atoi(line.c_str() + 7));
            } else if (line.rfind("@default=", 0) == 0) {
                d.selected = std::max(0, std::atoi(line.c_str() + 9) - 1);
            }
            continue;
        }
        const bool blank = line.find_first_not_of(" \t") == std::string::npos;
        if (!inOptions) {
            if (blank) {
                if (seenText) {
                    inOptions = true;
                }
                continue;
            }
            seenText = true;
            if (line[0] == '^' || paragraphs.empty()) {
                paragraphs.push_back(line[0] == '^' ? line.substr(1) : line);
            } else {
                paragraphs.back() += "\n" + line;
            }
        } else {
            if (blank) {
                if (!d.options.empty()) {
                    break;
                }
                continue;
            }
            if (const auto mark = line.find('~'); mark != std::string::npos && mark + 1 < line.size()) {
                // The key letter, in gold (the Sound Options mark theirs).
                line = line.substr(0, mark) + "{" + line[mark + 1] + "}" + line.substr(mark + 2);
            }
            addOption(d, line);
        }
    }
    if (d.checkbox) {
        d.checked.assign(d.options.size(), false);
    }
    for (const auto& paragraph : paragraphs) {
        for (auto& l : wrap(fontOf(art, d), paragraph, d.width - kTextLeft)) {
            d.lines.push_back(std::move(l));
        }
    }
    d.selected = d.options.empty() ? 0 : std::min(d.selected, static_cast<int>(d.options.size()) - 1);
    d.x = (Screen::kWidth - boxWidth(d)) / 2;
    d.y = fixedY >= 0 ? fixedY : (Screen::kHeight - dialogHeight(d)) / 2;
    return d;
}

void addOption(Dialog& d, std::string text, std::string value, bool disabled) {
    d.options.push_back(std::move(text));
    d.values.push_back(std::move(value));
    d.disabled.push_back(disabled);
}

void placeEuropeDialog(Dialog& d, bool help) {
    d.help = help;
    d.optionLeft = 13;
    d.portrait = kEuropeAgent;
    d.portraitX = 99;
    d.portraitBehind = false;
    const int h = dialogHeight(d);
    d.x = (Screen::kWidth - boxWidth(d)) / 2;
    // Centred on the harbour below the agent, rows 80 to 199.
    d.y = (279 - h) / 2;
    if (d.y + h > Screen::kHeight) {
        // Too tall: centred on the screen, and the agent is not shown.
        d.y = (Screen::kHeight - h) / 2;
        d.portrait = -1;
    }
    // The agent leans on the box with his hands over its top edge.
    d.portraitY = d.y - 78;
}

void placeFrontiersmanDialog(Dialog& d) {
    d.portrait = kFrontiersman;
    d.portraitBehind = false; // his rifle lies over the box
    d.x = (Screen::kWidth - boxWidth(d)) / 2;
    d.y = (288 - dialogHeight(d)) / 2;
    d.portraitX = 86;
    d.portraitY = d.y - 87;
}

void placeReligiousDialog(Dialog& d) {
    d.portrait = kReligiousAdvisor;
    d.portraitBehind = false;
    d.x = (Screen::kWidth - boxWidth(d)) / 2;
    d.y = (252 - dialogHeight(d)) / 2;
    d.portraitX = 114;
    d.portraitY = d.y - 52;
}

void placeColonialDialog(Dialog& d) {
    d.portrait = kColonialAdvisor;
    d.portraitBehind = false; // his musket lies over the box
    d.x = 39;
    d.y = (278 - dialogHeight(d)) / 2;
    d.portraitX = 210;
    d.portraitY = d.y - 77;
}

void placeMapDialog(Dialog& d) {
    placeColonialDialog(d);
    d.x = 54;
    d.portraitX = 195;
}

void placeBuyDialog(Dialog& d) {
    d.portrait = kBuyAdvisor;
    d.portraitBehind = false; // her hands lie over the box
    d.x = (Screen::kWidth - boxWidth(d)) / 2;
    d.y = 164 - dialogHeight(d);
    d.portraitX = 130;
    d.portraitY = d.y - 62;
}

void placeUnitDialog(Dialog& d, int unit) {
    d.unit = unit;
    d.indent = kUnitIndent;
    d.portrait = -1;
    d.x = (Screen::kWidth - boxWidth(d) + 1) / 2;
    d.y = (Screen::kHeight - dialogHeight(d)) / 2;
}

void placeCentredDialog(Dialog& d) {
    // Measured: the box centred on the screen, rows 68 to 131 for a box
    // 64 high.
    d.portrait = -1;
    d.x = (Screen::kWidth - boxWidth(d)) / 2;
    d.y = (Screen::kHeight - dialogHeight(d)) / 2;
}

void placeNativeDialog(Dialog& d, int tribe, int mood, int chiefWidth, int chiefHeight) {
    // Measured on five tribes: the box is centred on the screen's rows; the
    // Iroquois and Apache stand left of it, the Arawaks, Cherokee and Sioux
    // right. The others are not measured yet and stand right like the Sioux.
    d.portrait = kNativePortraits + 4 * tribe + std::clamp(mood, 0, 3);
    d.portraitBehind = false;
    const int w = boxWidth(d);
    d.y = (Screen::kHeight - dialogHeight(d)) / 2;
    constexpr int kArawaks = 2, kIroquois = 3, kCherokee = 4, kApache = 5;
    const bool left = tribe == kIroquois || tribe == kApache;
    const int centre = left ? 207 : tribe == kCherokee ? 132 : tribe == kArawaks ? 113 : 105;
    d.x = std::clamp(centre - w / 2, 0, Screen::kWidth - w);
    // The Sioux chief was measured at rows 10 to 186 right of the box.
    d.portraitY = 10;
    d.portraitX = left ? 4 : 210;
    if (chiefWidth > 0 && chiefHeight > 0) {
        // Measured on the Iroquois (two moods) and the Apache: the chief
        // is centred on the rows; one standing left and the box, 6 apart,
        // are centred together on the screen.
        constexpr int kGap = 6;
        d.portraitY = (Screen::kHeight - 2 - chiefHeight) / 2;
        if (left) {
            d.x = (Screen::kWidth + chiefWidth + kGap - w + 1) / 2;
            d.portraitX = d.x - kGap - chiefWidth;
        }
    }
}

void placeNavalDialog(Dialog& d) {
    // Measured with the high seas question: the naval adviser stands over
    // the box's left end.
    d.portrait = kNavalAdvisor;
    d.x = 64;
    d.y = 108;
    d.portraitX = 60;
    d.portraitY = 37;
}

void placeTutorialDialog(Dialog& d) {
    d.portrait = kNavalAdvisor;
    d.portraitBehind = false; // his hands lie over the box
    // The advisor stands 4 columns left of the box and 71 rows above it; the
    // two together are centred on the screen (measured with one box; the
    // woman advisor's tutorial box follows the same rule).
    d.x = (Screen::kWidth + 4 - boxWidth(d)) / 2;
    d.y = (Screen::kHeight + 72 - dialogHeight(d)) / 2;
    d.portraitX = d.x - 4;
    d.portraitY = d.y - 71;
}

void placeTradeDialog(Dialog& d) {
    placeBuyDialog(d);
    d.y = (262 - dialogHeight(d)) / 2;
    d.portraitY = d.y - 62;
}

std::string countryName(const Art& art, int nation) {
    constexpr int kDutch = 3;
    return (nation == kDutch ? "the " : "") + textLine(art.names, "COUNTRY", nation, "");
}

int checkboxForLetter(const Dialog& d, char letter) {
    if (!d.checkbox) {
        return -1;
    }
    const int want = std::toupper(static_cast<unsigned char>(letter));
    for (std::size_t i = 0; i < d.options.size(); ++i) {
        for (const char c : d.options[i]) {
            if (c != '{' && c != '}' && c != '"' && c != ' ') {
                if (std::toupper(static_cast<unsigned char>(c)) == want) {
                    return static_cast<int>(i);
                }
                break;
            }
        }
    }
    return -1;
}

void stepSelection(Dialog& d, int by) {
    const int n = static_cast<int>(d.options.size());
    for (int k = 1; k <= n; ++k) {
        const int at = ((d.selected + by * k) % n + n) % n;
        if (static_cast<std::size_t>(at) >= d.disabled.size() || !d.disabled[static_cast<std::size_t>(at)]) {
            d.selected = at;
            return;
        }
    }
}

int dialogHeight(const Dialog& d) {
    const int lineStep = d.small ? kSmallLineStep : kLineStep;
    const int optionStep = d.small ? kSmallOptionStep : kOptionStep;
    const int textBottom = kTextTop + lineStep * static_cast<int>(d.lines.size());
    const int helpRoom = d.help ? (d.small ? 6 : 3) : 0;
    if (d.options.empty()) {
        return textBottom + kOptionGap + 2 + helpRoom;
    }
    return textBottom + kOptionGap + optionStep * static_cast<int>(d.options.size()) + 2 + helpRoom;
}

int dialogOptionAt(const Dialog& d, int x, int y) {
    const Rect box{d.x, d.y, boxWidth(d), dialogHeight(d)};
    if (x < box.x + 4 || x >= box.x + box.w - 4) {
        return -1;
    }
    int top = box.y + kTextTop + static_cast<int>(d.lines.size()) * (d.small ? kSmallLineStep : kLineStep) + kOptionGap;
    const int step = d.small ? kSmallOptionStep : kOptionStep;
    for (int i = 0; i < static_cast<int>(d.options.size()); ++i) {
        if (y >= top - 1 && y < top - 1 + step) {
            return i;
        }
        top += step;
    }
    return -1;
}

void drawWoodBox(Screen& screen, const Art& art, Rect box) {
    screen.setClip({box.x + 1, box.y + 1, box.w - 2, box.h - 2});
    const auto& wood = art.wood;
    if (wood.width > 0 && wood.height > 0) {
        for (int y = box.y; y < box.y + box.h; ++y) {
            for (int x = box.x; x < box.x + box.w; ++x) {
                screen.pixel(x, y,
                             wood.pixels[static_cast<std::size_t>(((y - box.y) % wood.height) * wood.width +
                                                                  (x - box.x) % wood.width)]);
            }
        }
    }
    screen.resetClip();
    screen.frame(box, kBlack);
    screen.frame({box.x + 1, box.y + 1, box.w - 2, box.h - 2}, kRing);
    screen.fill({box.x + 2, box.y + 2, box.w - 4, 1}, kEdgeDark);
    screen.fill({box.x + box.w - 3, box.y + 2, 1, box.h - 4}, kEdgeDark);
    screen.fill({box.x + 2, box.y + 3, 1, box.h - 5}, kEdgeLight);
    screen.fill({box.x + 2, box.y + box.h - 3, box.w - 4, 1}, kEdgeLight);
}

void drawCombatAnalysis(Screen& screen, const Art& art, const Game& game, const CombatAnalysis& analysis) {
    // Measured on the original: a box 214 wide centred on the screen, the
    // title, then a row of 20 pixels for each side's unit and for each
    // bonus, the attacker on the left and the defender from x + 107.
    const std::size_t rows = 1 + std::max(analysis.attacker.bonuses.size(), analysis.defender.bonuses.size());
    const int w = 214;
    const int h = 26 + 20 * static_cast<int>(rows);
    const Rect box{(Screen::kWidth - w) / 2, (Screen::kHeight - h) / 2, w, h};
    drawWoodBox(screen, art, box);
    const Font& font = art.tiny;
    const std::string title = textLine(art.labelText, "MISC", combat_label::kTitle, "COMBAT ANALYSIS");
    // Measured: one pixel right of the centre our widths give.
    screen.text(font, box.x + (w - Screen::textWidth(font, title)) / 2 + 1, box.y + 9, title, kBasic);
    const auto label = [&](int row) { return textLine(art.labelText, "MISC", row, ""); };
    const auto side = [&](const CombatSide& s, int left) {
        int y = box.y + 24;
        std::string name;
        if (s.unit >= 0) {
            drawUnitOrders(screen, art, game, s.unit, left, y - 1);
            name = game.unitName(s.unit);
        } else if (s.colony >= 0 && s.colony < static_cast<int>(game.state().colonies.size())) {
            // Seen: the colonist by their skill, "Ore Miner".
            drawColonyDefender(screen, art, game, s.colony, s.colonist, s.armed, left, y - 1);
            const auto& c = game.state().colonies[static_cast<std::size_t>(s.colony)];
            const int job = professionJob(c.skills[static_cast<std::size_t>(std::clamp(s.colonist, 0, kMaxColonists - 1))]);
            const auto& jobs = game.rules().jobs;
            name = job >= 0 && job < static_cast<int>(jobs.size()) ? jobs[static_cast<std::size_t>(job)].name : "";
        } else if (s.settlement >= 0 && s.settlement < static_cast<int>(game.state().settlements.size())) {
            // Seen: the village's own "Braves".
            const int nation = game.state().settlements[static_cast<std::size_t>(s.settlement)].nation;
            drawBraveDefender(screen, art, game, s.braveType, nation, left, y - 1);
            const auto& types = game.rules().units;
            name = s.braveType >= 0 && s.braveType < static_cast<int>(types.size())
                       ? types[static_cast<std::size_t>(s.braveType)].name
                       : "";
        }
        screen.text(font, left + 17, y + 5, name, kBasic);
        const std::string base = std::to_string(s.base);
        screen.text(font, left + 80 - Screen::textWidth(font, base), y + 5, base, kHilite);
        for (const auto& b : s.bonuses) {
            y += 20;
            if (b.terrainPicture && s.unit >= 0) {
                const auto& u = game.unit(s.unit);
                drawTerrain(screen, art, game, u.x, u.y, left, y - 1);
            }
            std::string text = b.text;
            const auto& settlements = game.state().settlements;
            if (b.settlementPicture >= 0 && b.settlementPicture < static_cast<int>(settlements.size())) {
                // Seen: "Camp" beside the camp: the second column of @LEVELS.
                const auto& settlement = settlements[static_cast<std::size_t>(b.settlementPicture)];
                drawSettlementPicture(screen, art, settlement, left, y - 1);
                if (text.empty()) {
                    if (const auto* levels = art.names.find("LEVELS")) {
                        const bool capital = (settlement.raw[3] & kSettlementCapital) != 0;
                        const auto row = static_cast<std::size_t>(capital ? 4 : tribeLevel(game.state(), tribeOf(settlement)));
                        const auto fields =
                            row < levels->lines.size() ? splitFields(levels->lines[row]) : std::vector<std::string>{};
                        text = fields.size() > 1 ? fields[1] : "";
                    }
                }
            }
            const bool picture = b.terrainPicture || b.colonyPicture || b.settlementPicture >= 0;
            if (b.colonyPicture) {
                const int x = s.unit >= 0 ? game.unit(s.unit).x : -1;
                const int yy = s.unit >= 0 ? game.unit(s.unit).y : -1;
                const auto colony = s.colony >= 0 ? std::optional<int>(s.colony) : game.colonyAt(x, yy);
                if (colony) {
                    drawColonyPicture(screen, art, game.state().colonies[static_cast<std::size_t>(*colony)], left, y - 1);
                }
            }
            screen.text(font, picture ? left + 17 : left, y + 5, text.empty() ? label(b.label) : text, kBasic);
            const std::string value = b.points != 0 ? (b.points > 0 ? "+" : "") + std::to_string(b.points)
                                                    : (b.percent > 0 ? "+" : "") + std::to_string(b.percent) + "%";
            screen.text(font, left + 80 - Screen::textWidth(font, value), y + 5, value, kHilite);
        }
    };
    side(analysis.attacker, box.x + 5);
    side(analysis.defender, box.x + 107);
}

void drawDialog(Screen& screen, const Art& art, const Dialog& d) {
    const Rect box{d.x, d.y, boxWidth(d), dialogHeight(d)};
    const bool hasPortrait = d.portrait >= 0 && static_cast<std::size_t>(d.portrait) < art.portraits.size() &&
                             !art.portraits[static_cast<std::size_t>(d.portrait)].sprites.empty();
    if (hasPortrait) {
        // The portraits' own colours: palette entries 152 to 251.
        const auto& sheet = art.portraits[static_cast<std::size_t>(d.portrait)];
        if (sheet.palette) {
            screen.setColours(*sheet.palette, 152, 251);
        }
    }
    if (hasPortrait && d.portraitBehind) {
        screen.blit(art.portraits[static_cast<std::size_t>(d.portrait)].sprites.front().image, d.portraitX,
                    d.portraitY);
    }
    drawWoodBox(screen, art, box);

    const Font& font = fontOf(art, d);
    bool gold = false;
    int y = box.y + kTextTop;
    for (const auto& line : d.lines) {
        drawMarkedLine(screen, font, box.x + kTextLeft + d.indent, y, line, gold);
        y += d.small ? kSmallLineStep : kLineStep;
    }
    y += kOptionGap;
    for (std::size_t i = 0; i < d.options.size(); ++i) {
        if (static_cast<int>(i) == d.selected) {
            screen.fill({box.x + 4 + d.indent, y - 1, box.w - 8 - d.indent, d.small ? 7 : 11}, kBar);
        }
        const bool grey = i < d.disabled.size() && d.disabled[i];
        // In small print the chosen line is gold (seen in Pick Music).
        const bool goldLine = d.small && static_cast<int>(i) == d.selected && !grey;
        const TextColours& colours = grey ? kGrey : goldLine ? kSmallChosen : kBasic;
        bool optionGold = false;
        int left = box.x + d.optionLeft + d.indent;
        if (d.checkbox) {
            // The font's ']' is the ring with a dot, '[' the filled ring.
            const std::string mark = i < d.checked.size() && d.checked[i] ? "]" : "[";
            screen.text(font, left, y, mark, kCheckColours);
            left += Screen::textWidth(font, mark + " ");
        }
        drawMarkedLine(screen, font, left, y, d.options[i], optionGold, colours);
        if (i < d.values.size() && !d.values[i].empty()) {
            const int right = box.x + box.w - kValueRight;
            screen.text(font, right - Screen::textWidth(font, d.values[i]), y, d.values[i], colours);
        }
        y += d.small ? kSmallOptionStep : kOptionStep;
    }
    if (d.help) {
        const std::string help = "(F1 for Help)";
        screen.text(art.tiny, box.x + box.w - 2 - Screen::textWidth(art.tiny, help), box.y + box.h - (d.small ? 9 : 7), help,
                    {kGold, kNoColour, kNoColour});
    }

    if (hasPortrait && !d.portraitBehind) {
        screen.blit(art.portraits[static_cast<std::size_t>(d.portrait)].sprites.front().image, d.portraitX,
                    d.portraitY);
    }
}

} // namespace viceroy
