// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/game_files.hpp"

#include <cctype>
#include <system_error>

namespace viceroy {

namespace {

bool equalsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) {
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<std::filesystem::path> findFileNoCase(const std::filesystem::path& folder,
                                                    std::string_view name) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (entry.is_regular_file(ec) && equalsNoCase(entry.path().filename().string(), name)) {
            return entry.path();
        }
    }
    return std::nullopt;
}

bool isGameFolder(const std::filesystem::path& folder) {
    return findFileNoCase(folder, kMarkerFile).has_value();
}

} // namespace viceroy
