// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/rules/ruleset.hpp"

#include "viceroy/data/reader.hpp"

#include <string_view>

namespace viceroy {

namespace {

// Values in the tables are small; anything outside this range means the
// file is damaged or was edited wrongly.
constexpr long kMinValue = -1000;
constexpr long kMaxValue = 100000;

const TextSection& section(const TextFile& file, std::string_view name) {
    const auto* found = file.find(name);
    if (found == nullptr) {
        throw FormatError("NAMES.TXT has no @" + std::string(name) + " section");
    }
    return *found;
}

// The non-empty rows of a section, split into fields. A table ends at the
// first empty line, except where noted.
std::vector<std::vector<std::string>> rows(const TextSection& s, bool stopAtBlank = true) {
    std::vector<std::vector<std::string>> out;
    for (const auto& line : s.lines) {
        auto fields = splitFields(line);
        if (fields.empty()) {
            if (stopAtBlank && !out.empty()) {
                break;
            }
            continue;
        }
        out.push_back(std::move(fields));
    }
    return out;
}

class Row {
public:
    Row(std::string_view table, const std::vector<std::string>& fields) : table_(table), fields_(fields) {}

    void expect(std::size_t count) const {
        if (fields_.size() != count) {
            fail("has " + std::to_string(fields_.size()) + " values, expected " + std::to_string(count));
        }
    }

    const std::string& text(std::size_t i) const { return fields_.at(i); }

    int number(std::size_t i) const {
        const auto value = parseNumber(fields_.at(i));
        if (!value || *value < kMinValue || *value > kMaxValue) {
            fail("has a bad number '" + fields_.at(i) + "'");
        }
        return static_cast<int>(*value);
    }

    std::uint8_t bits(std::size_t i) const {
        const auto& f = fields_.at(i);
        if (f.size() != 8 || f.find_first_not_of("01") != std::string::npos) {
            fail("has bad flags '" + f + "'");
        }
        std::uint8_t v = 0;
        for (const char c : f) {
            v = static_cast<std::uint8_t>((v << 1) | (c == '1' ? 1 : 0));
        }
        return v;
    }

private:
    [[noreturn]] void fail(const std::string& what) const {
        throw FormatError("@" + std::string(table_) + " row '" + (fields_.empty() ? "" : fields_[0]) + "' " + what);
    }

    std::string_view table_;
    const std::vector<std::string>& fields_;
};

void readTerrain(const TextFile& file, std::string_view name, TerrainType::Group group, Ruleset& rules) {
    for (const auto& fields : rows(section(file, name))) {
        Row row(name, fields);
        row.expect(1 + 4 + kSquareWorkCount);
        TerrainType t;
        t.name = row.text(0);
        t.group = group;
        t.movementCost = row.number(1);
        t.defenseBonus = row.number(2);
        t.improvementTurns = row.number(3);
        t.value = row.number(4);
        for (std::size_t i = 0; i < kSquareWorkCount; ++i) {
            t.yield[i] = row.number(5 + i);
        }
        rules.terrain.push_back(std::move(t));
    }
}

} // namespace

Ruleset readRuleset(const TextFile& names) {
    Ruleset rules;
    readTerrain(names, "UNFORESTED", TerrainType::Group::Unforested, rules);
    readTerrain(names, "FORESTED", TerrainType::Group::Forested, rules);
    readTerrain(names, "OTHER", TerrainType::Group::Other, rules);

    for (const auto& fields : rows(section(names, "BUILDING"))) {
        Row row("BUILDING", fields);
        row.expect(6);
        rules.buildings.push_back(
            {row.text(0), row.number(1), row.number(2) * 10, row.number(3), row.number(4), row.number(5)});
    }

    for (const auto& fields : rows(section(names, "JOB"))) {
        Row row("JOB", fields);
        row.expect(4);
        rules.jobs.push_back({row.text(0), row.text(1), row.number(2), row.number(3)});
    }

    for (const auto& fields : rows(section(names, "CARGO"))) {
        Row row("CARGO", fields);
        CargoType c;
        c.name = row.text(0);
        if (fields.size() > 1) {
            row.expect(10);
            c.tradable = true;
            c.startPriceLow = row.number(1);
            c.startPriceHigh = row.number(2);
            c.driftLow = row.number(3);
            c.driftHigh = row.number(4);
            c.burden = row.number(5);
            c.rise = row.number(6);
            c.fall = row.number(7);
            c.attrition = row.number(8);
            c.volatility = row.number(9);
        }
        rules.cargo.push_back(std::move(c));
    }

    for (const auto& fields : rows(section(names, "UNIT"))) {
        Row row("UNIT", fields);
        row.expect(12);
        rules.units.push_back({row.text(0), row.number(1), row.number(2), row.number(3), row.number(4),
                               row.number(5), row.number(6), row.number(7), row.number(8), row.number(9),
                               row.number(10), row.bits(11)});
    }

    for (const auto& fields : rows(section(names, "CLASS"))) {
        Row row("CLASS", fields);
        row.expect(2);
        rules.classes.push_back({row.text(0), row.number(1)});
    }

    for (const auto& fields : rows(section(names, "FATHERS"))) {
        Row row("FATHERS", fields);
        row.expect(5);
        rules.fathers.push_back({row.text(0), row.number(1), {row.number(2), row.number(3), row.number(4)}});
    }

    // Only the first block lists the playable tribes; the rows after it are
    // spare names with two values each.
    for (const auto& fields : rows(section(names, "TRIBES"))) {
        Row row("TRIBES", fields);
        row.expect(5);
        rules.tribes.push_back({row.text(0), row.text(1), row.text(2), row.number(3), row.number(4)});
    }

    if (const auto* republics = names.find("INDEPENDENT")) {
        for (std::string line : republics->lines) {
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
                line.pop_back();
            }
            if (line.empty()) {
                if (!rules.independent.empty()) {
                    break;
                }
                continue;
            }
            rules.independent.push_back(line);
        }
    }

    if (rules.terrain.empty() || rules.buildings.empty() || rules.jobs.empty() || rules.cargo.empty() ||
        rules.units.empty() || rules.classes.empty() || rules.fathers.empty() || rules.tribes.empty()) {
        throw FormatError("NAMES.TXT has an empty rule table");
    }
    return rules;
}

} // namespace viceroy
