#include "megatexture.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

#include "dct.h"
#include "entropy.h"
#include "transform.h"

namespace pages {

bool decode_diffuse(const std::vector<uint8_t>& body, std::vector<uint8_t>& rgb) {
    MapPage map;
    if (!map.parse(body) || map.stream_size[0] == 0) {
        ModelPage model;
        if (!model.parse(body) || model.stream_size[0] == 0) return false;
        rgb = dct::diffuse_rgb(model.stream[0], model.stream_size[0], model.quality[0]);
        return true;
    }
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
    const int side = static_cast<int>(1u << (file.levels - 1 - level)) * PAGE_PAYLOAD;
    return assemble_diffuse(file, level, 0, 0, side, side);
}

LevelImage assemble_diffuse(PagesFile& file, uint32_t level, int x, int y, int width, int height) {
    if (level >= file.levels)
        throw std::runtime_error("level " + std::to_string(level) + " is out of range (the file has " +
                                 std::to_string(file.levels) + " levels)");
    const int side = static_cast<int>(1u << (file.levels - 1 - level)) * PAGE_PAYLOAD;
    if (width <= 0 || height <= 0 || x < 0 || y < 0 || x + width > side || y + height > side)
        throw std::runtime_error("the rectangle is outside level " + std::to_string(level));
    LevelImage out;
    out.width = width;
    out.height = height;
    out.rgb.assign(static_cast<size_t>(width) * height * 3, 0);

    // Coarser pages are shared by many missing pages below them, so keep them once decoded.
    std::map<const PageEntry*, std::vector<uint8_t>> coarse;
    std::vector<uint8_t> rgb;
    size_t not_map = 0;

    for (int py = y / PAGE_PAYLOAD; py <= (y + height - 1) / PAGE_PAYLOAD; ++py) {
        for (int px = x / PAGE_PAYLOAD; px <= (x + width - 1) / PAGE_PAYLOAD; ++px) {
            // The page itself, or the nearest coarser page that covers it: k levels up, it
            // covers s x s pages of this level.
            const std::vector<uint8_t>* src = nullptr;
            uint32_t k = 0;
            for (; level + k < file.levels; ++k) {
                const PageEntry* p = file.find(static_cast<uint32_t>(px) >> k, static_cast<uint32_t>(py) >> k, level + k);
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
                        std::vector<uint8_t> decoded;
                        if (!decode_diffuse(file.body(*p), decoded)) continue;
                        it = coarse.emplace(p, std::move(decoded)).first;
                    }
                    src = &it->second;
                }
                break;
            }
            if (!src) continue;  // left black
            ++(k == 0 ? out.decoded : out.filled);

            // Nearest-neighbour copy of this page's share of the source payload, clipped to the
            // rectangle.
            const int s = 1 << k;
            const int ox = (px & (s - 1)) * PAGE_PAYLOAD, oy = (py & (s - 1)) * PAGE_PAYLOAD;
            const int x0 = std::max(x, px * PAGE_PAYLOAD), x1 = std::min(x + width, (px + 1) * PAGE_PAYLOAD);
            const int y0 = std::max(y, py * PAGE_PAYLOAD), y1 = std::min(y + height, (py + 1) * PAGE_PAYLOAD);
            for (int ty = y0; ty < y1; ++ty) {
                const size_t sy = PAGE_BORDER + (oy + ty - py * PAGE_PAYLOAD) / s;
                uint8_t* dst = &out.rgb[(static_cast<size_t>(ty - y) * width + (x0 - x)) * 3];
                for (int tx = x0; tx < x1; ++tx, dst += 3) {
                    const size_t sx = PAGE_BORDER + (ox + tx - px * PAGE_PAYLOAD) / s;
                    std::memcpy(dst, &(*src)[(sy * PAGE_SIZE + sx) * 3], 3);
                }
            }
        }
    }
    if (out.decoded == 0 && out.filled == 0)
        throw std::runtime_error(not_map ? "its pages are in neither the map nor the model layout"
                                         : "no pages found for level " + std::to_string(level));
    return out;
}

}  // namespace pages
