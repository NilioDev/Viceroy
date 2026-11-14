// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/render/pedia_view.hpp"

#include "viceroy/render/dialog.hpp"

#include <algorithm>
#include <cstdlib>

namespace viceroy {

namespace {

constexpr std::uint8_t kWhite = 0x0F;
constexpr std::uint8_t kGreen = 0x44;
constexpr std::uint8_t kGold = 0x95;
constexpr std::uint8_t kBar = 0x8A;

// Rows of LABELS.TXT @MISC.
constexpr int kTitleLabel = 108; // ENCYCLOPEDIA OF COLONIZATION
constexpr int kExitLabel = 110;  // (Exit)

// Measured on the original.
constexpr int kTitleY = 5;
constexpr int kSubtitleY = 13;
constexpr int kListX = 7;
constexpr int kListY = 26;
constexpr int kColumnStep = 100;
constexpr int kRowStep = 7;
constexpr int kTextX = 10;
constexpr int kTextWidth = 300;

void say(Screen& screen, const Art& art, int x, int y, const std::string& text, std::uint8_t colour) {
    screen.text(art.tiny, x, y, text, {colour, colour, colour});
}

int measure(const Art& art, const std::string& text) {
    // The original measures text one pixel short of the glyphs' widths.
    return Screen::textWidth(art.tiny, text) - 1;
}

void sayCentred(Screen& screen, const Art& art, int y, const std::string& text, std::uint8_t colour) {
    say(screen, art, std::max(0, Screen::kWidth / 2 - measure(art, text) / 2), y, text, colour);
}

std::string firstField(const std::string& line) {
    const auto fields = splitFields(line);
    return fields.empty() ? std::string() : fields.front();
}

// The rows of a NAMES.TXT table, first field only.
std::vector<std::string> tableNames(const Art& art, const char* section) {
    std::vector<std::string> names;
    if (const auto* s = art.names.find(section)) {
        for (const auto& line : s->lines) {
            auto name = firstField(line);
            if (!name.empty()) {
                names.push_back(std::move(name));
            }
        }
    }
    return names;
}

const char* sectionPrefix(int part) {
    switch (part) {
    case pedia::kCargo:
        return "CARGO";
    case pedia::kUnit:
        return "UNIT";
    case pedia::kTerrain:
        return "TERRAIN";
    case pedia::kSkill:
        return "JOB";
    case pedia::kBuilding:
        return "BUILDING";
    default:
        return "FATHER";
    }
}

// Where a page's text starts, below the pictures of its part (measured).
int textTop(int part) {
    switch (part) {
    case pedia::kCargo:
        return 83;
    case pedia::kTerrain:
        return 85;
    case pedia::kUnit:
        return 69;
    case pedia::kSkill:
        return 119;
    case pedia::kBuilding:
        return 67;
    default:
        return 33;
    }
}

// The page's text: its first line (the name, in braces) alone, then the
// paragraphs; a line holding only ^ leaves an empty line.
std::vector<std::string> pageLines(const Art& art, int part, int id) {
    std::vector<std::string> lines;
    const auto* section = art.pedia.find(std::string(sectionPrefix(part)) + std::to_string(id));
    if (section == nullptr) {
        return lines;
    }
    std::vector<std::string> paragraphs;
    bool title = true;
    for (const auto& raw : section->lines) {
        std::string line = raw;
        while (!line.empty() && (line.back() == ' ' || line.back() == '\r')) {
            line.pop_back();
        }
        if (!line.empty() && line[0] == '@') {
            if (line.rfind("@;", 0) == 0) {
                break;
            }
            continue;
        }
        if (line.empty()) {
            continue;
        }
        if (line[0] == '^') {
            paragraphs.push_back(line.substr(1));
            title = paragraphs.size() == 1;
            continue;
        }
        if (paragraphs.empty() || title || paragraphs.back().empty()) {
            if (!paragraphs.empty() && paragraphs.back().empty()) {
                paragraphs.back() = line;
            } else {
                paragraphs.push_back(line);
            }
            title = false;
            continue;
        }
        paragraphs.back() += "\n" + line;
    }
    for (std::size_t i = 0; i < paragraphs.size(); ++i) {
        if (i > 1 || (i == 1 && paragraphs[0].empty())) {
            lines.emplace_back(); // the empty line a ^ leaves
        }
        for (auto& l : wrapMarkedText(art.tiny, paragraphs[i], kTextWidth)) {
            lines.push_back(std::move(l));
        }
    }
    return lines;
}

std::string partName(const Art& art, int part) { return textLine(art.pedia, "PEDIA", part, ""); }

} // namespace

int pediaTerrainOf(const MapSquare& square) {
    if (square.mountains()) {
        return 27;
    }
    if (square.hills()) {
        return 28;
    }
    if (square.terrain >= 24) {
        return square.terrain;
    }
    return square.isForest() ? 8 + (square.terrain & 7) : square.terrain & 7;
}

std::string pediaTerrainName(const Art& art, int terrain) {
    if (terrain >= 0 && terrain < 8) {
        return textLine(art.names, "UNFORESTED", terrain, "");
    }
    if (terrain >= 8 && terrain < 16) {
        return textLine(art.names, "FORESTED", terrain - 8, "") + " " + textLine(art.names, "OTHER_NAMES", 0, "Forest");
    }
    if (terrain >= 24 && terrain < 29) {
        return textLine(art.names, "OTHER", terrain - 24, "");
    }
    return {};
}

std::vector<PediaEntry> pediaEntries(const Art& art, int part) {
    std::vector<PediaEntry> entries;
    const auto add = [&](int id, std::string name) {
        if (!name.empty()) {
            entries.push_back({id, std::move(name)});
        }
    };
    switch (part) {
    case pedia::kCargo: {
        // Hammers, crosses, bells and flags have no page.
        const auto names = tableNames(art, "CARGO");
        for (std::size_t i = 0; i < names.size() && i < 16; ++i) {
            add(static_cast<int>(i), names[i]);
        }
        break;
    }
    case pedia::kUnit: {
        const auto names = tableNames(art, "UNIT");
        for (std::size_t i = 0; i < names.size(); ++i) {
            add(static_cast<int>(i), names[i]);
        }
        break;
    }
    case pedia::kTerrain:
        for (const int t : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 28}) {
            add(t, pediaTerrainName(art, t));
        }
        break;
    case pedia::kSkill: {
        // Seen: the Teacher (row 18) has no page in the index.
        const auto names = tableNames(art, "JOB");
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (i != 18) {
                add(static_cast<int>(i), names[i]);
            }
        }
        break;
    }
    case pedia::kBuilding: {
        // Seen: one Town Hall, and no Capitol.
        const auto names = tableNames(art, "BUILDING");
        for (std::size_t i = 0; i < names.size() && i < 42; ++i) {
            if (i == 10 || i == 11 || i == 30 || i == 31) {
                continue;
            }
            add(static_cast<int>(i), names[i]);
        }
        break;
    }
    default: {
        const auto names = tableNames(art, "FATHERS");
        for (std::size_t i = 0; i < names.size(); ++i) {
            add(static_cast<int>(i), names[i]);
        }
        break;
    }
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const PediaEntry& a, const PediaEntry& b) { return a.name < b.name; });
    return entries;
}

int pediaIndexHit(const Art& art, const PediaState& state, int x, int y) {
    const std::string exit = textLine(art.labelText, "MISC", kExitLabel, "(Exit)");
    const int exitX = Screen::kWidth - 6 - measure(art, exit);
    if (x >= exitX && y >= kTitleY - 1 && y < kTitleY + kRowStep) {
        return kPediaExit;
    }
    const auto entries = pediaEntries(art, state.part);
    if (x < kListX - 1 || y < kListY - 1) {
        return -1;
    }
    const int column = (x - (kListX - 1)) / kColumnStep;
    const int row = (y - (kListY - 1)) / kRowStep;
    if (row >= kPediaRows) {
        return -1;
    }
    const int i = column * kPediaRows + row;
    if (i < 0 || i >= static_cast<int>(entries.size())) {
        return -1;
    }
    const int left = kListX + column * kColumnStep;
    if (x > left + measure(art, entries[static_cast<std::size_t>(i)].name) + 1) {
        return -1;
    }
    return i;
}

void drawPedia(Screen& screen, const Art& art, const PediaState& state) {
    if (art.woodPanel.palette) {
        screen.setColours(*art.woodPanel.palette, 16, 255);
    }
    screen.blit(art.woodPanel.image, 0, 0);
    const std::string title = textLine(art.labelText, "MISC", kTitleLabel, "ENCYCLOPEDIA OF COLONIZATION");
    const auto entries = pediaEntries(art, state.part);
    if (state.page < 0) {
        // The index: the title in white, (Exit) in the corner, the names in
        // green, the one picked in gold on a dark bar.
        sayCentred(screen, art, kTitleY, title, kWhite);
        const std::string exit = textLine(art.labelText, "MISC", kExitLabel, "(Exit)");
        say(screen, art, Screen::kWidth - 6 - measure(art, exit), kTitleY, exit, kGreen);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const int column = static_cast<int>(i) / kPediaRows;
            const int row = static_cast<int>(i) % kPediaRows;
            const int x = kListX + column * kColumnStep;
            const int y = kListY + row * kRowStep;
            const bool picked = static_cast<int>(i) == state.selected;
            if (picked) {
                screen.fill({x - 1, y - 1, measure(art, entries[i].name) + 2, kRowStep}, kBar);
            }
            say(screen, art, x, y, entries[i].name, picked ? kGold : kGreen);
        }
        return;
    }
    // A page: the title and "(Name:  Part)" in gold, then the text.
    std::string name;
    for (const auto& e : entries) {
        if (e.id == state.page) {
            name = e.name;
        }
    }
    sayCentred(screen, art, kTitleY, title, kGold);
    sayCentred(screen, art, kSubtitleY, "(" + name + ": " + partName(art, state.part) + ")", kGold);
    bool gold = false;
    int y = textTop(state.part);
    for (const auto& line : pageLines(art, state.part, state.page)) {
        drawMarkedText(screen, art.tiny, kTextX, y, line, gold);
        y += kRowStep;
    }
}

} // namespace viceroy
