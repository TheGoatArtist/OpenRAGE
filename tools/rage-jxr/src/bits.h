// MSB-first bit reader. Reading past the end yields zero bits; callers check overrun().
#pragma once

#include <cstddef>
#include <cstdint>

namespace jxr {

class Bits {
public:
    Bits(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    size_t len_bits() const { return size_ * 8; }

    // True once the reader has gone more than 64 bits past the end, which only a misparse does.
    bool overrun() const { return pos > len_bits() + 64; }

    uint32_t bit() {
        size_t i = pos >> 3;
        uint32_t byte = i < size_ ? data_[i] : 0;
        uint32_t v = (byte >> (7 - (pos & 7))) & 1;
        ++pos;
        return v;
    }

    uint32_t get(uint32_t n) {
        uint32_t v = 0;
        for (uint32_t k = 0; k < n; ++k) v = (v << 1) | bit();
        return v;
    }

    uint32_t peek(uint32_t n) {
        size_t saved = pos;
        uint32_t v = get(n);
        pos = saved;
        return v;
    }

    // The byte holding bit `bit_pos`, or -1 past the end.
    int byte_at(size_t bit_pos) const {
        size_t i = bit_pos >> 3;
        return i < size_ ? data_[i] : -1;
    }

    size_t pos = 0;

private:
    const uint8_t* data_;
    size_t size_;
};

}  // namespace jxr
