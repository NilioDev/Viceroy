// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/ui/debug_menu.hpp"

#include "viceroy/game/colony.hpp"

#include <algorithm>

namespace viceroy {

namespace {

constexpr std::uint8_t kBlack = 0x00;
constexpr std::uint8_t kWhite = 0x0F;
constexpr std::uint8_t kGreen = 0x44;
constexpr std::uint8_t kGold = 0x95;

enum class MapEntry { Reveal, Gold, Moves, NextTurn, NextColony, Close };
enum class ColonyEntry { Goods, Colonist, Buildings, Bells, Close };

const std::vector<std::string>& mapEntries() {
    static const std::vector<std::string> entries{
        "Reveal the whole map", "Gold + 1000", "Give every unit its moves back", "End the turn",
        "Open the next colony", "Close"};
    return entries;
}

const std::vector<std::string>& colonyEntries() {
    static const std::vector<std::string> entries{
        "Goods + 100 of each", "Add a free colonist", "Every building one level up",
        "Sons of Liberty to 100%", "Close"};
    return entries;
}

} // namespace

void DebugMenu::toggle(const PlayScreen& play) {
    open_ = !open_;
    selected_ = 0;
    shown_ = entries(play);
}

std::vector<std::string> DebugMenu::entries(const PlayScreen& play) const {
    return play.colonyScreen() ? colonyEntries() : mapEntries();
}

std::string DebugMenu::handle(Command command, PlayScreen& play) {
    const int count = static_cast<int>(shown_.size());
    switch (command) {
    case Command::Up:
    case Command::MoveN:
        selected_ = (selected_ + count - 1) % count;
        break;
    case Command::Down:
    case Command::MoveS:
        selected_ = (selected_ + 1) % count;
        break;
    case Command::Cancel:
        open_ = false;
        break;
    case Command::Confirm:
    case Command::Skip: {
        const std::string result = apply(selected_, play);
        open_ = false;
        return result;
    }
    default:
        break;
    }
    return {};
}

std::string DebugMenu::apply(int entry, PlayScreen& play) {
    Game& game = play.game();
    auto& save = game.state();
    if (play.colonyScreen()) {
        auto& colony = save.colonies[static_cast<std::size_t>(play.colonyScreen()->colony)];
        switch (static_cast<ColonyEntry>(entry)) {
        case ColonyEntry::Goods:
            for (auto& amount : colony.stock) {
                amount = std::min(amount + 100, 30000);
            }
            return "Debug: goods added.";
        case ColonyEntry::Colonist:
            if (colony.population >= kMaxColonists) {
                return "Debug: the colony is full.";
            }
            colony.jobs[static_cast<std::size_t>(colony.population)] = static_cast<std::uint8_t>(job::kStatesman);
            colony.skills[static_cast<std::size_t>(colony.population)] = static_cast<std::uint8_t>(skill::kFreeColonist);
            ++colony.population;
            return "Debug: a colonist joined.";
        case ColonyEntry::Buildings:
            for (int k = 0; k < kBuildingKinds; ++k) {
                const auto kind = static_cast<BuildingKind>(k);
                if (kind == BuildingKind::Unused1 || kind == BuildingKind::Capitol) {
                    continue;
                }
                setBuildingLevel(colony, kind, buildingLevel(colony, kind) + 1);
            }
            return "Debug: buildings improved.";
        case ColonyEntry::Bells:
            colony.rebelDivisor = std::max(colony.rebelDivisor, 1);
            colony.rebelDividend = colony.rebelDivisor;
            return "Debug: everyone is a rebel.";
        case ColonyEntry::Close:
            break;
        }
        return {};
    }

    switch (static_cast<MapEntry>(entry)) {
    case MapEntry::Reveal:
        for (int y = 0; y < save.map.height; ++y) {
            for (int x = 0; x < save.map.width; ++x) {
                auto& sq = save.map.at(x, y);
                sq.seen = static_cast<std::uint8_t>(sq.seen | (1u << (4 + game.human())));
            }
        }
        return "Debug: map revealed.";
    case MapEntry::Gold: {
        auto& gold = save.nations[static_cast<std::size_t>(game.human())].gold;
        gold = std::min(gold + 1000, 2000000000);
        return "Debug: gold added.";
    }
    case MapEntry::Moves:
        for (auto& u : save.units) {
            if (u.nation == game.human()) {
                u.movesUsed = 0;
            }
        }
        return "Debug: moves given back.";
    case MapEntry::NextTurn:
        play.handle(Command::EndTurn);
        return "Debug: turn ended.";
    case MapEntry::NextColony: {
        const int count = static_cast<int>(save.colonies.size());
        const int start = play.lastColony() + 1;
        for (int k = 0; k < count; ++k) {
            const int i = (start + k) % count;
            if (save.colonies[static_cast<std::size_t>(i)].nation == game.human()) {
                play.openColony(i);
                return {};
            }
        }
        return "Debug: no colonies.";
    }
    case MapEntry::Close:
        break;
    }
    return {};
}

void DebugMenu::draw(Screen& screen, const Art& art) const {
    if (!open_) {
        return;
    }
    const int lines = static_cast<int>(shown_.size());
    const Rect box{60, 40, 200, 24 + 9 * lines};
    screen.fill(box, kBlack);
    screen.frame(box, kGold);
    screen.text(art.tiny, box.x + 6, box.y + 5, "Debug (not part of the original game)", {kGold, kNoColour, kNoColour});
    for (int i = 0; i < lines; ++i) {
        const int y = box.y + 17 + 9 * i;
        const bool selected = i == selected_;
        if (selected) {
            screen.fill({box.x + 3, y - 2, box.w - 6, 9}, kGreen);
        }
        screen.text(art.tiny, box.x + 8, y, shown_[static_cast<std::size_t>(i)],
                    {selected ? kBlack : kWhite, kNoColour, kNoColour});
    }
}

} // namespace viceroy
