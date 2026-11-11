// SPDX-License-Identifier: GPL-3.0-or-later
//
// The player's game library: the one-time import of their own copy of the
// original game into Viceroy's own folder, so it never has to be found again.
//
//   <library>/base/pack.json      manifest of the base pack
//   <library>/base/original/      the original files, upper-case names
//   <library>/saves/              savegames (the ones that came with the game
//                                 are copied here once, never overwritten)
//
// See docs/formats/pack.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace viceroy::pack {

// Something the player should be told, in plain words, when an import fails.
class ImportError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct ImportedFile {
    std::string name; // upper-case DOS name, e.g. "TERRAIN.SS"
    std::uint64_t size = 0;
    std::uint32_t crc32 = 0;
};

struct ImportResult {
    std::vector<ImportedFile> files;
    std::size_t savesCopied = 0;
};

// The files an import checks by reading them with our own readers.
const std::vector<std::string>& requiredFiles();

// Folder holding the imported original files.
std::filesystem::path baseFolder(const std::filesystem::path& library);
std::filesystem::path savesFolder(const std::filesystem::path& library);

// True when a complete import exists in the library.
bool hasBasePack(const std::filesystem::path& library);

// Imports the game from `source`: a folder (the game may sit in a subfolder)
// or a .zip file. Every required file is read and checked first; the library
// is only changed when everything is fine, and then replaces an earlier
// import. Throws ImportError with a message for the player.
ImportResult importGame(const std::filesystem::path& source, const std::filesystem::path& library);

// True when a name is safe to store: a plain DOS 8.3 name.
bool isSafeName(const std::string& name);

} // namespace viceroy::pack
