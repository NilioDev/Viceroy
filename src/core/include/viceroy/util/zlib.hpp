// SPDX-License-Identifier: GPL-3.0-or-later
//
// Our own small DEFLATE (RFC 1951) decoder and CRC-32, used to read zip
// archives so players can import the game straight from the zip it came in.
#pragma once

#include "viceroy/data/madspack.hpp"
#include "viceroy/data/reader.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace viceroy::zlib {

using ByteView = std::span<const std::uint8_t>;

std::uint32_t crc32(ByteView bytes, std::uint32_t crc = 0);

// Decodes a raw DEFLATE stream. Throws FormatError on corrupt input or when
// the output would grow beyond `maxSize`. `consumed` (if given) receives the
// number of input bytes used.
Bytes inflate(ByteView bytes, std::size_t maxSize, std::size_t* consumed = nullptr);

} // namespace viceroy::zlib
