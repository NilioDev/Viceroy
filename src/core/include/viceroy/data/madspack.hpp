// SPDX-License-Identifier: GPL-3.0-or-later
//
// The container that holds the original game's pictures, sprite sheets and
// fonts, and the "FAB" compression used inside it.
// See docs/formats/madspack.md.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace viceroy {

using Bytes = std::vector<std::uint8_t>;

// Unpacks one FAB-compressed block. The result must be exactly
// `expectedSize` bytes long; anything else throws FormatError.
Bytes fabDecompress(std::span<const std::uint8_t> packed, std::size_t expectedSize);

// True when the data starts with the container signature.
bool isMadspack(std::span<const std::uint8_t> data);

// Returns the unpacked entries of a container, in order.
std::vector<Bytes> readMadspack(std::span<const std::uint8_t> data);

} // namespace viceroy
