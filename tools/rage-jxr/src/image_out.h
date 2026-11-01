// Minimal PNG and PPM writers (uncompressed deflate), so the tool needs no dependencies.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// `channels` is 1 (grey) or 3 (RGB); `pixels` is row-major, 8 bits per sample.
bool write_png(const std::string& path, int width, int height, int channels, const std::vector<uint8_t>& pixels);
bool write_ppm(const std::string& path, int width, int height, const std::vector<uint8_t>& rgb);
