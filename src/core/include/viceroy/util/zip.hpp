// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reading zip archives (stored and deflated entries), so players can import
// the game straight from the zip file it came in.
#pragma once

#include "viceroy/data/madspack.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace viceroy::zip {

struct Entry {
    std::string name; // path inside the archive, with '/'
    Bytes content;
};

// The largest single file we unpack. The biggest original file is well under
// a megabyte; this only stops hostile archives.
inline constexpr std::size_t kMaxEntrySize = 64u * 1024u * 1024u;

// Reads every file entry (folders are skipped). Throws FormatError for
// archives we cannot read, such as encrypted, multi-part or damaged ones.
std::vector<Entry> read(std::span<const std::uint8_t> archive);

} // namespace viceroy::zip
