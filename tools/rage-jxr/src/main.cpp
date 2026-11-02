// rage-jxr: decode RAGE (2011) megatexture pages.
//
// RAGE's map pages hold four JPEG XR streams (diffuse, normal, specular, power) written by
// id Software's own encoder, which departs from the standard in four places of its adaptive
// entropy coding. Model pages (_vmtr) hold DCT streams coded like baseline JPEG. See
// docs/research/formats/megatexture-pages.md. Only reads files you supply; it writes the images
// you ask for and nothing else.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "dct.h"
#include "entropy.h"
#include "image_out.h"
#include "megatexture.h"
#include "pages.h"
#include "transform.h"

namespace {

const char* USAGE =
    "rage-jxr: decode RAGE megatexture pages (map pages and _vmtr model pages)\n"
    "\n"
    "Usage:\n"
    "  rage-jxr decode <file.pages> <x> <y> <level> [-o out.png] [--layers] [--payload]\n"
    "      Decode one page to PNG. <level> is the level in the file (0 = most detailed).\n"
    "      --layers   also write the normal, specular and power layers, and a model page's mask\n"
    "                 (some scales not yet confirmed)\n"
    "      --payload  crop to the 120x120 payload, without the 4-texel border\n"
    "  rage-jxr level <file.pages> <level> [-o out.png]\n"
    "      Decode the diffuse layer of a whole level into one image (120 texels per page).\n"
    "      Pages the file leaves out are filled from the nearest coarser page.\n"
    "  rage-jxr check <file.pages>\n"
    "      Parse every stream of every page and report whether each ends exactly at its end.\n"
    "  rage-jxr jxr <file.jxr> <out.ppm|out.png> [--rage] [--dump]\n"
    "      Decode a standard JPEG XR file (spatial, one tile, 4:4:4) with jxrlib's RGB output,\n"
    "      for checking the decoder against jxrlib. --rage applies RAGE's coding rules;\n"
    "      --dump prints the low-pass coefficients and block patterns.\n";

struct Args {
    std::vector<std::string> positional;
    std::string out;
    std::vector<std::string> flags;
    bool has(const char* f) const {
        for (const auto& x : flags)
            if (x == f) return true;
        return false;
    }
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 2; i < argc; ++i) {
        std::string s = argv[i];
        if (s == "-o" && i + 1 < argc)
            a.out = argv[++i];
        else if (s.rfind("--", 0) == 0)
            a.flags.push_back(s);
        else
            a.positional.push_back(s);
    }
    return a;
}

uint32_t num(const std::string& s, const char* what) {
    char* end = nullptr;
    unsigned long v = std::strtoul(s.c_str(), &end, 10);
    if (s.empty() || *end) throw std::runtime_error(std::string(what) + " must be a number, got \"" + s + "\"");
    return static_cast<uint32_t>(v);
}

// Entropy-decode and reconstruct one map-page layer.
jxr::Planes decode_layer(const uint8_t* stream, size_t size, int layer, jxr::ColorFormat& cf) {
    int px = pages::MapPage::layer_size(layer);
    jxr::Decoder d(stream, size, jxr::Rules::Rage(), px, px);
    auto mbs = d.decode();
    cf = d.cf;
    // RAGE's pages always use overlap mode 1 and JPEG XR's operators
    return jxr::reconstruct(d.header, mbs, d.mbw, d.mbh, 1, 1);
}

std::vector<uint8_t> crop(const std::vector<uint8_t>& px, int size, int channels, int border, int& inner) {
    inner = size - 2 * border;
    std::vector<uint8_t> out;
    for (int y = border; y < size - border; ++y) {
        auto row = px.begin() + (static_cast<size_t>(y) * size + border) * channels;
        out.insert(out.end(), row, row + static_cast<size_t>(inner) * channels);
    }
    return out;
}

std::string suffixed(const std::string& path, const char* suffix) {
    size_t slash = path.find_last_of("/\\");
    size_t dot = path.find_last_of('.');
    std::string stem = (dot != std::string::npos && (slash == std::string::npos || dot > slash)) ? path.substr(0, dot) : path;
    return stem + "_" + suffix + ".png";
}

// Write a model page's layers (diffuse, and with --layers the others and the mask).
int write_model_page(const Args& a, const pages::ModelPage& m, const std::string& out) {
    auto save = [&](const char* name, std::vector<uint8_t> px, int size, int channels, int border) {
        if (a.has("--payload")) px = crop(px, size, channels, border, size);
        std::string path = std::strcmp(name, "diffuse") == 0 ? out : suffixed(out, name);
        if (!write_png(path, size, size, channels, px)) throw std::runtime_error(path + ": cannot write");
        std::printf("%s %dx%d -> %s\n", name, size, size, path.c_str());
    };
    save("diffuse", dct::diffuse_rgb(m.stream[0], m.stream_size[0], m.quality[0]), 128, 3, 4);
    if (!a.has("--layers")) return 0;
    auto xy = dct::normal_xy(m.stream[1], m.stream_size[1], m.quality[1]);
    std::vector<uint8_t> normal;
    for (size_t i = 0; i < xy.size(); i += 2) normal.insert(normal.end(), {xy[i], xy[i + 1], 0});
    save("normal", normal, 128, 3, 4);
    if (m.mono_specular)
        save("specular", dct::gray(m.stream[2], m.stream_size[2], m.quality[2], 128), 128, 1, 4);
    else
        save("specular", dct::diffuse_rgb(m.stream[2], m.stream_size[2], m.quality[2]), 128, 3, 4);
    save("power", dct::gray(m.stream[3], m.stream_size[3], m.quality[3], 32), 32, 1, 1);
    if (m.stream_size[4]) save("mask", dct::mask(m.stream[4], m.stream_size[4]), 128, 1, 4);
    return 0;
}

int cmd_decode(const Args& a) {
    if (a.positional.size() != 4) throw std::runtime_error(std::string("decode needs <file.pages> <x> <y> <level>\n\n") + USAGE);
    uint32_t x = num(a.positional[1], "x"), y = num(a.positional[2], "y"), level = num(a.positional[3], "level");
    pages::PagesFile file(a.positional[0]);
    const pages::PageEntry* page = file.find(x, y, level);
    if (!page)
        throw std::runtime_error("no page (" + std::to_string(x) + ", " + std::to_string(y) + ") at level " +
                                 std::to_string(level));
    auto body = file.body(*page);
    std::string out = a.out.empty() ? "page_" + std::to_string(x) + "_" + std::to_string(y) + "_" + std::to_string(level) + ".png" : a.out;
    pages::MapPage map;
    if (!map.parse(body)) {
        pages::ModelPage model;
        if (!model.parse(body)) throw std::runtime_error("the page body is in neither the map nor the model layout");
        std::printf("page (%u, %u) level %u: %zu bytes, model layout%s\n", x, y, level, body.size(),
                    model.stream_size[4] ? ", with a mask" : "");
        return write_model_page(a, model, out);
    }
    std::printf("page (%u, %u) level %u: %zu bytes, mode %u%s\n", x, y, level, body.size(), map.mode,
                map.has_mask ? ", with a 128x128 1-bit mask (not decoded)" : "");
    int last = a.has("--layers") ? 3 : 0;
    for (int layer = 0; layer <= last; ++layer) {
        if (map.stream_size[layer] == 0) continue;
        jxr::ColorFormat cf;
        jxr::Planes planes = decode_layer(map.stream[layer], map.stream_size[layer], layer, cf);
        int size = planes.width, channels;
        std::vector<uint8_t> px;
        if (cf == jxr::ColorFormat::Yuv444) {
            px = jxr::rage_ycocg_to_rgb(planes);
            channels = 3;
        } else if (layer == 1) {
            // normal map: the two channels as red and green
            auto r = jxr::rage_plane_to_gray(planes, 0), g = jxr::rage_plane_to_gray(planes, 1);
            for (size_t i = 0; i < r.size(); ++i) px.insert(px.end(), {r[i], g[i], 0});
            channels = 3;
        } else {
            px = jxr::rage_plane_to_gray(planes, 0);
            channels = 1;
        }
        if (a.has("--payload")) px = crop(px, size, channels, layer == 3 ? 1 : 4, size);
        std::string path = layer == 0 ? out : suffixed(out, pages::LAYERS[layer]);
        if (!write_png(path, size, size, channels, px)) throw std::runtime_error(path + ": cannot write");
        std::printf("%s %dx%d -> %s\n", pages::LAYERS[layer], size, size, path.c_str());
    }
    return 0;
}

int cmd_level(const Args& a) {
    if (a.positional.size() != 2) throw std::runtime_error(std::string("level needs <file.pages> <level>\n\n") + USAGE);
    uint32_t level = num(a.positional[1], "level");
    auto start = std::chrono::steady_clock::now();
    pages::PagesFile file(a.positional[0]);
    pages::LevelImage img = pages::assemble_diffuse(file, level);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::string out = a.out.empty() ? "level_" + std::to_string(level) + ".png" : a.out;
    if (!write_png(out, img.width, img.height, 3, img.rgb)) throw std::runtime_error(out + ": cannot write");
    std::printf("level %u of %u: %dx%d, %zu pages decoded, %zu filled from coarser pages, %.1fs -> %s\n", level,
                file.levels, img.width, img.height, img.decoded, img.filled, secs, out.c_str());
    return 0;
}

int cmd_check(const Args& a) {
    if (a.positional.empty()) throw std::runtime_error(std::string("check needs <file.pages>\n\n") + USAGE);
    auto start = std::chrono::steady_clock::now();
    pages::PagesFile file(a.positional[0]);
    size_t ok[4] = {}, total[4] = {}, model_pages = 0, other = 0;
    std::vector<std::string> failures;
    for (const auto& page : file.pages) {
        auto body = file.body(page);
        auto where = [&](int layer) {
            return "page (" + std::to_string(page.x) + ", " + std::to_string(page.y) + ") level " +
                   std::to_string(page.level) + " " + pages::LAYERS[layer] + ": ";
        };
        pages::MapPage map;
        pages::ModelPage model;
        if (!map.parse(body) && model.parse(body)) {
            // DCT streams end on a byte boundary
            ++model_pages;
            for (int layer = 0; layer < 4; ++layer) {
                if (model.stream_size[layer] == 0) continue;
                ++total[layer];
                try {
                    size_t used = 0;
                    const uint8_t* s = model.stream[layer];
                    size_t n = model.stream_size[layer];
                    int q = model.quality[layer];
                    if (layer == 0 || (layer == 2 && !model.mono_specular)) dct::diffuse_rgb(s, n, q, &used);
                    else if (layer == 1) dct::normal_xy(s, n, q, &used);
                    else dct::gray(s, n, q, layer == 3 ? 32 : 128, &used);
                    size_t slack = n * 8 - used;
                    if (slack < 8)
                        ++ok[layer];
                    else
                        failures.push_back(where(layer) + std::to_string(slack) + " bits left over");
                } catch (const std::exception& e) {
                    failures.push_back(where(layer) + e.what());
                }
            }
            continue;
        }
        if (!map.parse(body)) {
            ++other;
            continue;
        }
        for (int layer = 0; layer < 4; ++layer) {
            if (map.stream_size[layer] == 0) continue;
            ++total[layer];
            try {
                int px = pages::MapPage::layer_size(layer);
                jxr::Decoder d(map.stream[layer], map.stream_size[layer], jxr::Rules::Rage(), px, px);
                d.decode();
                long long slack = d.slack();
                if (slack >= 0 && slack < 16)
                    ++ok[layer];
                else
                    failures.push_back(where(layer) + std::to_string(slack) + " bits left over");
            } catch (const std::exception& e) {
                failures.push_back(where(layer) + e.what());
            }
        }
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("%zu pages in %.1fs\n", file.pages.size(), secs);
    for (int layer = 0; layer < 4; ++layer)
        std::printf("  %-8s %zu of %zu streams parse exactly\n", pages::LAYERS[layer], ok[layer], total[layer]);
    if (model_pages) std::printf("  %zu pages are in the model (DCT) layout\n", model_pages);
    if (other) std::printf("  %zu pages are in neither layout\n", other);
    for (size_t i = 0; i < failures.size() && i < 10; ++i) std::printf("  failed: %s\n", failures[i].c_str());
    if (failures.size() > 10) std::printf("  ... and %zu more\n", failures.size() - 10);
    return failures.empty() ? 0 : 1;
}

int cmd_jxr(const Args& a) {
    if (a.positional.size() != 2) throw std::runtime_error(std::string("jxr needs <file.jxr> <out>\n\n") + USAGE);
    std::ifstream in(a.positional[0], std::ios::binary);
    if (!in) throw std::runtime_error(a.positional[0] + ": cannot open");
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    static const uint8_t sig[8] = {'W', 'M', 'P', 'H', 'O', 'T', 'O', 0};
    size_t at = 0;
    while (at + 8 <= data.size() && std::memcmp(&data[at], sig, 8) != 0) ++at;
    if (at + 8 + 12 > data.size()) throw std::runtime_error("no WMPHOTO image header");
    const uint8_t* h = &data[at + 8];
    int subversion = h[0] & 15, overlap = h[1] & 3;
    if (h[1] & 0x80) throw std::runtime_error("tiled images not supported");
    if (h[1] & 0x40) throw std::runtime_error("frequency-mode bitstreams not supported");
    if (h[2] & 0x20) throw std::runtime_error("windowing not supported");
    int w, ht;
    size_t n;
    if (h[2] & 0x80) {
        w = (h[4] << 8 | h[5]) + 1;
        ht = (h[6] << 8 | h[7]) + 1;
        n = 8;
    } else {
        w = static_cast<int>((uint32_t(h[4]) << 24 | h[5] << 16 | h[6] << 8 | h[7]) + 1);
        ht = static_cast<int>((uint32_t(h[8]) << 24 | h[9] << 16 | h[10] << 8 | h[11]) + 1);
        n = 12;
    }
    const uint8_t* code = h + n;
    size_t code_size = data.size() - (at + 8 + n);
    jxr::Decoder d(code, code_size, a.has("--rage") ? jxr::Rules::Rage() : jxr::Rules::Standard(), w, ht);
    auto mbs = d.decode();
    if (a.has("--dump")) std::fputs(jxr::dump(mbs).c_str(), stdout);
    if (d.cf != jxr::ColorFormat::Yuv444) throw std::runtime_error("only 4:4:4 colour files can be written");
    jxr::Planes planes = jxr::reconstruct(d.header, mbs, d.mbw, d.mbh, overlap, subversion);
    auto rgb = jxr::jxr_yuv_to_rgb(planes);
    // the frame is padded to whole macroblocks; keep the image's own size
    std::vector<uint8_t> out;
    for (int y = 0; y < ht; ++y) {
        auto row = rgb.begin() + static_cast<size_t>(y) * planes.width * 3;
        out.insert(out.end(), row, row + static_cast<size_t>(w) * 3);
    }
    const std::string& path = a.positional[1];
    bool png = path.size() > 4 && (path.compare(path.size() - 4, 4, ".png") == 0 || path.compare(path.size() - 4, 4, ".PNG") == 0);
    if (!(png ? write_png(path, w, ht, 3, out) : write_ppm(path, w, ht, out))) throw std::runtime_error(path + ": cannot write");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fputs(USAGE, stderr);
        return 2;
    }
    std::string cmd = argv[1];
    Args a = parse_args(argc, argv);
    try {
        if (cmd == "decode") return cmd_decode(a);
        if (cmd == "level") return cmd_level(a);
        if (cmd == "check") return cmd_check(a);
        if (cmd == "jxr") return cmd_jxr(a);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    std::fputs(USAGE, stderr);
    return 2;
}
