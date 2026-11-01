#include "image_out.h"

#include <cstdio>

namespace {

uint32_t crc32(const uint8_t* data, size_t n) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < n; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
    }
    return ~crc;
}

uint32_t adler32(const std::vector<uint8_t>& data) {
    uint32_t a = 1, b = 0;
    for (uint8_t x : data) {
        a = (a + x) % 65521;
        b = (b + a) % 65521;
    }
    return b << 16 | a;
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<uint8_t>(v >> s));
}

void chunk(std::vector<uint8_t>& out, const char* kind, const std::vector<uint8_t>& data) {
    put32(out, static_cast<uint32_t>(data.size()));
    size_t start = out.size();
    out.insert(out.end(), kind, kind + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32(out, crc32(out.data() + start, out.size() - start));
}

bool write_all(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    return std::fclose(f) == 0 && ok;
}

}  // namespace

bool write_png(const std::string& path, int width, int height, int channels, const std::vector<uint8_t>& pixels) {
    const size_t stride = static_cast<size_t>(width) * channels;
    std::vector<uint8_t> raw;
    raw.reserve((stride + 1) * height);
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), pixels.begin() + y * stride, pixels.begin() + (y + 1) * stride);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    for (size_t pos = 0; pos < raw.size() || raw.empty(); pos += 65535) {
        size_t n = raw.size() - pos < 65535 ? raw.size() - pos : 65535;
        z.push_back(pos + n >= raw.size() ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n));
        z.push_back(static_cast<uint8_t>(~n >> 8));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        if (raw.empty()) break;
    }
    put32(z, adler32(raw));
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<uint8_t> ihdr;
    put32(ihdr, static_cast<uint32_t>(width));
    put32(ihdr, static_cast<uint32_t>(height));
    ihdr.insert(ihdr.end(), {8, static_cast<uint8_t>(channels == 3 ? 2 : 0), 0, 0, 0});
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return write_all(path, out);
}

bool write_ppm(const std::string& path, int width, int height, const std::vector<uint8_t>& rgb) {
    std::string head = "P6\n" + std::to_string(width) + " " + std::to_string(height) + "\n255\n";
    std::vector<uint8_t> bytes(head.begin(), head.end());
    bytes.insert(bytes.end(), rgb.begin(), rgb.end());
    return write_all(path, bytes);
}
