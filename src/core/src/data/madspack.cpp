// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/data/madspack.hpp"

#include "viceroy/data/reader.hpp"

#include <algorithm>
#include <cstring>

namespace viceroy {

namespace {

constexpr char kSignature[] = "MADSPACK 2.0\x1a";
constexpr std::size_t kSignatureSize = sizeof(kSignature) - 1;
constexpr std::size_t kHeaderSize = 0xB0;
constexpr std::size_t kMaxEntries = 16;
constexpr std::size_t kEntryTableOffset = 16;
// Nothing in the original game unpacks to more than a few hundred kilobytes.
constexpr std::size_t kMaxEntrySize = 4u * 1024u * 1024u;

// Hands out the control bits of a FAB stream, lowest bit first. A new
// 16-bit word is fetched when the current one runs out; the last bit of the
// old word is carried into the new one.
class BitSource {
public:
    explicit BitSource(Reader& in) : in_(in), bits_(in.u16()) {}

    unsigned next() {
        if (--left_ == 0) {
            bits_ = static_cast<std::uint32_t>(in_.u16() << 1) | (bits_ & 1u);
            left_ = 16;
        }
        const unsigned bit = bits_ & 1u;
        bits_ >>= 1;
        return bit;
    }

private:
    Reader& in_;
    std::uint32_t bits_;
    int left_ = 16;
};

} // namespace

Bytes fabDecompress(std::span<const std::uint8_t> packed, std::size_t expectedSize) {
    Reader in(packed);
    const auto magic = in.bytes(3);
    if (magic[0] != 'F' || magic[1] != 'A' || magic[2] != 'B') {
        throw FormatError("FAB block without signature");
    }
    const unsigned shift = in.u8();
    if (shift < 10 || shift > 13) {
        throw FormatError("FAB block with unsupported shift");
    }
    const unsigned lengthBits = 16 - shift;
    const unsigned lengthMask = (1u << lengthBits) - 1u;
    const unsigned offsetHighFill = (0xFFu << (shift - 8)) & 0xFFu;

    Bytes out;
    out.reserve(expectedSize);
    BitSource bits(in);

    for (;;) {
        if (bits.next() == 1) {
            out.push_back(in.u8());
        } else {
            std::size_t length = 0;
            std::size_t distance = 0;
            if (bits.next() == 0) {
                // Short copy: 2 to 5 bytes from up to 256 bytes back.
                const unsigned highBit = bits.next(); // two reads, in this order
                const unsigned lowBit = bits.next();
                length = ((highBit << 1) | lowBit) + 2u;
                distance = 256u - in.u8();
            } else {
                // Long copy: offset and length share two bytes.
                const unsigned low = in.u8();
                const unsigned high = in.u8();
                const unsigned offset = ((((high >> lengthBits) | offsetHighFill) & 0xFFu) << 8) | low;
                distance = 0x10000u - offset;
                length = high & lengthMask;
                if (length == 0) {
                    length = in.u8();
                    if (length == 0) {
                        break; // end of stream
                    }
                    if (length == 1) {
                        continue; // no-op marker
                    }
                    length += 1;
                } else {
                    length += 2;
                }
            }
            if (distance == 0 || distance > out.size()) {
                throw FormatError("FAB copy reaches before start of output");
            }
            if (out.size() + length > expectedSize) {
                throw FormatError("FAB block unpacks to more than expected");
            }
            // Byte by byte on purpose: copies may overlap their own output.
            for (std::size_t i = 0; i < length; ++i) {
                out.push_back(out[out.size() - distance]);
            }
        }
        if (out.size() > expectedSize) {
            throw FormatError("FAB block unpacks to more than expected");
        }
    }
    if (out.size() != expectedSize) {
        throw FormatError("FAB block unpacks to an unexpected size");
    }
    return out;
}

bool isMadspack(std::span<const std::uint8_t> data) {
    return data.size() >= kSignatureSize && std::memcmp(data.data(), kSignature, kSignatureSize) == 0;
}

std::vector<Bytes> readMadspack(std::span<const std::uint8_t> data) {
    if (!isMadspack(data) || data.size() < kHeaderSize) {
        throw FormatError("not a MADSPACK container");
    }
    Reader header(data);
    header.seek(14);
    const std::size_t count = header.u16();
    if (count > kMaxEntries) {
        throw FormatError("MADSPACK container with too many entries");
    }

    std::vector<Bytes> entries;
    entries.reserve(count);
    Reader body(data);
    body.seek(kHeaderSize);
    header.seek(kEntryTableOffset);
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint16_t flags = header.u16();
        const std::size_t size = header.u32();
        const std::size_t packedSize = header.u32();
        if (size > kMaxEntrySize) {
            throw FormatError("MADSPACK entry too large");
        }
        const auto packed = body.bytes(packedSize);
        if ((flags & 1u) != 0) {
            entries.push_back(fabDecompress(packed, size));
        } else {
            if (packedSize != size) {
                throw FormatError("stored MADSPACK entry with wrong size");
            }
            entries.emplace_back(packed.begin(), packed.end());
        }
    }
    return entries;
}

} // namespace viceroy
