// SPDX-License-Identifier: GPL-3.0-or-later
//
// Starting a game on the map of America, screen by screen as the original
// does it: which map, the difficulty, the nation, the player's name, two
// pages about the nation, the audience with the king and the departure in
// 1492. Measurements are in docs/rules/new-game-screens.md.
#pragma once

#include "viceroy/data/font.hpp"
#include "viceroy/data/image.hpp"
#include "viceroy/game/new_game.hpp"
#include "viceroy/render/dialog.hpp"
#include "viceroy/ui/play.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace viceroy {

struct NewGameArt {
    Picture difficulty; // DIFFICUL.PIK: the five men
    Picture nations;    // NATIONS.PIK: the four flags
    Picture customize;  // CUSTOMIZ.PIK: the twelve pictures of a new world's settings
    Picture panel;      // WOODPANL.PIK: the wood behind the name and the nation pages
    Picture audience;   // KINGLSS1.PIK: the king's room
    SpriteSheet king;   // KING1.SS: the king and his dog
    std::vector<SpriteSheet> banners; // ENGLND1.SS, FRANCE1.SS, SPAIN1.SS, DUTCH1.SS: behind the king
    Font script;        // FONTKING.FF: the writing on the scroll
    std::vector<Picture> departure; // LEVN0001.PIK to LEVN0010.PIK: the harbour at dawn
};

// Loads the pictures. Throws FormatError.
NewGameArt loadNewGameArt(const std::filesystem::path& folder);

class NewGameFlow {
public:
    enum class Step {
        Customize,
        MapChoice,
        MapFile,
        Difficulty,
        Nation,
        Name,
        NationPage1,
        NationPage2,
        Audience,
        Departure,
        Done,
        Cancelled
    };

    // maps: the map files (*.MP) offered after "Map Editor", as named.
    explicit NewGameFlow(const Art& art, std::vector<std::string> maps = {});
    // A game in a new world made with these settings: no map to choose,
    // so it starts at the difficulty.
    static NewGameFlow newWorld(const Art& art, const NewWorldSettings& settings);
    // Customize New World: the four settings are chosen first, then on as
    // for a new world.
    static NewGameFlow customize(const Art& art);
    bool isNewWorld() const { return newWorld_; }
    // The settings of the new world (on the Customize screen all 1 to
    // start with).
    const NewWorldSettings& settings() const { return settings_; }
    int customizeColumn() const { return column_; }

    void handle(Command command);
    void click(int x, int y);
    // Typing the name.
    void type(char c);
    void backspace();
    void tick(std::uint64_t milliseconds);

    Step step() const { return step_; }
    bool typing() const { return step_ == Step::Name; }
    int difficulty() const { return difficulty_; }
    int nation() const { return nation_; }
    const std::string& name() const { return name_; }
    // The map file picked from the map editor's, or empty for the original
    // Americas.
    std::string mapFile() const;
    // What newAmericaGame needs, with the seed left to the caller.
    NewGameSetup setup() const;

    void draw(Screen& screen, const Art& art, const NewGameArt& pictures) const;

private:
    void next();
    void choose(int column, int value);
    void drawDeparture(Screen& screen, const Art& art, const NewGameArt& pictures, int frame) const;

    const Art& art_;
    Step step_ = Step::MapChoice;
    Dialog mapChoice_;
    Dialog mapFiles_;
    int difficulty_ = 0;
    int nation_ = 0;
    std::string name_;
    std::uint64_t elapsed_ = 0; // in the departure
    bool newWorld_ = false;
    NewWorldSettings settings_;
    int column_ = 0; // the setting picked on the Customize screen
};

} // namespace viceroy
