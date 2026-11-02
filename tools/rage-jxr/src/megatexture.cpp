#include "megatexture.h"

#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

#include "entropy.h"
#include "transform.h"

namespace pages {

bool decode_diffuse(const std::vector<uint8_t>& body, std::vector<uint8_t>& rgb) {
    MapPage map;
    if (!map.parse(body) || map.stream_size[0] == 0) return false;
    jxr::Decoder d(map.stream[0], map.stream_size[0], jxr::Rules::Rage(), PAGE_SIZE, PAGE_SIZE);
    auto mbs = d.decode();
    if (d.cf != jxr::ColorFormat::Yuv444) return false;
    // RAGE's pages always use overlap mode 1 and JPEG XR's operators
    rgb = jxr::rage_ycocg_to_rgb(jxr::reconstruct(d.header, mbs, d.mbw, d.mbh, 1, 1));
    return true;
}

LevelImage assemble_diffuse(PagesFile& file, uint32_t level) {
    if (level >= file.levels)
        throw std::runtime_error("level " + std::to_string(level) + " is out of range (the file has " +
                                 std::to_string(file.levels) + " levels)");
    const uint32_t across = 1u << (file.levels - 1 - level);
    LevelImage out;
    out.width = out.height = static_cast<int>(across) * PAGE_PAYLOAD;
    out.rgb.assign(static_cast<size_t>(out.width) * out.height * 3, 0);

    // Coarser pages are shared by many missing pages below them, so keep them once decoded.
    std::map<const PageEntry*, std::vector<uint8_t>> coarse;
    std::vector<uint8_t> rgb;
    size_t not_map = 0;

    for (uint32_t y = 0; y < across; ++y) {
        for (uint32_t x = 0; x < across; ++x) {
            // The page itself, or the nearest coarser page that covers it: k levels up, it
            // covers s x s pages of this level.
            const std::vector<uint8_t>* src = nullptr;
            uint32_t k = 0;
            for (; level + k < file.levels; ++k) {
                const PageEntry* p = file.find(x >> k, y >> k, level + k);
                if (!p) continue;
                if (k == 0) {
                    if (!decode_diffuse(file.body(*p), rgb)) {
                        ++not_map;
                        continue;
                    }
                    src = &rgb;
                } else {
                    auto it = coarse.find(p);
                    if (it == coarse.end()) {
                        std::vector<uint8_t> px;
                        if (!decode_diffuse(file.body(*p), px)) continue;
                        it = coarse.emplace(p, std::move(px)).first;
                    }
                    src = &it->second;
                }
                break;
            }
            if (!src) continue;  // left black
            ++(k == 0 ? out.decoded : out.filled);

            // Nearest-neighbour copy of this page's share of the source payload.
            const uint32_t s = 1u << k;
            const uint32_t ox = (x & (s - 1)) * PAGE_PAYLOAD, oy = (y & (s - 1)) * PAGE_PAYLOAD;
            for (int j = 0; j < PAGE_PAYLOAD; ++j) {
                const size_t sy = PAGE_BORDER + (oy + j) / s;
                uint8_t* dst = &out.rgb[((static_cast<size_t>(y) * PAGE_PAYLOAD + j) * out.width + static_cast<size_t>(x) * PAGE_PAYLOAD) * 3];
                if (s == 1) {
                    std::memcpy(dst, &(*src)[(sy * PAGE_SIZE + PAGE_BORDER) * 3], PAGE_PAYLOAD * 3);
                    continue;
                }
                for (int i = 0; i < PAGE_PAYLOAD; ++i) {
                    const size_t sx = PAGE_BORDER + (ox + i) / s;
                    std::memcpy(dst + i * 3, &(*src)[(sy * PAGE_SIZE + sx) * 3], 3);
                }
            }
        }
    }
    if (out.decoded == 0 && out.filled == 0)
        throw std::runtime_error(not_map ? "not a map megatexture (its pages use another codec)"
                                         : "no pages found for level " + std::to_string(level));
    return out;
}

}  // namespace pages
