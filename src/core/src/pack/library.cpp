// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/pack/library.hpp"

#include "viceroy/data/font.hpp"
#include "viceroy/data/image.hpp"
#include "viceroy/data/text_table.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/rules/ruleset.hpp"
#include "viceroy/util/zip.hpp"
#include "viceroy/util/zlib.hpp"

#include <algorithm>
#include <cctype>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <cstdio>
#include <optional>
#include <system_error>

namespace viceroy::pack {

namespace fs = std::filesystem;

namespace {

// Upper bounds that only hostile input reaches. The original game is about
// 20 MB in some 300 files.
constexpr std::uint64_t kMaxTotalSize = 512ull * 1024 * 1024;
constexpr std::size_t kMaxFiles = 4096;
constexpr int kMaxSearchDepth = 4;

using Files = std::map<std::string, Bytes>; // upper-case name -> content

std::string upper(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

bool isJunk(const std::string& component) {
    return component.empty() || component[0] == '.' || component == "__MACOSX";
}

Bytes readWholeFile(const fs::path& path) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec || size > zip::kMaxEntrySize) {
        throw ImportError("Could not read " + path.filename().string() + ".");
    }
    std::ifstream in(path, std::ios::binary);
    Bytes data(static_cast<std::size_t>(size));
    if (!in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) {
        throw ImportError("Could not read " + path.filename().string() + ".");
    }
    return data;
}

void addFile(Files& files, std::uint64_t& total, const std::string& name, Bytes content) {
    const std::string key = upper(name);
    if (!isSafeName(key) || files.count(key) != 0) {
        return; // not a DOS file name, or the same name twice in other case
    }
    if (files.size() >= kMaxFiles || content.size() > kMaxTotalSize - total) {
        throw ImportError("This is far bigger than the original game. Choose the game's own folder.");
    }
    total += content.size();
    files.emplace(key, std::move(content));
}

// Breadth-first, so the shallowest copy of the game wins.
std::optional<fs::path> findGameFolder(const fs::path& root) {
    std::deque<std::pair<fs::path, int>> queue{{root, 0}};
    while (!queue.empty()) {
        auto [folder, depth] = queue.front();
        queue.pop_front();
        if (isGameFolder(folder)) {
            return folder;
        }
        if (depth == kMaxSearchDepth) {
            continue;
        }
        std::error_code ec;
        std::vector<fs::path> children;
        for (const auto& entry : fs::directory_iterator(folder, ec)) {
            if (entry.is_directory(ec) && !entry.is_symlink(ec) &&
                !isJunk(entry.path().filename().string())) {
                children.push_back(entry.path());
            }
        }
        std::sort(children.begin(), children.end());
        for (auto& child : children) {
            queue.emplace_back(std::move(child), depth + 1);
        }
    }
    return std::nullopt;
}

Files gatherFromFolder(const fs::path& source) {
    const auto folder = findGameFolder(source);
    if (!folder) {
        throw ImportError("No game files found here. Choose the folder that holds VICEROY.EXE.");
    }
    Files files;
    std::uint64_t total = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(*folder, ec)) {
        if (entry.is_regular_file(ec) && !entry.is_symlink(ec)) {
            const std::string name = entry.path().filename().string();
            if (isSafeName(upper(name))) {
                addFile(files, total, name, readWholeFile(entry.path()));
            }
        }
    }
    return files;
}

std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> parts;
    std::string part;
    std::istringstream in(path);
    while (std::getline(in, part, '/')) {
        parts.push_back(part);
    }
    return parts;
}

Files gatherFromZip(const fs::path& source) {
    std::vector<zip::Entry> entries;
    try {
        entries = zip::read(readWholeFile(source));
    } catch (const FormatError& e) {
        throw ImportError(std::string("This zip file can't be read: ") + e.what());
    }

    // Find the folder inside the zip that holds the game: the shallowest one
    // with the marker file. Mac junk (__MACOSX, ._*) never counts.
    std::optional<std::string> gameDir;
    std::size_t gameDepth = 0;
    for (const auto& entry : entries) {
        const auto parts = splitPath(entry.name);
        if (parts.empty() || std::any_of(parts.begin(), parts.end(), isJunk) ||
            upper(parts.back()) != kMarkerFile) {
            continue;
        }
        const std::string dir = entry.name.substr(0, entry.name.size() - parts.back().size());
        if (!gameDir || parts.size() < gameDepth) {
            gameDir = dir;
            gameDepth = parts.size();
        }
    }
    if (!gameDir) {
        throw ImportError("No game files found in this zip file.");
    }

    Files files;
    std::uint64_t total = 0;
    for (auto& entry : entries) {
        if (entry.name.size() <= gameDir->size() || entry.name.compare(0, gameDir->size(), *gameDir) != 0) {
            continue;
        }
        const std::string name = entry.name.substr(gameDir->size());
        if (name.find('/') != std::string::npos) {
            continue; // a subfolder; the game keeps everything in one folder
        }
        addFile(files, total, name, std::move(entry.content));
    }
    return files;
}

// Reads each required file with our own reader, so a damaged or wrong copy
// is caught now and not halfway through a game.
void validate(const Files& files) {
    const std::map<std::string, std::function<void(const Bytes&)>> checks{
        {"VICEROY.PAL", [](const Bytes& b) { (void)readVgaPalette(b); }},
        {"TERRAIN.SS", [](const Bytes& b) { (void)readSpriteSheet(b); }},
        {"PHYS0.SS", [](const Bytes& b) { (void)readSpriteSheet(b); }},
        {"FONTSMAL.FF", [](const Bytes& b) { (void)readFont(b); }},
        {"NAMES.TXT", [](const Bytes& b) { (void)readRuleset(parseTextFile(b)); }},
        {"GAME.TXT", [](const Bytes& b) { (void)parseTextFile(b); }},
    };
    for (const auto& name : requiredFiles()) {
        const auto it = files.find(name);
        if (it == files.end()) {
            throw ImportError(name + " is missing. Is this the complete DOS game?");
        }
        const auto check = checks.find(name);
        if (check == checks.end()) {
            continue;
        }
        try {
            check->second(it->second);
        } catch (const std::exception&) {
            throw ImportError(name + " is damaged or from another version of the game.");
        }
    }
}

void writeFile(const fs::path& path, const Bytes& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out) {
        throw ImportError("Could not write " + path.string() + ".");
    }
}

std::string manifest(const std::vector<ImportedFile>& files) {
    std::ostringstream out;
    out << "{\n"
        << "  \"format\": \"viceroy-pack\",\n"
        << "  \"formatVersion\": 1,\n"
        << "  \"id\": \"base\",\n"
        << "  \"kind\": \"original\",\n"
        << "  \"files\": [\n";
    for (std::size_t i = 0; i < files.size(); ++i) {
        char crc[9];
        std::snprintf(crc, sizeof crc, "%08x", files[i].crc32);
        out << "    {\"name\": \"" << files[i].name << "\", \"size\": " << files[i].size << ", \"crc32\": \"" << crc
            << "\"}" << (i + 1 < files.size() ? "," : "") << "\n";
    }
    out << "  ]\n}\n";
    return out.str();
}

bool isSafeChar(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

} // namespace

const std::vector<std::string>& requiredFiles() {
    static const std::vector<std::string> names{"VICEROY.PAL", "TERRAIN.SS", "PHYS0.SS",
                                                "FONTSMAL.FF", "NAMES.TXT",  "GAME.TXT"};
    return names;
}

fs::path baseFolder(const fs::path& library) {
    return library / "base" / "original";
}

fs::path savesFolder(const fs::path& library) {
    return library / "saves";
}

bool isSafeName(const std::string& name) {
    const auto dot = name.find('.');
    const std::string stem = name.substr(0, dot);
    const std::string ext = dot == std::string::npos ? "" : name.substr(dot + 1);
    if (stem.empty() || stem.size() > 8 || ext.size() > 3 || (dot != std::string::npos && ext.empty())) {
        return false;
    }
    return std::all_of(stem.begin(), stem.end(), isSafeChar) && std::all_of(ext.begin(), ext.end(), isSafeChar);
}

bool hasBasePack(const fs::path& library) {
    std::error_code ec;
    if (!fs::is_regular_file(library / "base" / "pack.json", ec)) {
        return false;
    }
    const auto folder = baseFolder(library);
    return std::all_of(requiredFiles().begin(), requiredFiles().end(),
                       [&](const std::string& name) { return fs::is_regular_file(folder / name, ec); });
}

ImportResult importGame(const fs::path& source, const fs::path& library) {
    std::error_code ec;
    Files files;
    if (fs::is_directory(source, ec)) {
        files = gatherFromFolder(source);
    } else if (fs::is_regular_file(source, ec)) {
        files = gatherFromZip(source);
    } else {
        throw ImportError("Could not open " + source.string() + ".");
    }
    validate(files);

    // Write the new pack next to the old one and swap it in at the end, so a
    // failed import never leaves a half-written library behind.
    const fs::path base = library / "base";
    const fs::path fresh = library / "base.new";
    const fs::path old = library / "base.old";
    fs::remove_all(fresh, ec);
    fs::create_directories(fresh / "original", ec);
    if (ec) {
        throw ImportError("Could not create the game library in " + library.string() + ".");
    }

    ImportResult result;
    try {
        for (const auto& [name, content] : files) {
            writeFile(fresh / "original" / name, content);
            result.files.push_back({name, content.size(), zlib::crc32(content)});
        }
        const std::string json = manifest(result.files);
        writeFile(fresh / "pack.json", Bytes(json.begin(), json.end()));
    } catch (...) {
        fs::remove_all(fresh, ec);
        throw;
    }

    fs::remove_all(old, ec);
    if (fs::exists(base, ec)) {
        fs::rename(base, old, ec);
        if (ec) {
            fs::remove_all(fresh, ec);
            throw ImportError("Could not replace the earlier import in " + library.string() + ".");
        }
    }
    fs::rename(fresh, base, ec);
    if (ec) {
        fs::rename(old, base, ec);
        throw ImportError("Could not finish the import in " + library.string() + ".");
    }
    fs::remove_all(old, ec);

    // The savegames that came with the game, copied once. A player's own
    // save of the same name is never overwritten.
    const fs::path saves = savesFolder(library);
    fs::create_directories(saves, ec);
    for (const auto& [name, content] : files) {
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".SAV") == 0 && !fs::exists(saves / name, ec)) {
            try {
                writeFile(saves / name, content);
                ++result.savesCopied;
            } catch (const ImportError&) {
                // The game itself is imported; a missing sample save is fine.
            }
        }
    }
    return result;
}

} // namespace viceroy::pack
