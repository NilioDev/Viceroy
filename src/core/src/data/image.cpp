// SPDX-License-Identifier: GPL-3.0-or-later
#include "viceroy/data/image.hpp"

#include "viceroy/data/reader.hpp"

namespace viceroy {

namespace {

constexpr std::size_t kPaletteBytes = 768;
constexpr int kMaxDimension = 4096;

// Sprite line commands.
constexpr std::uint8_t kLineEnd = 0xFF;
constexpr std::uint8_t kSpriteEnd = 0xFC;
constexpr std::uint8_t kLineRaw = 0xFE;
constexpr std::uint8_t kLineRuns = 0xFD;
constexpr std::uint8_t kRawRun = 0xFE;

std::uint8_t scale6(std::uint8_t v) {
    const unsigned c = v & 0x3Fu;
    return static_cast<std::uint8_t>((c * 255u + 31u) / 63u);
}

void checkSize(int width, int height) {
    if (width < 0 || height < 0 || width > kMaxDimension || height > kMaxDimension) {
        throw FormatError("image with impossible size");
    }
}

std::optional<Palette> optionalPalette(const std::vector<Bytes>& entries, std::size_t index) {
    if (entries.size() > index && entries[index].size() >= kPaletteBytes) {
        return readVgaPalette(entries[index]);
    }
    return std::nullopt;
}

} // namespace

Palette readVgaPalette(std::span<const std::uint8_t> data) {
    if (data.size() < kPaletteBytes) {
        throw FormatError("palette too short");
    }
    Palette palette{};
    for (std::size_t i = 0; i < palette.size(); ++i) {
        palette[i] = {scale6(data[i * 3]), scale6(data[i * 3 + 1]), scale6(data[i * 3 + 2])};
    }
    return palette;
}

Picture readPicture(std::span<const std::uint8_t> file) {
    const auto entries = readMadspack(file);
    if (entries.size() < 2) {
        throw FormatError("picture without pixels");
    }
    Reader header(entries[0]);
    const int height = header.u16();
    const int width = header.u16();
    checkSize(width, height);
    const auto pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (entries[1].size() != pixelCount) {
        throw FormatError("picture pixel count does not match its size");
    }
    Picture picture;
    picture.image = {width, height, entries[1]};
    picture.palette = optionalPalette(entries, 2);
    return picture;
}

IndexedImage decodeSprite(std::span<const std::uint8_t> data, int width, int height) {
    checkSize(width, height);
    IndexedImage image{width, height,
                       std::vector<std::uint8_t>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                                                 kTransparent)};
    Reader in(data);
    auto put = [&](int x, int y, std::uint8_t colour) {
        // Lines may be shorter than the sprite; pixels past its width are
        // dropped, like the original does.
        if (x < width && colour != kTransparent) {
            image.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)] = colour;
        }
    };
    for (int y = 0; y < height; ++y) {
        const std::uint8_t mode = in.u8();
        if (mode == kSpriteEnd) {
            break;
        }
        if (mode == kLineEnd) {
            continue; // empty line
        }
        if (mode != kLineRaw && mode != kLineRuns) {
            throw FormatError("unknown sprite line command");
        }
        int x = 0;
        for (;;) {
            std::uint8_t b = in.u8();
            if (b == kLineEnd) {
                break;
            }
            int count = 1;
            std::uint8_t colour = b;
            if (mode == kLineRuns) {
                count = b;
                colour = in.u8();
            } else if (b == kRawRun) {
                count = in.u8();
                colour = in.u8();
            }
            for (int i = 0; i < count; ++i) {
                put(x++, y, colour);
            }
            if (x > kMaxDimension) {
                throw FormatError("sprite line too long");
            }
        }
    }
    return image;
}

SpriteSheet readSpriteSheet(std::span<const std::uint8_t> file) {
    const auto entries = readMadspack(file);
    if (entries.size() < 4) {
        throw FormatError("sprite sheet with missing parts");
    }
    constexpr std::size_t kRecordSize = 16;
    const Bytes& table = entries[1];
    const Bytes& pixels = entries[3];
    SpriteSheet sheet;
    sheet.palette = optionalPalette(entries, 2);
    Reader records(table);
    for (std::size_t i = 0; i < table.size() / kRecordSize; ++i) {
        const std::size_t offset = records.u32();
        const std::size_t length = records.u32();
        Sprite sprite;
        sprite.x = records.u16();
        sprite.y = records.u16();
        const int width = records.u16();
        const int height = records.u16();
        if (offset > pixels.size() || length > pixels.size() - offset) {
            throw FormatError("sprite data outside its sheet");
        }
        sprite.image = decodeSprite(std::span(pixels).subspan(offset, length), width, height);
        sheet.sprites.push_back(std::move(sprite));
    }
    return sheet;
}

} // namespace viceroy
