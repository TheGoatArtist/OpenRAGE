// JPEG XR codestream parsing: plane header, adaptive entropy decoding, prediction.
//
// Covers what RAGE's map pages use: spatial bitstream, one tile, Y_ONLY / YUV 4:4:4 /
// N-component planes. The structure follows Microsoft's jxrlib (segdec.c, strdec.c,
// adapthuff.c, strPredQuant*.c, image.c), BSD-2-Clause, Copyright (c) Microsoft Corp.;
// see LICENSE-jxrlib.txt. RAGE's departures from the standard are the switches in Rules.
#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "bits.h"

namespace jxr {

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

enum class ColorFormat { YOnly, Yuv444, NComponent };

// How the adaptive coding behaves. Standard() is JPEG XR; Rage() is what id's encoder wrote.
struct Rules {
    bool channel_models;       // every channel has its own bit-precision model, with luma weights
    bool channel_cbp_models;   // every channel has its own coded-block-pattern model
    bool fixed_dc_ncomp;       // N-component streams never update the DC model
    bool ncomp_luma_contexts;  // N-component streams code every channel with the luma contexts

    static Rules Standard() { return {false, false, false, false}; }
    static Rules Rage() { return {true, true, true, true}; }
};

struct PlaneHeader {
    ColorFormat cf = ColorFormat::YOnly;
    bool scaled = false;
    uint32_t subband = 0;  // 0 all, 1 no flexbits, 2 no high-pass, 3 DC only
    int channels = 1;
    std::vector<uint32_t> qdc, qlp, qhp;  // qhp empty when there is no high-pass band
    bool hp_from_lp = false;
};

constexpr uint32_t SHIFTZERO = 1;
constexpr uint32_t QPFRACBITS = 2;

// Scaled-arithmetic quantizer values per channel (remapQP / formatQuantizer).
struct Quantizers {
    std::vector<int32_t> dc, lp, hp;
};
Quantizers quantizers(const PlaneHeader& h);

extern const int DCT_INDEX_DC[16];

// One decoded macroblock: per channel, the 16 DC/low-pass values and the 256 high-pass values
// in jxrlib's buffer layout (block (bx, by) at 64*bx + 16*by).
struct Macroblock {
    int x = 0, y = 0;
    std::vector<std::array<int32_t, 16>> dc;
    std::vector<std::array<int32_t, 256>> hp;
    std::vector<int32_t> cbp;
    int orient = 0;
};

using Table = std::vector<std::pair<uint32_t, uint32_t>>;  // (code, length) per symbol

// One adaptive Huffman context (CAdaptiveHuffman).
struct Ah {
    int n = 0;
    bool init = false;
    int32_t disc = 0, disc1 = 0, lower = 0, upper = 0;
    int t = 0;
    const Table* table = nullptr;
    const int32_t* delta = nullptr;
    const int32_t* delta1 = nullptr;

    void adapt();
    int decode(Bits& b) const;
};

// Bit-precision model for one band (CAdaptiveModel).
struct Model {
    int band = 0;
    bool per_channel = false;
    std::vector<int32_t> bits, state;

    Model() = default;
    Model(int band, int32_t start, int nch, bool per_channel);
    int slot(int c) const { return per_channel ? c : (c < 1 ? c : 1); }
    uint32_t bits_for(int c) const { return static_cast<uint32_t>(bits[slot(c)]); }
    void update(ColorFormat cf, int nch, const std::vector<int32_t>& lmc);
};

// Adaptive scan order (CAdaptiveScan).
struct Scan {
    std::array<int, 16> scan{};
    std::array<int32_t, 16> total{};
    void reset_totals();
    void hit(int i);
};

struct Pred {
    int32_t dc = 0;
    std::array<int32_t, 6> ad{};
    int32_t cbp = 0;
};

class Decoder {
public:
    // `code` starts at the image-plane header (as RAGE stores it).
    Decoder(const uint8_t* code, size_t size, Rules rules, int width, int height);
    std::vector<Macroblock> decode();
    // Bits left between the parse position and the end of the stream (negative = overran).
    long long slack() const { return static_cast<long long>(b_.len_bits()) - static_cast<long long>(b_.pos); }

    PlaneHeader header;
    ColorFormat cf;
    int nch, mbw, mbh;

private:
    Rules rules_;
    Bits b_;
    std::vector<int32_t> qp_hp_;
    bool ncomp_luma_;
    std::vector<Ah> ah_;
    Scan scan_lp_, scan_h_, scan_v_;
    Model m_ac_, m_lp_, m_dc_;
    int32_t cbp_max_ = 1, cbp_zero_ = 1;
    std::vector<int32_t> cbp_c0_, cbp_c1_, cbp_st_;
    std::vector<std::vector<Pred>> cur_, prev_;

    void adapt_lp();
    void adapt_hp();
    int sym(int k);
    int sym_d(int k);
    int sym_d2(int k);
    int32_t abs_level(int k);
    int32_t sig_run(int32_t max_run);
    int index(int32_t loc, int k);
    int32_t sign();
    void decode_dc(Macroblock& mb);
    std::vector<std::pair<int32_t, int32_t>> decode_block(bool chroma, int offset, int32_t loc);
    void decode_lp(Macroblock& mb, bool reset_rgi, bool reset_ctx);
    int32_t four_pattern(int code, int one, int two, int four);
    std::vector<int32_t> decode_cbp();
    std::vector<int32_t> pred_cbp(const std::vector<int32_t>& diff, int mbx, int mby);
    int decode_block_hp(bool chroma, int32_t qp, std::array<int32_t, 16>& coef, bool vertical);
    void decode_hp(Macroblock& mb, bool reset_rgi, bool reset_ctx);
    void pred_dcac(Macroblock& mb);
    void pred_ac(Macroblock& mb) const;
    void update_pred(const Macroblock& mb);
};

// The coefficient dump format of the jxrlib experiment build, for cross-checking.
std::string dump(const std::vector<Macroblock>& mbs);

}  // namespace jxr
