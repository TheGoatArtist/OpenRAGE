// Turning megatexture map pages into images (docs/research/formats/megatexture-pages.md).
#pragma once

#include <cstdint>
#include <vector>

#include "pages.h"

namespace pages {

// A page is 128x128 texels: a 120x120 payload with a 4-texel border copied from its neighbours.
constexpr int PAGE_SIZE = 128;
constexpr int PAGE_BORDER = 4;
constexpr int PAGE_PAYLOAD = PAGE_SIZE - 2 * PAGE_BORDER;

// Decode a page's diffuse layer to 128x128 RGB, from either body layout: map pages (JPEG XR) or
// model pages (_vmtr, DCT). Returns false when the body is in neither. Throws if a stream is damaged.
bool decode_diffuse(const std::vector<uint8_t>& body, std::vector<uint8_t>& rgb);

struct LevelImage {
    int width = 0, height = 0;  // in texels: 120 per page
    std::vector<uint8_t> rgb;
    size_t decoded = 0;  // pages decoded at this level
    size_t filled = 0;   // pages the file leaves out, filled from the nearest coarser page
};

// The diffuse layer of one whole level, page payloads side by side. Throws when the file has no
// such level or no page decodes.
LevelImage assemble_diffuse(PagesFile& file, uint32_t level);

// The same for a rectangle of that level, in texels (page x's payload is texels 120x to 120x+119).
LevelImage assemble_diffuse(PagesFile& file, uint32_t level, int x, int y, int width, int height);

}  // namespace pages
