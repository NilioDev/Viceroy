// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/data/savegame.hpp"

#include "viceroy/data/reader.hpp"

#include <algorithm>
#include <cstring>

namespace viceroy {

namespace {

constexpr char kSignature[] = "COLONIZE";
constexpr std::size_t kSignatureSize = sizeof(kSignature); // includes the 0 byte

// Sizes of the blocks, as worked out in docs/formats/savegame.md.
constexpr std::size_t kHeaderSize = 158;
constexpr std::size_t kPlayerSize = 52;
constexpr std::size_t kOtherSize = 24;
constexpr std::size_t kColonySize = 202;
constexpr std::size_t kUnitSize = 28;
constexpr std::size_t kNationSize = 316;
constexpr std::size_t kSettlementSize = 18;
constexpr std::size_t kTribesAndMoreSize = 8 * 78 + 727;
constexpr std::size_t kTailSize = 1502;
constexpr std::size_t kResourceSeedAt = 612; // in the block after the map

constexpr int kMaxMapSide = 256;
constexpr int kMaxRecords = 4096;

template <std::size_t N>
std::array<std::uint8_t, N> take(Reader& in) {
    std::array<std::uint8_t, N> out{};
    const auto bytes = in.bytes(N);
    std::copy(bytes.begin(), bytes.end(), out.begin());
    return out;
}

std::string text(const std::uint8_t* p, std::size_t size) {
    std::string s;
    for (std::size_t i = 0; i < size && p[i] != 0; ++i) {
        s.push_back(static_cast<char>(p[i]));
    }
    return s;
}

// Leaves the bytes alone when the text is unchanged: the original keeps
// leftovers after the 0 byte, and we write saves back byte for byte.
void putText(std::uint8_t* p, std::size_t size, const std::string& s) {
    if (text(p, size) == s) {
        return;
    }
    std::memset(p, 0, size);
    std::memcpy(p, s.data(), std::min(s.size(), size - 1));
}

int u16(const std::uint8_t* p) {
    return p[0] | (p[1] << 8);
}

int s16(const std::uint8_t* p) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(u16(p)));
}

void put16(std::uint8_t* p, int v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}

void put32(std::uint8_t* p, int v) {
    const auto u = static_cast<std::uint32_t>(v);
    for (int i = 0; i < 4; ++i) {
        p[i] = static_cast<std::uint8_t>(u >> (8 * i));
    }
}

int s32(const std::uint8_t* p) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
                                     (static_cast<std::uint32_t>(p[2]) << 16) |
                                     (static_cast<std::uint32_t>(p[3]) << 24));
}

void append(Bytes& out, std::span<const std::uint8_t> bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}

} // namespace

SaveGame readSaveGame(std::span<const std::uint8_t> file) {
    if (file.size() < kHeaderSize || std::memcmp(file.data(), kSignature, kSignatureSize) != 0) {
        throw FormatError("not a savegame");
    }
    Reader in(file);
    SaveGame save;
    save.header = take<kHeaderSize>(in);
    const auto* h = save.header.data();
    save.map.width = u16(h + 12);
    save.map.height = u16(h + 14);
    if (save.map.width < 1 || save.map.height < 1 || save.map.width > kMaxMapSide ||
        save.map.height > kMaxMapSide) {
        throw FormatError("savegame with impossible map size");
    }
    save.year = u16(h + 26);
    save.season = u16(h + 28);
    save.turn = u16(h + 30);
    save.activeUnit = s16(h + 34);
    save.humanNation = u16(h + 40) & 3;
    const int settlementCount = u16(h + 42);
    const int unitCount = u16(h + 44);
    const int colonyCount = u16(h + 46);
    save.difficulty = h[54];
    if (settlementCount > kMaxRecords || unitCount > kMaxRecords || colonyCount > kMaxRecords) {
        throw FormatError("savegame with impossible counts");
    }

    for (auto& player : save.players) {
        player.raw = take<kPlayerSize>(in);
        player.name = text(player.raw.data(), 24);
        player.country = text(player.raw.data() + 24, 24);
        player.control = player.raw[49];
    }
    save.other = take<kOtherSize>(in);

    save.colonies.resize(static_cast<std::size_t>(colonyCount));
    for (auto& colony : save.colonies) {
        colony.raw = take<kColonySize>(in);
        colony.x = colony.raw[0];
        colony.y = colony.raw[1];
        colony.name = text(colony.raw.data() + 2, 24);
        colony.nation = colony.raw[26];
        colony.population = colony.raw[31];
        const auto* r = colony.raw.data();
        for (std::size_t i = 0; i < kMaxColonists; ++i) {
            colony.jobs[i] = r[32 + i];
            colony.skills[i] = r[64 + i];
        }
        for (std::size_t i = 0; i < 8; ++i) {
            colony.squares[i] = static_cast<std::int8_t>(r[112 + i]);
        }
        colony.buildings = 0;
        for (int i = 5; i >= 0; --i) {
            colony.buildings = (colony.buildings << 8) | r[132 + i];
        }
        colony.hammers = u16(r + 146);
        colony.construction = r[148] == 0xFF ? -1 : r[148];
        for (std::size_t i = 0; i < kGoodsKinds; ++i) {
            colony.stock[i] = s16(r + 154 + 2 * i);
        }
        colony.rebelDividend = s32(r + 194);
        colony.rebelDivisor = s32(r + 198);
    }

    save.units.resize(static_cast<std::size_t>(unitCount));
    for (auto& unit : save.units) {
        unit.raw = take<kUnitSize>(in);
        const auto* r = unit.raw.data();
        unit.x = r[0];
        unit.y = r[1];
        unit.type = r[2];
        unit.nation = r[3] & 0x0F;
        unit.movesUsed = r[5];
        unit.orders = r[8];
        unit.holds = r[12];
        unit.tools = r[21];
        unit.workTurns = r[22];
        unit.profession = r[23];
        unit.nextInTransport = s16(r + 24);
        unit.prevInTransport = s16(r + 26);
    }

    for (auto& nation : save.nations) {
        nation.raw = take<kNationSize>(in);
        nation.taxRate = nation.raw[1];
        nation.gold = s32(nation.raw.data() + 42);
        for (std::size_t g = 0; g < kGoodsKinds; ++g) {
            nation.prices[g] = nation.raw[76 + g];
        }
        for (std::size_t g = 0; g < kGoodsKinds; ++g) {
            nation.volumes[g] = s16(nation.raw.data() + 92 + 2 * g);
            nation.tradeGold[g] = s32(nation.raw.data() + 124 + 4 * g);
            nation.tonsTraded[g] = s32(nation.raw.data() + 188 + 4 * g);
            nation.tonsTraded2[g] = s32(nation.raw.data() + 252 + 4 * g);
        }
        for (std::size_t r = 0; r < nation.recruits.size(); ++r) {
            nation.recruits[r] = nation.raw[2 + r];
        }
        nation.recruitCount = nation.raw[6];
        nation.artilleryBought = nation.raw[30];
        nation.crosses = u16(nation.raw.data() + 46);
        nation.crossesNeeded = u16(nation.raw.data() + 48);
    }

    save.settlements.resize(static_cast<std::size_t>(settlementCount));
    for (auto& settlement : save.settlements) {
        settlement.raw = take<kSettlementSize>(in);
        settlement.x = settlement.raw[0];
        settlement.y = settlement.raw[1];
        settlement.nation = settlement.raw[2];
        settlement.population = settlement.raw[4];
    }

    const auto more = in.bytes(kTribesAndMoreSize);
    save.tribesAndMore.assign(more.begin(), more.end());

    const std::size_t layer = static_cast<std::size_t>(save.map.width) * static_cast<std::size_t>(save.map.height);
    for (auto& l : save.layers) {
        const auto bytes = in.bytes(layer);
        l.assign(bytes.begin(), bytes.end());
    }
    save.map.squares.resize(layer);
    for (std::size_t i = 0; i < layer; ++i) {
        auto& sq = save.map.squares[i];
        sq.terrain = save.layers[0][i] & 0x1Fu;
        sq.relief = static_cast<std::uint8_t>(save.layers[0][i] >> 5);
        sq.flags = save.layers[1][i];
        sq.region = save.layers[2][i];
        sq.seen = save.layers[3][i];
    }
    const auto rest = in.bytes(in.remaining());
    if (rest.size() < kTailSize) {
        throw FormatError("savegame ends early");
    }
    save.tail.assign(rest.begin(), rest.end());
    save.map.resourceSeed = save.tail[kResourceSeedAt];
    return save;
}

Bytes writeSaveGame(const SaveGame& save) {
    Bytes out;
    auto header = save.header;
    auto* h = header.data();
    put16(h + 12, save.map.width);
    put16(h + 14, save.map.height);
    put16(h + 26, save.year);
    put16(h + 28, save.season);
    put16(h + 30, save.turn);
    put16(h + 34, save.activeUnit);
    put16(h + 42, static_cast<int>(save.settlements.size()));
    put16(h + 44, static_cast<int>(save.units.size()));
    put16(h + 46, static_cast<int>(save.colonies.size()));
    h[54] = static_cast<std::uint8_t>(save.difficulty);
    append(out, header);

    for (const auto& player : save.players) {
        auto raw = player.raw;
        putText(raw.data(), 24, player.name);
        putText(raw.data() + 24, 24, player.country);
        raw[49] = static_cast<std::uint8_t>(player.control);
        append(out, raw);
    }
    append(out, save.other);

    for (const auto& colony : save.colonies) {
        auto raw = colony.raw;
        raw[0] = static_cast<std::uint8_t>(colony.x);
        raw[1] = static_cast<std::uint8_t>(colony.y);
        putText(raw.data() + 2, 24, colony.name);
        raw[26] = static_cast<std::uint8_t>(colony.nation);
        raw[31] = static_cast<std::uint8_t>(colony.population);
        for (std::size_t i = 0; i < kMaxColonists; ++i) {
            raw[32 + i] = colony.jobs[i];
            raw[64 + i] = colony.skills[i];
        }
        for (std::size_t i = 0; i < 8; ++i) {
            raw[112 + i] = static_cast<std::uint8_t>(colony.squares[i]);
        }
        for (std::size_t i = 0; i < 6; ++i) {
            raw[132 + i] = static_cast<std::uint8_t>(colony.buildings >> (8 * i));
        }
        put16(raw.data() + 146, colony.hammers);
        raw[148] = static_cast<std::uint8_t>(colony.construction < 0 ? 0xFF : colony.construction);
        for (std::size_t i = 0; i < kGoodsKinds; ++i) {
            put16(raw.data() + 154 + 2 * i, colony.stock[i]);
        }
        put32(raw.data() + 194, colony.rebelDividend);
        put32(raw.data() + 198, colony.rebelDivisor);
        append(out, raw);
    }
    for (const auto& unit : save.units) {
        auto raw = unit.raw;
        raw[0] = static_cast<std::uint8_t>(unit.x);
        raw[1] = static_cast<std::uint8_t>(unit.y);
        raw[2] = static_cast<std::uint8_t>(unit.type);
        raw[3] = static_cast<std::uint8_t>((raw[3] & 0xF0) | (unit.nation & 0x0F));
        raw[5] = static_cast<std::uint8_t>(unit.movesUsed);
        raw[8] = static_cast<std::uint8_t>(unit.orders);
        raw[12] = static_cast<std::uint8_t>(unit.holds);
        raw[21] = static_cast<std::uint8_t>(std::clamp(unit.tools, 0, 255));
        raw[22] = static_cast<std::uint8_t>(std::clamp(unit.workTurns, 0, 255));
        raw[23] = static_cast<std::uint8_t>(unit.profession);
        put16(raw.data() + 24, unit.nextInTransport);
        put16(raw.data() + 26, unit.prevInTransport);
        append(out, raw);
    }
    for (const auto& nation : save.nations) {
        auto raw = nation.raw;
        raw[1] = static_cast<std::uint8_t>(nation.taxRate);
        put32(raw.data() + 42, nation.gold);
        for (std::size_t g = 0; g < kGoodsKinds; ++g) {
            raw[76 + g] = static_cast<std::uint8_t>(std::clamp(nation.prices[g], 0, 255));
        }
        for (std::size_t g = 0; g < kGoodsKinds; ++g) {
            put16(raw.data() + 92 + 2 * g, std::clamp(nation.volumes[g], -32768, 32767));
            put32(raw.data() + 124 + 4 * g, nation.tradeGold[g]);
            put32(raw.data() + 188 + 4 * g, nation.tonsTraded[g]);
            put32(raw.data() + 252 + 4 * g, nation.tonsTraded2[g]);
        }
        for (std::size_t r = 0; r < nation.recruits.size(); ++r) {
            raw[2 + r] = static_cast<std::uint8_t>(nation.recruits[r]);
        }
        raw[6] = static_cast<std::uint8_t>(std::clamp(nation.recruitCount, 0, 255));
        raw[30] = static_cast<std::uint8_t>(std::clamp(nation.artilleryBought, 0, 255));
        put16(raw.data() + 46, nation.crosses);
        put16(raw.data() + 48, nation.crossesNeeded);
        append(out, raw);
    }
    for (const auto& settlement : save.settlements) {
        auto raw = settlement.raw;
        raw[0] = static_cast<std::uint8_t>(settlement.x);
        raw[1] = static_cast<std::uint8_t>(settlement.y);
        raw[2] = static_cast<std::uint8_t>(settlement.nation);
        raw[4] = static_cast<std::uint8_t>(settlement.population);
        append(out, raw);
    }
    append(out, save.tribesAndMore);

    const std::size_t layer = save.map.squares.size();
    std::array<Bytes, 4> layers;
    for (std::size_t l = 0; l < 4; ++l) {
        layers[l].resize(layer);
    }
    for (std::size_t i = 0; i < layer; ++i) {
        const auto& sq = save.map.squares[i];
        layers[0][i] = static_cast<std::uint8_t>((sq.relief << 5) | (sq.terrain & 0x1F));
        layers[1][i] = sq.flags;
        layers[2][i] = sq.region;
        layers[3][i] = sq.seen;
    }
    for (const auto& l : layers) {
        append(out, l);
    }
    append(out, save.tail);
    return out;
}

} // namespace viceroy
