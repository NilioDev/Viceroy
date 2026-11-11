// SPDX-License-Identifier: GPL-3.0-or-later
//
// Finding the player's own copy of the original game files.
#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace viceroy {

// A file the engine needs from the original DOS release.
inline constexpr std::string_view kMarkerFile = "VICEROY.PAL";

// Looks for a file in a folder without regard to case, because copies made
// from DOS disks have upper-case names and other systems often lower-case
// them. Returns the real path when it is found.
std::optional<std::filesystem::path> findFileNoCase(const std::filesystem::path& folder,
                                                    std::string_view name);

// True when the folder looks like an installation of the original game.
bool isGameFolder(const std::filesystem::path& folder);

} // namespace viceroy
