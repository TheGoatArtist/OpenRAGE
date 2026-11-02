// RAGE's DCT page codec, used by model pages (_vmtr.pages): baseline JPEG entropy coding with the
// standard tables, no markers and no byte stuffing (docs/research/formats/megatexture-pages.md,
// "Model pages").
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dct {

// Each layer decoder reads one stream and returns 8-bit texels, row by row. They throw
// std::runtime_error on an invalid code or when the stream runs out. `bits_used`, when given,
// receives the number of bits read, for checking that a stream ends where it should.

// Diffuse, and colour specular: YCoCg 4:2:0, returned as 128x128 RGB.
std::vector<uint8_t> diffuse_rgb(const uint8_t* data, size_t size, int quality, size_t* bits_used = nullptr);
// Normal map: 128x128, two values per texel (X, Y) as the game exports them.
std::vector<uint8_t> normal_xy(const uint8_t* data, size_t size, int quality, size_t* bits_used = nullptr);
// One channel, `side` x `side`: the specular layer (128) or the power layer (32).
std::vector<uint8_t> gray(const uint8_t* data, size_t size, int quality, int side, size_t* bits_used = nullptr);
// The cover mask: 128x128 bits, row by row, least significant bit first. Returns 0 or 255 per texel.
std::vector<uint8_t> mask(const uint8_t* data, size_t size);

}  // namespace dct
