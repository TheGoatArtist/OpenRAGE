// Reading pages out of a megatexture .pages file (docs/research/formats/megatexture-pages.md).
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace pages {

struct PageEntry {
    uint64_t offset = 0, size = 0;
    uint32_t x = 0, y = 0, level = 0;
};

class PagesFile {
public:
    // Open a .pages file and walk its page tree (headers only), in file order. Throws on error.
    explicit PagesFile(const std::string& path);
    ~PagesFile();
    PagesFile(const PagesFile&) = delete;
    PagesFile& operator=(const PagesFile&) = delete;

    const PageEntry* find(uint32_t x, uint32_t y, uint32_t level) const;
    // The page's compressed body (everything after its 40-byte header).
    std::vector<uint8_t> body(const PageEntry& page);

    std::vector<PageEntry> pages;
    // Number of levels: the top page is at level `levels - 1` and is 1 page across; level L is
    // 2^(levels - 1 - L) pages across.
    uint32_t levels = 0;

private:
    std::FILE* file_ = nullptr;
    std::unordered_map<uint64_t, size_t> index_;  // (level, y, x) -> position in `pages`
    void read_at(uint64_t offset, void* dst, size_t n);
};

extern const char* const LAYERS[4];

// A page body in the map layout: four JPEG XR layer streams and an optional mask.
struct MapPage {
    uint16_t mode = 0;
    const uint8_t* stream[4] = {};
    size_t stream_size[4] = {};
    bool has_mask = false;

    // Returns false when the body isn't in the map layout (model pages use another codec).
    bool parse(const std::vector<uint8_t>& body);
    // Pixel size of a layer: the power layer is 32x32, the others 128x128.
    static int layer_size(int layer) { return layer == 3 ? 32 : 128; }
};

}  // namespace pages
