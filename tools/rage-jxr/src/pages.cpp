#include "pages.h"

#include <functional>
#include <queue>
#include <stdexcept>
#include <tuple>

namespace pages {

namespace {

constexpr uint32_t FILE_MAGIC = 0x77339904;
constexpr uint32_t PAGE_MAGIC = 0xCABFED04;
constexpr uint64_t ROOT_OFFSET = 0x20;
constexpr size_t PAGE_HEADER = 40;
constexpr uint64_t UNIT = 2;  // child offsets and sizes count 2-byte units

uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }

uint64_t key(uint32_t x, uint32_t y, uint32_t level) {
    return static_cast<uint64_t>(level) << 32 | static_cast<uint64_t>(y) << 16 | x;
}

// (offset, size) of each child named in a page header.
std::vector<std::pair<uint64_t, uint64_t>> children(const uint8_t* h) {
    uint32_t s01 = le32(h + 24), s23 = le32(h + 28);
    uint32_t sizes[4] = {s01 & 0xFFFF, s01 >> 16, s23 & 0xFFFF, s23 >> 16};
    std::vector<std::pair<uint64_t, uint64_t>> out;
    for (int q = 0; q < 4; ++q)
        if (sizes[q]) out.emplace_back(le32(h + 8 + 4 * q) * UNIT, sizes[q] * UNIT);
    return out;
}

int seek64(std::FILE* f, uint64_t offset) {
#ifdef _WIN32
    return _fseeki64(f, static_cast<long long>(offset), SEEK_SET);
#else
    return fseeko(f, static_cast<off_t>(offset), SEEK_SET);
#endif
}

}  // namespace

const char* const LAYERS[4] = {"diffuse", "normal", "specular", "power"};

void PagesFile::read_at(uint64_t offset, void* dst, size_t n) {
    if (seek64(file_, offset) != 0 || std::fread(dst, 1, n, file_) != n)
        throw std::runtime_error("read failed at byte " + std::to_string(offset));
}

PagesFile::PagesFile(const std::string& path) {
    file_ = std::fopen(path.c_str(), "rb");
    if (!file_) throw std::runtime_error(path + ": cannot open");
    uint8_t head[32];
    read_at(0, head, sizeof head);
    if (le32(head) != FILE_MAGIC) throw std::runtime_error(path + ": not a .pages file");
    std::fseek(file_, 0, SEEK_END);
#ifdef _WIN32
    uint64_t len = static_cast<uint64_t>(_ftelli64(file_));
#else
    uint64_t len = static_cast<uint64_t>(ftello(file_));
#endif
    uint8_t h[PAGE_HEADER];
    read_at(ROOT_OFFSET, h, PAGE_HEADER);
    using Item = std::pair<uint64_t, uint64_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pending;
    for (auto& c : children(h)) pending.push(c);
    while (!pending.empty()) {
        auto [offset, size] = pending.top();
        pending.pop();
        if (offset + PAGE_HEADER > len) continue;
        read_at(offset, h, PAGE_HEADER);
        if (le32(h) != PAGE_MAGIC) continue;
        uint32_t xy = le32(h + 32);
        pages.push_back({offset, size, xy & 0xFFFF, xy >> 16, le32(h + 36)});
        for (auto& c : children(h)) pending.push(c);
    }
    for (size_t i = 0; i < pages.size(); ++i) {
        index_.emplace(key(pages[i].x, pages[i].y, pages[i].level), i);
        if (pages[i].level + 1 > levels) levels = pages[i].level + 1;
    }
}

PagesFile::~PagesFile() {
    if (file_) std::fclose(file_);
}

const PageEntry* PagesFile::find(uint32_t x, uint32_t y, uint32_t level) const {
    auto it = index_.find(key(x, y, level));
    return it == index_.end() ? nullptr : &pages[it->second];
}

std::vector<uint8_t> PagesFile::body(const PageEntry& page) {
    std::vector<uint8_t> buf(page.size > PAGE_HEADER ? page.size - PAGE_HEADER : 0);
    if (!buf.empty()) read_at(page.offset + PAGE_HEADER, buf.data(), buf.size());
    return buf;
}

bool MapPage::parse(const std::vector<uint8_t>& body) {
    if (body.size() < 16) return false;
    mode = static_cast<uint16_t>(body[4] | body[5] << 8);
    size_t pos = 16;
    for (int i = 0; i < 4; ++i) {
        stream_size[i] = static_cast<size_t>(body[6 + 2 * i]) << 8 | body[7 + 2 * i];
        pos += stream_size[i];
    }
    size_t mask_len = (mode & 8) ? 2048 : 0;
    size_t padded = (pos + 1) & ~size_t(1);
    if (pos > body.size() || (padded + mask_len != body.size() && pos + mask_len != body.size())) return false;
    pos = 16;
    for (int i = 0; i < 4; ++i) {
        stream[i] = body.data() + pos;
        pos += stream_size[i];
    }
    has_mask = mask_len > 0;
    return true;
}

}  // namespace pages
