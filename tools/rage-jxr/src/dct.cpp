#include "dct.h"

#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

#include "bits.h"

namespace dct {

namespace {

// ITU T.81 Annex K: the example Huffman tables (code counts per length 1-16, then the values).
const uint8_t DC_LUMA_BITS[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
const uint8_t DC_CHROMA_BITS[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
const uint8_t DC_VALUES[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

const uint8_t AC_LUMA_BITS[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
const uint8_t AC_LUMA_VALUES[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71,
    0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3,
    0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};

const uint8_t AC_CHROMA_BITS[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
const uint8_t AC_CHROMA_VALUES[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22,
    0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1,
    0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
    0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
    0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
    0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};

// Annex K quantization tables, in row order.
const int Q_LUMA[64] = {16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
                        14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
                        18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
                        49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
const int Q_CHROMA[64] = {17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
                          24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
                          99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                          99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};

const int ZIGZAG[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
                        41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                        30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

class Huffman {
public:
    Huffman(const uint8_t bits[16], const uint8_t* values) : values_(values) {
        int code = 0, k = 0;
        for (int len = 1; len <= 16; ++len) {
            first_index_[len] = k;
            first_code_[len] = code;
            code += bits[len - 1];
            k += bits[len - 1];
            last_code_[len] = code - 1;  // -1 when no code has this length
            code <<= 1;
        }
    }

    int decode(jxr::Bits& b) const {
        int code = 0;
        for (int len = 1; len <= 16; ++len) {
            code = code << 1 | static_cast<int>(b.bit());
            if (code <= last_code_[len] && code >= first_code_[len])
                return values_[first_index_[len] + code - first_code_[len]];
        }
        throw std::runtime_error("invalid Huffman code at bit " + std::to_string(b.pos));
    }

private:
    const uint8_t* values_;
    int first_code_[17] = {}, last_code_[17] = {}, first_index_[17] = {};
};

const Huffman& dc_luma() { static const Huffman h(DC_LUMA_BITS, DC_VALUES); return h; }
const Huffman& dc_chroma() { static const Huffman h(DC_CHROMA_BITS, DC_VALUES); return h; }
const Huffman& ac_luma() { static const Huffman h(AC_LUMA_BITS, AC_LUMA_VALUES); return h; }
const Huffman& ac_chroma() { static const Huffman h(AC_CHROMA_BITS, AC_CHROMA_VALUES); return h; }

// The standard tables scaled by a quality setting, as libjpeg does it.
std::array<int, 64> quant(const int* table, int quality) {
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;
    const int scale = quality < 50 ? 5000 / quality : 200 - 2 * quality;
    std::array<int, 64> q{};
    for (int i = 0; i < 64; ++i) {
        int v = (table[i] * scale + 50) / 100;
        q[i] = v < 1 ? 1 : v > 255 ? 255 : v;
    }
    return q;
}

// A coefficient's value from its size category and the extra bits that follow (T.81 F.2.2.1).
int extend(jxr::Bits& b, int size) {
    if (size == 0) return 0;
    int v = static_cast<int>(b.get(static_cast<uint32_t>(size)));
    return v < (1 << (size - 1)) ? v - (1 << size) + 1 : v;
}

struct Channel {
    const Huffman* dc;
    const Huffman* ac;
    std::array<int, 64> q;
    int pred = 0;  // DC of the previous block of this channel
};

// One 8x8 block: entropy decode, dequantize and inverse DCT, without the level shift.
void block(jxr::Bits& b, Channel& ch, float out[64]) {
    int coef[64] = {};
    ch.pred += extend(b, ch.dc->decode(b));
    coef[0] = ch.pred;
    for (int k = 1; k < 64;) {
        int rs = ch.ac->decode(b);
        int run = rs >> 4, size = rs & 15;
        if (size == 0) {
            if (run != 15) break;  // end of block
            k += 16;
            continue;
        }
        k += run;
        if (k > 63) throw std::runtime_error("AC run past the end of a block at bit " + std::to_string(b.pos));
        coef[ZIGZAG[k]] = extend(b, size);
        ++k;
    }

    // Separable float IDCT: out[y][x] = 1/4 sum_v sum_u C(u) C(v) F(v,u) cos((2x+1)u pi/16) cos((2y+1)v pi/16)
    static const auto basis = [] {
        std::array<float, 64> c{};
        for (int x = 0; x < 8; ++x)
            for (int u = 0; u < 8; ++u)
                c[x * 8 + u] = static_cast<float>(std::cos((2 * x + 1) * u * 3.14159265358979323846 / 16) *
                                                  (u == 0 ? std::sqrt(0.5) : 1.0) / 2);
        return c;
    }();
    float f[64], tmp[64];
    for (int i = 0; i < 64; ++i) f[i] = static_cast<float>(coef[i] * ch.q[i]);
    for (int v = 0; v < 8; ++v)  // rows: over u
        for (int x = 0; x < 8; ++x) {
            float s = 0;
            for (int u = 0; u < 8; ++u) s += basis[x * 8 + u] * f[v * 8 + u];
            tmp[v * 8 + x] = s;
        }
    for (int x = 0; x < 8; ++x)  // columns: over v
        for (int y = 0; y < 8; ++y) {
            float s = 0;
            for (int v = 0; v < 8; ++v) s += basis[y * 8 + v] * tmp[v * 8 + x];
            out[y * 8 + x] = s;
        }
}

uint8_t clamp8(float v) {
    long r = std::lround(v);
    return static_cast<uint8_t>(r < 0 ? 0 : r > 255 ? 255 : r);
}

void finish(const jxr::Bits& b, size_t size, size_t* bits_used) {
    if (b.pos > size * 8) throw std::runtime_error("stream ends early");
    if (bits_used) *bits_used = b.pos;
}

}  // namespace

std::vector<uint8_t> diffuse_rgb(const uint8_t* data, size_t size, int quality, size_t* bits_used) {
    jxr::Bits b(data, size);
    Channel y{&dc_luma(), &ac_luma(), quant(Q_LUMA, quality)};
    Channel c[2] = {{&dc_chroma(), &ac_chroma(), quant(Q_CHROMA, quality)},
                    {&dc_chroma(), &ac_chroma(), quant(Q_CHROMA, quality)}};
    std::vector<float> Y(128 * 128), C[2] = {std::vector<float>(64 * 64), std::vector<float>(64 * 64)};
    float px[64];
    // Per 16x16 area, in rows: four Y blocks (TL, TR, BL, BR), then Co, then Cg
    for (int my = 0; my < 8; ++my)
        for (int mx = 0; mx < 8; ++mx) {
            for (int i = 0; i < 4; ++i) {
                block(b, y, px);
                const int ox = mx * 16 + (i & 1) * 8, oy = my * 16 + (i >> 1) * 8;
                for (int j = 0; j < 64; ++j) Y[(oy + j / 8) * 128 + ox + j % 8] = px[j] + 128;
            }
            for (int k = 0; k < 2; ++k) {
                block(b, c[k], px);
                for (int j = 0; j < 64; ++j) C[k][(my * 8 + j / 8) * 64 + mx * 8 + j % 8] = px[j];
            }
        }
    finish(b, size, bits_used);

    // Co and Cg are stored at half scale around 64
    std::vector<uint8_t> rgb(128 * 128 * 3);
    for (int ty = 0; ty < 128; ++ty)
        for (int tx = 0; tx < 128; ++tx) {
            const int ci = (ty / 2) * 64 + tx / 2;
            const float l = Y[ty * 128 + tx], co = 2 * (C[0][ci] - 64), cg = 2 * (C[1][ci] - 64);
            uint8_t* o = &rgb[(ty * 128 + tx) * 3];
            o[0] = clamp8(l + co - cg);
            o[1] = clamp8(l + cg);
            o[2] = clamp8(l - co - cg);
        }
    return rgb;
}

std::vector<uint8_t> normal_xy(const uint8_t* data, size_t size, int quality, size_t* bits_used) {
    jxr::Bits b(data, size);
    Channel ch[2] = {{&dc_luma(), &ac_luma(), quant(Q_LUMA, quality)}, {&dc_luma(), &ac_luma(), quant(Q_LUMA, quality)}};
    std::vector<uint8_t> out(128 * 128 * 2);
    float px[64];
    // X and Y alternate per 8x8 tile; tiles in rows. Both are stored at half scale around -64.
    for (int t = 0; t < 256; ++t)
        for (int k = 0; k < 2; ++k) {
            block(b, ch[k], px);
            const int ox = (t % 16) * 8, oy = (t / 16) * 8;
            for (int j = 0; j < 64; ++j) out[((oy + j / 8) * 128 + ox + j % 8) * 2 + k] = clamp8(2 * (px[j] + 64));
        }
    finish(b, size, bits_used);
    return out;
}

std::vector<uint8_t> gray(const uint8_t* data, size_t size, int quality, int side, size_t* bits_used) {
    jxr::Bits b(data, size);
    Channel ch{&dc_luma(), &ac_luma(), quant(Q_LUMA, quality)};
    std::vector<uint8_t> out(static_cast<size_t>(side) * side);
    const int across = side / 8;
    float px[64];
    for (int t = 0; t < across * across; ++t) {
        block(b, ch, px);
        const int ox = (t % across) * 8, oy = (t / across) * 8;
        for (int j = 0; j < 64; ++j) out[(oy + j / 8) * side + ox + j % 8] = clamp8(px[j] + 128);
    }
    finish(b, size, bits_used);
    return out;
}

std::vector<uint8_t> mask(const uint8_t* data, size_t size) {
    if (size < 128 * 128 / 8) throw std::runtime_error("mask is too short");
    std::vector<uint8_t> out(128 * 128);
    for (size_t i = 0; i < out.size(); ++i) out[i] = (data[i >> 3] >> (i & 7)) & 1 ? 255 : 0;
    return out;
}

}  // namespace dct
