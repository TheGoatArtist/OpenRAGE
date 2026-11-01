// JPEG XR pixel reconstruction: dequantization, inverse core transform, overlap filter, output.
//
// A line-for-line port of jxrlib's strInvTransform.c (BSD-2-Clause, Copyright (c) Microsoft
// Corp.; see LICENSE-jxrlib.txt) for one tile, full frame, Y_ONLY / 4:4:4 / N-component.
#pragma once

#include <cstdint>
#include <vector>

#include "entropy.h"

namespace jxr {

// Reconstructed planes: one width * height array per channel, in the scaled integer domain.
struct Planes {
    int width = 0, height = 0;
    bool scaled = false;
    std::vector<std::vector<int32_t>> data;
};

// Run the inverse transform over a decoded frame. `subversion` 0 selects HD Photo 1.0's
// operators, anything else JPEG XR's. `overlap` is 0, 1 or 2.
Planes reconstruct(const PlaneHeader& h, const std::vector<Macroblock>& mbs, int mbw, int mbh, int overlap,
                   int subversion);

// RAGE's scale between the planes and 8-bit values, fitted against ground truth (inferred).
constexpr double RAGE_LUMA_SCALE = 9.6875;
constexpr double RAGE_CHROMA_SCALE = 2.0 * RAGE_LUMA_SCALE;

// A diffuse page: the three planes hold RAGE's Y, Co and Cg.
std::vector<uint8_t> rage_ycocg_to_rgb(const Planes& p);
// One plane as 8-bit grey with the luma scale (the scale of non-diffuse layers is not confirmed).
std::vector<uint8_t> rage_plane_to_gray(const Planes& p, int ch);
// jxrlib's 8-bit RGB output for standard 4:4:4 streams (outputMBRow, CF_RGB).
std::vector<uint8_t> jxr_yuv_to_rgb(const Planes& p);

}  // namespace jxr
