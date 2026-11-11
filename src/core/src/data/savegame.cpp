// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/data/savegame.hpp"

#include "viceroy/data/reader.hpp"

#include <cstring>

namespace viceroy {

namespace {

constexpr char kSignature[] = "COLONIZE";
constexpr std::size_t kSignatureSize = sizeof(kSignature); // includes the 0 byte

// Sizes of the blocks before the map, as worked out in
// docs/formats/savegame.md.
constexpr std::size_t kHeaderSize = 158;
constexpr std::size_t kPlayersSize = 4 * 52 + 24;
constexpr std::size_t kColonySize = 202;
constexpr std::size_t kUnitSize = 28;
constexpr std::size_t kDwellingSize = 18;
// Nations, the eight tribes and other game-wide data: fixed size.
constexpr std::size_t kFixedAfterUnits = 4 * 316 + 8 * 78 + 727;

constexpr int kMaxMapSide = 256;
constexpr int kMapLayers = 4;

} // namespace

SaveGame readSaveGame(std::span<const std::uint8_t> file) {
    if (file.size() < kHeaderSize || std::memcmp(file.data(), kSignature, kSignatureSize) != 0) {
        throw FormatError("not a savegame");
    }
    Reader in(file);
    SaveGame save;

    in.seek(12);
    save.map.width = in.u16();
    save.map.height = in.u16();
    if (save.map.width < 1 || save.map.height < 1 || save.map.width > kMaxMapSide ||
        save.map.height > kMaxMapSide) {
        throw FormatError("savegame with impossible map size");
    }
    in.seek(26);
    save.year = in.u16();
    save.season = in.u16();
    save.turn = in.u16();
    in.seek(42);
    save.tribeCount = in.u16();
    save.unitCount = in.u16();
    save.colonyCount = in.u16();
    in.seek(54);
    save.difficulty = in.u8();

    const std::size_t mapStart = kHeaderSize + kPlayersSize + static_cast<std::size_t>(save.colonyCount) * kColonySize +
                                 static_cast<std::size_t>(save.unitCount) * kUnitSize +
                                 static_cast<std::size_t>(save.tribeCount) * kDwellingSize + kFixedAfterUnits;
    const std::size_t layer = static_cast<std::size_t>(save.map.width) * static_cast<std::size_t>(save.map.height);
    in.seek(mapStart); // throws when the counts point past the end
    const auto tiles = in.bytes(layer);
    const auto mask = in.bytes(layer);
    const auto path = in.bytes(layer);
    const auto seen = in.bytes(layer);
    static_assert(kMapLayers == 4);

    save.map.squares.resize(layer);
    for (std::size_t i = 0; i < layer; ++i) {
        auto& sq = save.map.squares[i];
        sq.terrain = tiles[i] & 0x1Fu;
        sq.relief = static_cast<std::uint8_t>(tiles[i] >> 5);
        sq.flags = mask[i];
        sq.region = path[i];
        sq.seen = seen[i];
    }
    save.raw.assign(file.begin(), file.end());
    return save;
}

} // namespace viceroy
