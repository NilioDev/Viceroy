// SPDX-License-Identifier: GPL-3.0-or-later
//
// The original game's text files (NAMES.TXT, GAME.TXT, PEDIA.TXT, ...):
// sections that start with "@NAME", holding either comma-separated tables
// or message text. See docs/formats/text.md.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace viceroy {

struct TextSection {
    std::string name;
    // Every line after the header up to the next header, as UTF-8 and
    // without line endings. Lines that start with ';' are left out.
    std::vector<std::string> lines;
};

struct TextFile {
    std::vector<TextSection> sections;
    const TextSection* find(std::string_view name) const;
};

// Converts DOS code page 437 to UTF-8.
std::string cp437ToUtf8(std::span<const std::uint8_t> text);

TextFile parseTextFile(std::span<const std::uint8_t> data);

// Splits a table line on commas, after dropping a trailing "; comment".
// Fields are trimmed. Empty lines give no fields.
std::vector<std::string> splitFields(std::string_view line);

// Reads a whole field as a number; nullopt when it is not one.
std::optional<long> parseNumber(std::string_view field);

} // namespace viceroy
