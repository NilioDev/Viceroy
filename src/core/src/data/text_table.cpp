// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/data/text_table.hpp"

#include <array>
#include <charconv>

namespace viceroy {

namespace {

// Unicode code points of the bytes 0x80 to 0xFF in code page 437.
constexpr std::array<char32_t, 128> kCp437High = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
};

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string_view trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(" \t");
    return s.substr(first, last - first + 1);
}

bool isNameChar(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// "@NAME", optionally followed by spaces and a comment. Lower-case
// "@width=..." and "@@..." lines are directives or text inside a section.
std::optional<std::string> sectionHeader(std::string_view line) {
    if (line.size() < 2 || line[0] != '@' || !isNameChar(line[1])) {
        return std::nullopt;
    }
    std::size_t end = 1;
    while (end < line.size() && isNameChar(line[end])) {
        ++end;
    }
    const auto rest = trim(line.substr(end));
    if (!rest.empty() && rest[0] != ';') {
        return std::nullopt;
    }
    return std::string(line.substr(1, end - 1));
}

} // namespace

const TextSection* TextFile::find(std::string_view name) const {
    for (const auto& section : sections) {
        if (section.name == name) {
            return &section;
        }
    }
    return nullptr;
}

std::string cp437ToUtf8(std::span<const std::uint8_t> text) {
    std::string out;
    out.reserve(text.size());
    for (const auto byte : text) {
        appendUtf8(out, byte < 0x80 ? char32_t{byte} : kCp437High[byte - 0x80u]);
    }
    return out;
}

TextFile parseTextFile(std::span<const std::uint8_t> data) {
    TextFile file;
    TextSection* current = nullptr;
    std::size_t start = 0;
    while (start < data.size()) {
        std::size_t end = start;
        while (end < data.size() && data[end] != '\n') {
            ++end;
        }
        std::size_t lineEnd = end;
        if (lineEnd > start && data[lineEnd - 1] == '\r') {
            --lineEnd;
        }
        const std::string line = cp437ToUtf8(data.subspan(start, lineEnd - start));
        start = end + 1;

        if (!line.empty() && line[0] == ';') {
            continue;
        }
        if (auto name = sectionHeader(line)) {
            file.sections.push_back({std::move(*name), {}});
            current = &file.sections.back();
            continue;
        }
        if (current != nullptr) {
            current->lines.push_back(line);
        }
    }
    return file;
}

std::vector<std::string> splitFields(std::string_view line) {
    const auto comment = line.find(';');
    if (comment != std::string_view::npos) {
        line = line.substr(0, comment);
    }
    std::vector<std::string> fields;
    if (trim(line).empty()) {
        return fields;
    }
    for (;;) {
        const auto comma = line.find(',');
        fields.emplace_back(trim(line.substr(0, comma)));
        if (comma == std::string_view::npos) {
            break;
        }
        line = line.substr(comma + 1);
    }
    return fields;
}

std::optional<long> parseNumber(std::string_view field) {
    field = trim(field);
    long value = 0;
    const auto* first = field.data();
    const auto* last = field.data() + field.size();
    const auto result = std::from_chars(first, last, value);
    if (field.empty() || result.ec != std::errc{} || result.ptr != last) {
        return std::nullopt;
    }
    return value;
}

} // namespace viceroy
