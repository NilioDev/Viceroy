// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bounds-checked little-endian reading of untrusted bytes. Every read past the
// end throws FormatError, so a damaged or hostile file can never make the
// engine read outside its buffer.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace viceroy {

class FormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> data) : data_(data) {}

    std::size_t pos() const { return pos_; }
    std::size_t size() const { return data_.size(); }
    std::size_t remaining() const { return data_.size() - pos_; }

    void seek(std::size_t pos) {
        if (pos > data_.size()) {
            throw FormatError("seek past end of data");
        }
        pos_ = pos;
    }

    std::uint8_t u8() {
        need(1);
        return data_[pos_++];
    }

    std::uint16_t u16() {
        need(2);
        const auto v = static_cast<std::uint16_t>(data_[pos_] | (data_[pos_ + 1] << 8));
        pos_ += 2;
        return v;
    }

    std::uint32_t u32() {
        need(4);
        const auto v = static_cast<std::uint32_t>(data_[pos_]) | (static_cast<std::uint32_t>(data_[pos_ + 1]) << 8) |
                       (static_cast<std::uint32_t>(data_[pos_ + 2]) << 16) |
                       (static_cast<std::uint32_t>(data_[pos_ + 3]) << 24);
        pos_ += 4;
        return v;
    }

    void skip(std::size_t count) {
        need(count);
        pos_ += count;
    }

    std::span<const std::uint8_t> bytes(std::size_t count) {
        need(count);
        auto out = data_.subspan(pos_, count);
        pos_ += count;
        return out;
    }

private:
    void need(std::size_t count) const {
        if (count > remaining()) {
            throw FormatError("unexpected end of data");
        }
    }

    std::span<const std::uint8_t> data_;
    std::size_t pos_ = 0;
};

} // namespace viceroy
