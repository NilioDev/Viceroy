// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "viceroy/data/reader.hpp"
#include "viceroy/game_files.hpp"
#include "viceroy/pack/library.hpp"
#include "viceroy/util/zip.hpp"
#include "viceroy/util/zlib.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace viceroy;
namespace fs = std::filesystem;

namespace {

// Writes a zip archive with stored (uncompressed) entries.
struct ZipWriter {
    Bytes out;
    Bytes directory;
    std::uint16_t count = 0;

    static void put16(Bytes& b, std::uint32_t v) {
        b.push_back(static_cast<std::uint8_t>(v));
        b.push_back(static_cast<std::uint8_t>(v >> 8));
    }
    static void put32(Bytes& b, std::uint32_t v) {
        put16(b, v & 0xffff);
        put16(b, v >> 16);
    }

    void add(const std::string& name, const Bytes& content) {
        const auto offset = static_cast<std::uint32_t>(out.size());
        const std::uint32_t crc = zlib::crc32(content);
        const auto size = static_cast<std::uint32_t>(content.size());
        put32(out, 0x04034b50);
        put16(out, 20);
        put16(out, 0);
        put16(out, 0); // stored
        put32(out, 0);
        put32(out, crc);
        put32(out, size);
        put32(out, size);
        put16(out, static_cast<std::uint32_t>(name.size()));
        put16(out, 0);
        out.insert(out.end(), name.begin(), name.end());
        out.insert(out.end(), content.begin(), content.end());

        put32(directory, 0x02014b50);
        put16(directory, 20);
        put16(directory, 20);
        put16(directory, 0);
        put16(directory, 0);
        put32(directory, 0);
        put32(directory, crc);
        put32(directory, size);
        put32(directory, size);
        put16(directory, static_cast<std::uint32_t>(name.size()));
        put16(directory, 0);
        put16(directory, 0);
        put32(directory, 0);
        put32(directory, 0);
        put32(directory, offset);
        directory.insert(directory.end(), name.begin(), name.end());
        ++count;
    }

    Bytes finish() {
        Bytes archive = out;
        const auto dirOffset = static_cast<std::uint32_t>(archive.size());
        archive.insert(archive.end(), directory.begin(), directory.end());
        put32(archive, 0x06054b50);
        put16(archive, 0);
        put16(archive, 0);
        put16(archive, count);
        put16(archive, count);
        put32(archive, static_cast<std::uint32_t>(directory.size()));
        put32(archive, dirOffset);
        put16(archive, 0);
        return archive;
    }
};

Bytes text(const std::string& s) {
    return Bytes(s.begin(), s.end());
}

Bytes readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), {});
}

void writeAll(const fs::path& path, const Bytes& data) {
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data.data()),
                                                static_cast<std::streamsize>(data.size()));
}

fs::path freshDir(const std::string& name) {
    const auto dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

template <typename F>
bool throwsImportError(F&& f) {
    try {
        f();
    } catch (const pack::ImportError&) {
        return true;
    }
    return false;
}

} // namespace

VCR_TEST("crc32 matches the standard check value") {
    VCR_CHECK(zlib::crc32(text("123456789")) == 0xCBF43926u);
}

VCR_TEST("deflate streams unpack, and the size limit stops zip bombs") {
    const Bytes packed{0x0b, 0xcb, 0x4c, 0x4e, 0x2d, 0xca, 0xaf, 0x54, 0x08,
                       0x1b, 0xa5, 0xc9, 0xa2, 0x53, 0xf3, 0x52, 0x00};
    std::string expected;
    for (int i = 0; i < 40; ++i) {
        expected += "Viceroy ";
    }
    expected += "end";
    VCR_CHECK(zlib::inflate(packed, 1000) == text(expected));
    bool limited = false;
    try {
        (void)zlib::inflate(packed, 100);
    } catch (const FormatError&) {
        limited = true;
    }
    VCR_CHECK(limited);
}

VCR_TEST("zip archives are read and damage is caught") {
    ZipWriter w;
    w.add("GAME/A.TXT", text("hello"));
    w.add("GAME/", {});
    Bytes archive = w.finish();
    const auto entries = zip::read(archive);
    VCR_CHECK(entries.size() == 1);
    VCR_CHECK(entries[0].name == "GAME/A.TXT");
    VCR_CHECK(entries[0].content == text("hello"));

    archive[30 + 10] ^= 0xff; // a byte of A.TXT's content
    bool damaged = false;
    try {
        (void)zip::read(archive);
    } catch (const FormatError&) {
        damaged = true;
    }
    VCR_CHECK(damaged);

    for (std::size_t cut : {std::size_t{0}, std::size_t{10}, archive.size() / 2, archive.size() - 1}) {
        bool rejected = false;
        try {
            (void)zip::read(std::span<const std::uint8_t>(archive).first(cut));
        } catch (const FormatError&) {
            rejected = true;
        }
        VCR_CHECK(rejected);
    }
}

VCR_TEST("only plain DOS names are stored") {
    VCR_CHECK(pack::isSafeName("TERRAIN.SS"));
    VCR_CHECK(pack::isSafeName("DESCRIPT.ION"));
    VCR_CHECK(pack::isSafeName("CC-00.SS"));
    VCR_CHECK(!pack::isSafeName(""));
    VCR_CHECK(!pack::isSafeName(".."));
    VCR_CHECK(!pack::isSafeName("../EVIL.TXT"));
    VCR_CHECK(!pack::isSafeName("A\\B.TXT"));
    VCR_CHECK(!pack::isSafeName("terrain.ss"));
    VCR_CHECK(!pack::isSafeName("TOOLONGNAME.TXT"));
    VCR_CHECK(!pack::isSafeName("A.B.C"));
    VCR_CHECK(!pack::isSafeName("NAME."));
}

VCR_TEST("a folder without the game is refused and changes nothing") {
    const auto source = freshDir("viceroy_test_import_empty");
    const auto library = freshDir("viceroy_test_import_empty_lib");
    std::ofstream(source / "README.TXT") << "not a game";
    VCR_CHECK(throwsImportError([&] { pack::importGame(source, library); }));
    VCR_CHECK(throwsImportError([&] { pack::importGame(source / "missing.zip", library); }));
    VCR_CHECK(!pack::hasBasePack(library));
    VCR_CHECK(fs::is_empty(library));
    fs::remove_all(source);
    fs::remove_all(library);
}

VCR_TEST("a damaged game file is refused") {
    const auto source = freshDir("viceroy_test_import_damaged");
    const auto library = freshDir("viceroy_test_import_damaged_lib");
    for (const auto& name : pack::requiredFiles()) {
        std::ofstream(source / name) << "rubbish";
    }
    VCR_CHECK(throwsImportError([&] { pack::importGame(source, library); }));
    VCR_CHECK(!pack::hasBasePack(library));
    fs::remove_all(source);
    fs::remove_all(library);
}

VCR_TEST("the original game imports from its folder") {
    const fs::path game = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(game)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const auto library = freshDir("viceroy_test_import_folder_lib");
    const auto result = pack::importGame(game, library);
    VCR_CHECK(pack::hasBasePack(library));
    VCR_CHECK(result.files.size() > 250);
    VCR_CHECK(result.savesCopied == 10);
    VCR_CHECK(readAll(pack::baseFolder(library) / "VICEROY.PAL") == readAll(*findFileNoCase(game, "VICEROY.PAL")));
    VCR_CHECK(fs::is_regular_file(pack::savesFolder(library) / "COLONY00.SAV"));

    // A second import replaces the pack but keeps the player's saves.
    writeAll(pack::savesFolder(library) / "COLONY00.SAV", text("my own game"));
    const auto again = pack::importGame(game, library);
    VCR_CHECK(again.savesCopied == 0);
    VCR_CHECK(readAll(pack::savesFolder(library) / "COLONY00.SAV") == text("my own game"));
    VCR_CHECK(pack::hasBasePack(library));
    VCR_CHECK(!fs::exists(library / "base.new"));
    VCR_CHECK(!fs::exists(library / "base.old"));
    fs::remove_all(library);
}

VCR_TEST("the original game imports from a zip, ignoring junk and hostile names") {
    const fs::path game = VICEROY_GAMEDATA_DIR;
    if (!isGameFolder(game)) {
        VCR_SKIP("no game files in gamedata/");
    }
    const auto work = freshDir("viceroy_test_import_zip");
    const auto library = work / "library";
    ZipWriter w;
    w.add("__MACOSX/col/._VICEROY.PAL", text("mac junk"));
    w.add("col/../EVIL.TXT", text("escape"));
    w.add("col/sub/VICEROY.PAL", text("deeper copy"));
    for (const auto& name : pack::requiredFiles()) {
        w.add("col/" + name, readAll(*findFileNoCase(game, name)));
    }
    w.add("col/colony00.sav", readAll(*findFileNoCase(game, "COLONY00.SAV")));
    const auto zipPath = work / "col.zip";
    writeAll(zipPath, w.finish());

    const auto result = pack::importGame(zipPath, library);
    VCR_CHECK(pack::hasBasePack(library));
    VCR_CHECK(result.files.size() == pack::requiredFiles().size() + 1);
    VCR_CHECK(fs::is_regular_file(pack::baseFolder(library) / "COLONY00.SAV"));
    VCR_CHECK(result.savesCopied == 1);
    VCR_CHECK(!fs::exists(work / "EVIL.TXT"));
    VCR_CHECK(!fs::exists(library / "EVIL.TXT"));
    VCR_CHECK(!fs::exists(library / "base" / "EVIL.TXT"));
    VCR_CHECK(readAll(pack::baseFolder(library) / "VICEROY.PAL") == readAll(*findFileNoCase(game, "VICEROY.PAL")));
    fs::remove_all(work);
}
