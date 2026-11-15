// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/render/pedia_view.hpp"

#include "viceroy/game/game.hpp"
#include "viceroy/render/colony_view.hpp"
#include "viceroy/render/dialog.hpp"

#include <algorithm>
#include <array>

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

// Lines of a paragraph as the original breaks them, worked out from the
// line breaks of 68 pages: a word goes on the line while the line stays
// within 286 pixels, measured with each space one pixel wide, a word ending
// a sentence one pixel narrower and two after it, a line of PEDIA.TXT that
// ends without a space three more before the next word, and the
// paragraph's last word four pixels wider. Seven of the 310 breaks seen do
// not follow this. kBareBreak marks where a line of the file ended without a
// space.
constexpr char kBareBreak = '\v';

std::vector<std::string> wrapPage(const Art& art, const std::string& text) {
    constexpr int kLimit = 286;
    constexpr int kLastWordExtra = 4;
    std::vector<std::string> words;
    std::vector<bool> afterBare;
    std::size_t i = 0;
    while (i < text.size()) {
        bool bare = false;
        while (i < text.size() && (text[i] == ' ' || text[i] == '\n' || text[i] == kBareBreak)) {
            bare = bare || text[i] == kBareBreak;
            ++i;
        }
        const std::size_t end = std::min(text.find_first_of(" \n\v", i), text.size());
        if (end > i) {
            words.push_back(text.substr(i, end - i));
            afterBare.push_back(bare);
        }
        i = end;
    }
    const auto endsSentence = [](const std::string& word) {
        const auto stop = word.find_last_not_of("}");
        return stop != std::string::npos && word[stop] == '.';
    };
    std::vector<std::string> lines;
    std::string line;
    int width = 0;
    int gap = 0;
    for (std::size_t w = 0; w < words.size(); ++w) {
        const int wordWidth = markedWidth(art.tiny, words[w]) - (endsSentence(words[w]) ? 1 : 0) +
                              (w + 1 == words.size() ? kLastWordExtra : 0);
        if (afterBare[w]) {
            gap += 3;
        }
        if (!line.empty() && width + gap + wordWidth > kLimit) {
            lines.push_back(line);
            line.clear();
        }
        if (line.empty()) {
            line = words[w];
            width = wordWidth;
        } else {
            line += " " + words[w];
            width += gap + wordWidth;
        }
        gap = endsSentence(words[w]) ? 2 : 1;
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
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
    bool previousBare = false; // the last line of text ended without a space
    for (const auto& raw : section->lines) {
        std::string line = raw;
        while (!line.empty() && (line.back() == '\r' || line.back() == '\t')) {
            line.pop_back(); // seen: "^" and a tab is a paragraph break too
        }
        const bool bare = !line.empty() && line.back() != ' ';
        while (!line.empty() && line.back() == ' ') {
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
        const bool joinAfterBare = previousBare;
        previousBare = bare;
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
        paragraphs.back() += joinAfterBare ? kBareBreak : '\n';
        paragraphs.back() += line;
    }
    for (std::size_t i = 0; i < paragraphs.size(); ++i) {
        if (i > 1 || (i == 1 && paragraphs[0].empty())) {
            lines.emplace_back(); // the empty line a ^ leaves
        }
        auto wrapped = wrapPage(art, paragraphs[i]);
        for (auto& l : wrapped) {
            lines.push_back(std::move(l));
        }
    }
    return lines;
}

std::string label(const Art& art, int row, const char* fallback) {
    return textLine(art.labelText, "MISC", row, fallback);
}

// The free colonist's page shows every kind of colonist (seen): the
// free colonist, servant, criminal and convert, the experts from the
// farmer to the statesman, and the pioneer, soldier and scout.
constexpr int kColonistPictures[] = {19, 25, 26, 24, 0,  1,  2,  3,  4,  5,  6,  7, 8,
                                     9,  10, 11, 12, 13, 14, 15, 16, 17, 20, 21, 22};
constexpr int kColonistsAcross = 17;
constexpr int kPictureStep = 18;
constexpr int kPictureRowStep = 20;
constexpr int kPictureX = 8;
constexpr int kPictureY = 33;

int iconWidth(const Art& art, int icon) {
    const auto i = static_cast<std::size_t>(icon);
    return i < art.icons.sprites.size() ? art.icons.sprites[i].image.width : 0;
}

// The units that come plain and as experts, and the expert's row of @JOB.
int expertJob(int type) {
    switch (type) {
    case unit_type::kSoldier:
        return 21;
    case unit_type::kPioneer:
        return 20;
    case unit_type::kMissionary:
        return 24;
    case unit_type::kDragoon:
        return 23;
    case unit_type::kScout:
        return 22;
    default:
        return -1;
    }
}

constexpr int kArtillery = 11;
constexpr int kDamagedArtilleryIcon = 65;
constexpr int kExpertIconsAfter = 28; // the plain picture is this many before the expert's

// A unit's page: the unit as on the map, in the player's colour (with its
// expert or damaged form beside it), its name beside them and its numbers
// below, as seen.
void drawUnitPage(Screen& screen, const Art& art, int type, const std::string& name, int nation) {
    if (type < 0 || type >= static_cast<int>(art.rules.units.size())) {
        return;
    }
    const auto& u = art.rules.units[static_cast<std::size_t>(type)];
    int pictures = 1;
    int y = kPictureY;
    std::string title = name;
    if (type == unit_type::kColonist) {
        int i = 0;
        for (const int skill : kColonistPictures) {
            const int icon = colonistIcon(skill);
            // Seen: the narrower people stand as far left as the others.
            const int x = kPictureX + kPictureStep * (i % kColonistsAcross) + (iconWidth(art, icon) < 8 ? -1 : 0);
            drawUnitTypeOnSquare(screen, art, type, nation, x, kPictureY + kPictureRowStep * (i / kColonistsAcross),
                                 icon);
            ++i;
        }
        pictures = (i - 1) % kColonistsAcross + 1;
        y += kPictureRowStep * ((i - 1) / kColonistsAcross);
    } else if (const int job = expertJob(type); job >= 0) {
        const int plain = u.icon - 1 - kExpertIconsAfter;
        // Seen: narrower people stand as far left as the others, and a
        // scout's horse one pixel right of its box's corner.
        const int shift = type == unit_type::kScout ? 1 : 0;
        drawUnitTypeOnSquare(screen, art, type, nation, kPictureX + (iconWidth(art, plain) < 8 ? -1 : 0), y, plain,
                             shift);
        drawUnitTypeOnSquare(screen, art, type, nation,
                             kPictureX + kPictureStep + (iconWidth(art, u.icon - 1) < 8 ? -1 : 0), y, -1, shift);
        pictures = 2;
        const auto j = static_cast<std::size_t>(job);
        title +=
            " (" + label(art, 106, "and") + " " + (j < art.rules.jobs.size() ? art.rules.jobs[j].expertName : "") + ")";
    } else if (type == kArtillery) {
        drawUnitTypeOnSquare(screen, art, type, nation, kPictureX, y);
        drawUnitTypeOnSquare(screen, art, type, nation, kPictureX + kPictureStep, y, kDamagedArtilleryIcon);
        pictures = 2;
        title += " (" + label(art, 106, "and") + " " + label(art, 201, "Damaged") + " " + name + ")";
    } else {
        drawUnitTypeOnSquare(screen, art, type, nation, kPictureX, y);
    }
    say(screen, art, kPictureX + kPictureStep * pictures, y + 6, title, kGreen);

    std::vector<std::string> items{label(art, 179, "Combat") + ": " + std::to_string(u.combat)};
    if (type == unit_type::kSoldier || type == unit_type::kDragoon) {
        items.push_back("(" + label(art, 65, "Veteran") + ": " + std::to_string(u.combat * 3 / 2) + ")");
    } else if (type == kArtillery) {
        // Seen: "(Attack: +2 Damaged: -2)".
        items.push_back("(" + label(art, 180, "Attack") + ": +" + std::to_string(u.attack - u.combat) + " " +
                        label(art, 201, "Damaged") + ": -2)");
    }
    items.push_back(label(art, 182, "Moves") + ": " + std::to_string(u.movement));
    if (u.cargoSlots > 0) {
        items.push_back("(" + label(art, 181, "Cargo Holds") + ": " + std::to_string(u.cargoSlots) + ")");
    }
    int x = kPictureX;
    for (const auto& item : items) {
        say(screen, art, x, y + 24, item, kGreen);
        x += Screen::textWidth(art.tiny, item) + 6;
    }
}

// A goods page: a row for each good of its chain, the expert who makes it,
// one load and six loads, then "Cloth (With Master Weavers)" (seen).
struct GoodsRow {
    int good;
    int job;        // the expert's row of @JOB, -1 for none
    int label = -1; // a MISC row naming the row instead of the good
    int icon = -1;  // a picture other than the good's
};

constexpr int kFishLabel = 177;
constexpr int kFishIcon = 57;
constexpr int kHammers = 16;
constexpr int kHammersIcon = 54;

std::vector<GoodsRow> goodsRows(int good) {
    // The chains as seen: what is grown or dug and what is made of it.
    switch (good) {
    case 0:
        return {{0, 0}, {0, 8, kFishLabel, kFishIcon}}; // food, and fish
    case 1:
    case 9:
        return {{1, 1}, {9, 9}}; // sugar, rum
    case 2:
    case 10:
        return {{2, 2}, {10, 10}}; // tobacco, cigars
    case 3:
    case 11:
        return {{3, 3}, {11, 11}}; // cotton, cloth
    case 4:
    case 12:
        return {{4, 4}, {12, 12}}; // furs, coats
    case 5:
        return {{5, 5}, {kHammers, 13, -1, kHammersIcon}}; // lumber, hammers
    case 6:
    case 14:
    case 15:
        return {{6, 6}, {14, 14}, {15, 15}}; // ore, tools, muskets
    case 7:
        return {{7, 7}};
    default:
        return {{good, -1}}; // horses, trade goods: no expert
    }
}

constexpr int kGoodsIcon = 22;
constexpr int kGoodsRowStep = 20;

// Returns the number of rows.
int drawGoodsPage(Screen& screen, const Art& art, int good) {
    int y = kPictureY;
    const auto rows = goodsRows(good);
    for (const auto& row : rows) {
        const auto j = static_cast<std::size_t>(row.job);
        // Without an expert the row starts where the expert would stand.
        const int dx = row.job >= 0 ? 0 : -14;
        if (row.job >= 0) {
            blitSprite(screen, art.icons, colonistIcon(row.job), 10, y - 2);
        }
        const int icon = row.icon >= 0 ? row.icon : kGoodsIcon + row.good;
        blitSprite(screen, art.icons, icon, 24 + dx, y);
        for (int k = 0; k < 6; ++k) {
            blitSprite(screen, art.icons, icon, 40 + dx + 4 * k, y);
        }
        std::string text =
            row.label >= 0 ? label(art, row.label, "Fish") : firstField(textLine(art.names, "CARGO", row.good, ""));
        if (row.job >= 0 && j < art.rules.jobs.size()) {
            text += " (" + label(art, 178, "With") + " " + art.rules.jobs[j].expertName + ")";
        }
        say(screen, art, 76 + dx, y + 4, text, kGreen);
        y += kGoodsRowStep;
    }
    return static_cast<int>(rows.size());
}

const IndexedImage* sheetImage(const SpriteSheet& sheet, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < sheet.sprites.size()
               ? &sheet.sprites[static_cast<std::size_t>(index)].image
               : nullptr;
}

// A skill's page (seen): the expert and the experts' name, then the
// buildings they work in, one under another, and what they make.
struct SkillPicture {
    int job;
    std::array<int, 3> buildings; // rows of @BUILDING, which are also BUILDING.SS sprites; -1 for none
    int good;                     // row of @CARGO, -1 for none
    int icon;                     // the good's picture
};

constexpr int kExpertIcons = 81;
constexpr int kConvertIcon = 66;
constexpr int kConvert = 27;

constexpr SkillPicture kSkillPictures[] = {
    {0, {-1, -1, -1}, 0, 22},   {1, {-1, -1, -1}, 1, 23},   {2, {-1, -1, -1}, 2, 24},   {3, {-1, -1, -1}, 3, 25},
    {4, {-1, -1, -1}, 4, 26},   {5, {-1, -1, -1}, 5, 27},   {6, {-1, -1, -1}, 6, 28},   {7, {-1, -1, -1}, 7, 29},
    {8, {-1, -1, -1}, 0, 57},   {9, {27, 28, 29}, 9, 31},   {10, {24, 25, 26}, 10, 32}, {11, {21, 22, 23}, 11, 33},
    {12, {32, 33, 34}, 12, 34}, {13, {35, 36, -1}, 16, 54}, {14, {39, 40, 41}, 14, 36}, {15, {3, 4, 5}, 15, 37},
    {16, {37, 38, -1}, 17, 56}, {17, {9, -1, -1}, 18, 62},
};

int drawSkillPage(Screen& screen, const Art& art, const ColonyArt* colonyArt, int job) {
    const auto j = static_cast<std::size_t>(job);
    const std::string expert = j < art.rules.jobs.size() ? art.rules.jobs[j].expertName : std::string();
    const SkillPicture* picture = nullptr;
    for (const auto& p : kSkillPictures) {
        if (p.job == job) {
            picture = &p;
        }
    }
    // The buildings stand from y 22, four rows apart; the expert and the
    // goods face the middle of the first.
    int firstHeight = 0;
    int buildingWidth = 0;
    int bottom = 0;
    if (picture != nullptr && colonyArt != nullptr) {
        int y = 22;
        for (const int b : picture->buildings) {
            if (const auto* image = sheetImage(colonyArt->buildings, b)) {
                if (firstHeight == 0) {
                    firstHeight = image->height;
                    buildingWidth = image->width;
                }
                y += image->height + 4;
                bottom = y;
            }
        }
    }
    const int top = firstHeight > 0 ? 22 + (firstHeight - 16) / 2 + 1 : 33;
    blitSprite(screen, art.icons, job == kConvert ? kConvertIcon : kExpertIcons + job, 10, top);
    const int nameX = 24;
    say(screen, art, nameX, top + 6, expert, kGold);
    int x = nameX + Screen::textWidth(art.tiny, expert) + 24;
    if (picture != nullptr && firstHeight > 0) {
        int y = 22;
        int firstName = 0;
        for (const int b : picture->buildings) {
            const auto* image = sheetImage(colonyArt->buildings, b);
            if (image == nullptr) {
                continue;
            }
            screen.blit(*image, x, y);
            const auto r = static_cast<std::size_t>(b);
            const std::string name = r < art.rules.buildings.size() ? art.rules.buildings[r].name : std::string();
            say(screen, art, x + image->width + 3, y + (image->height - 16) / 2 + 7, name, kGold);
            if (firstName == 0) {
                firstName = Screen::textWidth(art.tiny, name);
            }
            y += image->height + 4;
        }
        x += buildingWidth + 3 + firstName + 24;
    }
    if (picture != nullptr) {
        blitSprite(screen, art.icons, picture->icon, x, top + 2);
        const std::string good = firstField(textLine(art.names, "CARGO", picture->good, ""));
        say(screen, art, x + 16, top + 6, good, kGold);
    }
    return std::max(bottom, top + 16);
}

std::string partName(const Art& art, int part) { return textLine(art.pedia, "PEDIA", part, ""); }

} // namespace

std::vector<std::string> pediaPageLines(const Art& art, int part, int id) { return pageLines(art, part, id); }

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

void drawPedia(Screen& screen, const Art& art, const PediaState& state, int nation, const ColonyArt* colonyArt) {
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
    if (state.part == pedia::kUnit) {
        drawUnitPage(screen, art, state.page, name, nation);
    }
    bool gold = false;
    int y = textTop(state.part);
    if (state.part == pedia::kSkill) {
        y = drawSkillPage(screen, art, colonyArt, state.page) + 4;
    }
    if (state.part == pedia::kCargo) {
        // Seen: the text starts 23 rows below the last row of goods.
        y = kPictureY + kGoodsRowStep * drawGoodsPage(screen, art, state.page) + 10;
    }
    if (state.part == pedia::kUnit && state.page == 0) {
        y += kPictureRowStep; // the second row of colonists
    }
    for (const auto& line : pageLines(art, state.part, state.page)) {
        drawMarkedText(screen, art.tiny, kTextX, y, line, gold);
        y += kRowStep;
    }
}

} // namespace viceroy
