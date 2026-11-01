// JPEG XR entropy decoding and prediction; see entropy.h. Follows jxrlib (BSD-2-Clause,
// Copyright (c) Microsoft Corp.); see LICENSE-jxrlib.txt.
#include "entropy.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace jxr {

// ---------------------------------------------------------------- plane header

static std::vector<uint32_t> read_quantizer(Bits& b, int channels) {
    uint32_t mode = channels > 1 ? b.get(2) : 0;
    uint32_t first = b.get(8);
    if (mode == 0) return std::vector<uint32_t>(channels, first);
    if (mode == 1) {
        std::vector<uint32_t> q(channels, b.get(8));
        q[0] = first;
        return q;
    }
    std::vector<uint32_t> q{first};
    for (int c = 1; c < channels; ++c) q.push_back(b.get(8));
    return q;
}

static PlaneHeader read_plane_header(Bits& b) {
    PlaneHeader h;
    switch (b.get(3)) {
    case 0: h.cf = ColorFormat::YOnly; break;
    case 3: h.cf = ColorFormat::Yuv444; break;
    case 6: h.cf = ColorFormat::NComponent; break;
    default: throw Error("colour format not supported");
    }
    h.scaled = b.get(1) == 1;
    h.subband = b.get(4);
    if (h.cf == ColorFormat::Yuv444) {
        b.get(8);
        h.channels = 3;
    } else if (h.cf == ColorFormat::NComponent) {
        h.channels = static_cast<int>(b.get(4)) + 1;
        b.get(4);
    } else {
        h.channels = 1;
    }
    if (b.get(1) != 1) throw Error("per-tile DC quantizer not supported");
    h.qdc = read_quantizer(b, h.channels);
    if (b.get(1) == 1)
        h.qlp = h.qdc;
    else if (b.get(1) == 1)
        h.qlp = read_quantizer(b, h.channels);
    else
        throw Error("per-tile LP quantizer not supported");
    if (h.subband < 2) {
        if (b.get(1) == 1) {
            h.qhp = h.qlp;
            h.hp_from_lp = true;
        } else if (b.get(1) == 1) {
            h.qhp = read_quantizer(b, h.channels);
        } else {
            throw Error("per-tile HP quantizer not supported");
        }
    }
    b.pos = (b.pos + 7) & ~size_t(7);
    return h;
}

// Skip to the spatial packet start code 00 00 01 xx and past it.
static void find_tile(Bits& b) {
    for (size_t pos = b.pos;; pos += 8) {
        int a = b.byte_at(pos), c = b.byte_at(pos + 8), d = b.byte_at(pos + 16);
        if (a < 0 || c < 0 || d < 0) throw Error("no tile start code");
        if (a == 0 && c == 0 && d == 1) {
            b.pos = pos + 32;
            return;
        }
    }
}

static int32_t qp_value(uint32_t index, uint32_t shift) {
    if (index == 0) return 1;
    if (index < 16) return static_cast<int32_t>(index << shift);
    return static_cast<int32_t>((16 + (index & 0xf)) << (((index >> 4) - 1) + shift));
}

Quantizers quantizers(const PlaneHeader& h) {
    auto band = [&](const std::vector<uint32_t>& idx, bool shifted) {
        std::vector<int32_t> out;
        for (int c = 0; c < h.channels; ++c)
            out.push_back(qp_value(idx[c], c > 0 && shifted ? SHIFTZERO - 1 : SHIFTZERO));
        return out;
    };
    Quantizers q;
    q.dc = band(h.qdc, true);
    q.lp = band(h.qlp, true);
    if (h.qhp.empty())
        q.hp.assign(h.channels, 1);
    else if (h.hp_from_lp)
        q.hp = q.lp;
    else
        q.hp = band(h.qhp, false);
    return q;
}

// ---------------------------------------------------------------- adaptive code tables (adapthuff.c)

static const Table T4[1] = {{{1, 1}, {1, 2}, {0, 3}, {1, 3}}};
static const Table T5[2] = {
    {{1, 1}, {1, 2}, {1, 3}, {0, 4}, {1, 4}},
    {{1, 1}, {0, 3}, {1, 3}, {2, 3}, {3, 3}},
};
static const Table T6[4] = {
    {{1, 1}, {0, 5}, {1, 3}, {1, 5}, {1, 2}, {1, 4}},
    {{1, 2}, {0, 4}, {2, 2}, {1, 4}, {3, 2}, {1, 3}},
    {{0, 4}, {1, 4}, {1, 2}, {2, 2}, {3, 2}, {1, 3}},
    {{0, 5}, {1, 5}, {1, 2}, {1, 1}, {1, 4}, {1, 3}},
};
static const Table T7[2] = {
    {{1, 2}, {2, 2}, {3, 2}, {1, 3}, {1, 4}, {0, 5}, {1, 5}},
    {{1, 1}, {1, 2}, {1, 3}, {1, 4}, {1, 5}, {0, 6}, {1, 6}},
};
static const Table T8[2] = {
    {{2, 2}, {1, 3}, {1, 5}, {1, 4}, {3, 2}, {2, 3}, {0, 5}, {3, 3}},
    {{1, 3}, {2, 3}, {1, 4}, {3, 3}, {4, 3}, {5, 3}, {0, 4}, {3, 2}},
};
static const Table T9[2] = {
    {{2, 3}, {0, 5}, {2, 4}, {1, 5}, {2, 5}, {1, 1}, {3, 3}, {3, 5}, {3, 4}},
    {{1, 1}, {1, 3}, {2, 3}, {1, 4}, {1, 6}, {3, 3}, {1, 5}, {0, 7}, {1, 7}},
};
static const Table T12[5] = {
    {{1, 5}, {1, 6}, {0, 7}, {1, 7}, {4, 5}, {2, 3}, {5, 5}, {1, 1}, {6, 5}, {1, 4}, {7, 5}, {3, 3}},
    {{2, 4}, {2, 5}, {0, 6}, {1, 6}, {3, 4}, {2, 3}, {3, 5}, {3, 2}, {3, 3}, {4, 3}, {1, 5}, {5, 3}},
    {{3, 2}, {1, 3}, {0, 7}, {1, 7}, {1, 5}, {2, 3}, {2, 7}, {3, 3}, {4, 3}, {5, 3}, {3, 7}, {1, 4}},
    {{1, 3}, {3, 2}, {0, 7}, {1, 5}, {2, 5}, {2, 3}, {1, 7}, {3, 3}, {3, 5}, {4, 3}, {1, 6}, {5, 3}},
    {{2, 3}, {1, 1}, {1, 7}, {1, 4}, {2, 7}, {3, 3}, {0, 8}, {2, 4}, {3, 7}, {3, 4}, {1, 8}, {1, 5}},
};
static const int32_t DELTA5[5] = {0, -1, 0, 1, 1};
static const int32_t DELTA6[3][6] = {{-1, 1, 1, 1, 0, 1}, {-2, 0, 0, 2, 0, 0}, {-1, -1, 0, 1, -2, 0}};
static const int32_t DELTA7[7] = {1, 0, -1, -1, -1, -1, -1};
static const int32_t DELTA9[9] = {2, 2, 1, 1, -1, -2, -2, -2, -3};
static const int32_t DELTA12[4][12] = {
    {1, 1, 1, 1, 1, 0, 0, -1, 2, 1, 0, 0},
    {2, 2, -1, -1, -1, 0, -2, -1, 0, 0, -2, -1},
    {-1, 1, 0, 2, 0, 0, 0, 0, -2, 0, 1, 1},
    {0, 1, 0, 1, -2, 0, -1, -1, -2, -1, -2, -2},
};
constexpr int32_t THRESHOLD = 8;
constexpr int32_t MEMORY = 8;

static int max_tables(int n) {
    switch (n) {
    case 4: return 1;
    case 6: return 4;
    case 12: return 5;
    default: return 2;  // 5, 7, 8, 9
    }
}

// AdaptDiscriminant: move to a neighbouring table when the statistics say so.
void Ah::adapt() {
    bool second = n == 6 || n == 12;
    if (!init) {
        init = true;
        disc = disc1 = 0;
        t = second ? 1 : 0;
    }
    int32_t dl = disc, dh = second ? disc1 : disc;
    bool change = false;
    if (dl < lower) {
        --t;
        change = true;
    } else if (dh > upper) {
        ++t;
        change = true;
    }
    if (change) disc = disc1 = 0;
    const int32_t lim = THRESHOLD * MEMORY;
    disc = std::clamp(disc, -lim, lim);
    disc1 = std::clamp(disc1, -lim, lim);
    int mx = max_tables(n);
    lower = t == 0 ? INT32_MIN : -THRESHOLD;
    upper = t == mx - 1 ? (1 << 30) : THRESHOLD;
    // delta rows for the 6- and 12-symbol tables: toward the lower table and the upper one
    int lower_row = std::max(t + (t == 0 ? 1 : 0) - 1, 0);
    int upper_row = std::max(t - (t + 1 == mx ? 1 : 0), 0);
    switch (n) {
    case 4: table = &T4[0]; delta = nullptr; break;
    case 5: table = &T5[t]; delta = DELTA5; break;
    case 6: table = &T6[t]; delta1 = DELTA6[upper_row]; delta = DELTA6[lower_row]; break;
    case 7: table = &T7[t]; delta = DELTA7; break;
    case 8: table = &T8[0]; delta = nullptr; break;
    case 9: table = &T9[t]; delta = DELTA9; break;
    case 12: table = &T12[t]; delta1 = DELTA12[upper_row]; delta = DELTA12[lower_row]; break;
    }
}

int Ah::decode(Bits& b) const {
    uint32_t code = 0;
    for (uint32_t len = 1; len <= 16; ++len) {
        code = (code << 1) | b.bit();
        for (size_t s = 0; s < table->size(); ++s)
            if ((*table)[s].first == code && (*table)[s].second == len) return static_cast<int>(s);
    }
    throw Error("invalid code at bit " + std::to_string(b.pos));
}

// ---------------------------------------------------------------- constants (image.c, strcodec.c)

static const int ZZ_LP[16] = {0, 1, 4, 5, 2, 8, 6, 9, 3, 12, 10, 7, 13, 11, 14, 15};
static const int ZZ_H[16] = {0, 1, 4, 5, 2, 8, 6, 9, 3, 12, 10, 7, 13, 11, 14, 15};
static const int ZZ_V[16] = {0, 4, 8, 5, 1, 12, 9, 6, 2, 13, 3, 15, 7, 10, 14, 11};
static const int DCT_INDEX[16] = {0, 5, 1, 6, 10, 12, 8, 14, 2, 4, 3, 7, 9, 13, 11, 15};
const int DCT_INDEX_DC[16] = {0, 128, 64, 208, 32, 240, 48, 224, 16, 192, 80, 144, 112, 176, 96, 160};
static const int BLK_OFFSET[16] = {0, 64, 16, 80, 128, 192, 144, 208, 32, 96, 48, 112, 160, 224, 176, 240};
static const int32_t RUN_BIN[15] = {-1, -1, -1, -1, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0};
static const uint32_t RUN_FLC[15] = {0, 0, 1, 1, 3, 0, 0, 1, 1, 2, 0, 0, 0, 0, 1};
static const int32_t RUN_REMAP[15] = {1, 2, 3, 5, 7, 1, 2, 3, 5, 7, 1, 2, 3, 4, 5};
static const int ALPHABET[21] = {5, 4, 8, 7, 7, 12, 6, 6, 12, 6, 6, 7, 7, 12, 6, 6, 12, 6, 6, 7, 7};
constexpr int CTDC = 5;
constexpr int CONTEXTX = 8;
constexpr int CBP = 21;  // index of the CBP context in Decoder::ah_
constexpr int CBP1 = 22;
constexpr int32_t MAXTOTAL = 32767;
constexpr int32_t ORIENT_WEIGHT = 4;
constexpr int32_t MODELWEIGHT = 70;
constexpr int BAND_DC = 0, BAND_LP = 1, BAND_AC = 2;
static const int32_t W0[3] = {240, 12, 1};
static const int32_t W1[3][16] = {
    {0, 240, 120, 80, 60, 48, 40, 34, 30, 27, 24, 22, 20, 18, 17, 16},
    {0, 12, 6, 4, 3, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1},
    {0, 16, 8, 5, 4, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 1},
};

Model::Model(int band_, int32_t start, int nch, bool per_channel_) : band(band_), per_channel(per_channel_) {
    int n = per_channel ? nch : 2;
    bits.assign(n, start);
    state.assign(n, 0);
}

// UpdateModelMB. `lmc` holds the nonzero count of each channel.
void Model::update(ColorFormat cf, int nch, const std::vector<int32_t>& lmc) {
    std::vector<int32_t> lm;
    if (per_channel) {
        for (int32_t n : lmc) lm.push_back(n * W0[band]);
    } else {
        int32_t chroma = 0;
        for (size_t c = 1; c < lmc.size(); ++c) chroma += lmc[c];
        chroma *= W1[band][nch - 1];
        if (band == BAND_AC) chroma >>= 4;
        lm = {lmc[0] * W0[band], chroma};
    }
    for (size_t j = 0; j < bits.size(); ++j) {
        int32_t ms = state[j];
        int32_t d = (lm[j] - MODELWEIGHT) >> 2;
        if (d <= -8) {
            d = std::max(d + 4, -16);
            ms += d;
            if (ms < -8) {
                if (bits[j] == 0) {
                    ms = -8;
                } else {
                    ms = 0;
                    --bits[j];
                }
            }
        } else if (d >= 8) {
            d = std::min(d - 4, 15);
            ms += d;
            if (ms > 8) {
                if (bits[j] >= 15) {
                    bits[j] = 15;
                    ms = 8;
                } else {
                    ms = 0;
                    ++bits[j];
                }
            }
        }
        state[j] = ms;
        if (cf == ColorFormat::YOnly) break;
    }
}

void Scan::reset_totals() {
    total[0] = MAXTOTAL;
    for (int k = 1; k < 16; ++k) total[k] = 34 - 2 * k;
}

void Scan::hit(int i) {
    ++total[i];
    if (i > 0 && total[i] > total[i - 1]) {
        std::swap(total[i], total[i - 1]);
        std::swap(scan[i], scan[i - 1]);
    }
}

// ---------------------------------------------------------------- the decoder

Decoder::Decoder(const uint8_t* code, size_t size, Rules rules, int width, int height)
    : rules_(rules), b_(code, size) {
    header = read_plane_header(b_);
    find_tile(b_);
    cf = header.cf;
    nch = header.channels;
    mbw = (width + 15) / 16;
    mbh = (height + 15) / 16;
    qp_hp_ = quantizers(header).hp;
    ncomp_luma_ = rules.ncomp_luma_contexts && cf == ColorFormat::NComponent;
    for (int n : ALPHABET) ah_.push_back(Ah{n});
    ah_.push_back(Ah{cf == ColorFormat::Yuv444 ? 9 : 5});
    ah_.push_back(Ah{5});
    for (int k = 0; k < 16; ++k) {
        scan_lp_.scan[k] = ZZ_LP[k];
        scan_h_.scan[k] = DCT_INDEX[ZZ_H[k]];
        scan_v_.scan[k] = DCT_INDEX[ZZ_V[k]];
    }
    m_ac_ = Model(BAND_AC, 0, nch, rules.channel_models);
    m_lp_ = Model(BAND_LP, 4, nch, rules.channel_models);
    m_dc_ = Model(BAND_DC, 8, nch, rules.channel_models);
    size_t ncbp = rules.channel_cbp_models ? std::max(nch, 2) : 2;
    cbp_c0_.assign(ncbp, -4);
    cbp_c1_.assign(ncbp, 4);
    cbp_st_.assign(ncbp, 0);
    cur_.assign(nch, std::vector<Pred>(mbw));
    prev_.assign(nch, std::vector<Pred>(mbw));
    adapt_lp();
    adapt_hp();
}

void Decoder::adapt_lp() {
    for (int k = 0; k < CTDC + CONTEXTX; ++k) ah_[k].adapt();
}

void Decoder::adapt_hp() {
    ah_[CBP].adapt();
    ah_[CBP1].adapt();
    for (int k = 0; k < CONTEXTX; ++k) ah_[k + CTDC + CONTEXTX].adapt();
}

// ---- symbol helpers

int Decoder::sym(int k) { return ah_[k].decode(b_); }

// getHuff plus the discriminant update.
int Decoder::sym_d(int k) {
    int s = sym(k);
    ah_[k].disc += ah_[k].delta[s];
    return s;
}

int Decoder::sym_d2(int k) {
    int s = sym(k);
    ah_[k].disc += ah_[k].delta[s];
    ah_[k].disc1 += ah_[k].delta1[s];
    return s;
}

int32_t Decoder::abs_level(int k) {
    static const int32_t remap[6] = {2, 3, 4, 6, 10, 14};
    static const uint32_t fixed_len[6] = {0, 0, 1, 2, 2, 2};
    int i = sym_d(k);
    if (i < 2) return i + 2;
    if (i < 6) return remap[i] + static_cast<int32_t>(b_.get(fixed_len[i]));
    uint32_t fixed = b_.get(4) + 4;
    if (fixed == 19) {
        fixed += b_.get(2);
        if (fixed == 22) fixed += b_.get(3);
    }
    return 2 + (1 << fixed) + static_cast<int32_t>(b_.get(fixed));
}

int32_t Decoder::sig_run(int32_t max_run) {
    if (max_run < 5) {
        if (max_run == 1 || b_.bit() == 1) return 1;
        if (max_run == 2 || b_.bit() == 1) return 2;
        if (max_run == 3 || b_.bit() == 1) return 3;
        return 4;
    }
    int idx = sym(0) + RUN_BIN[max_run] * 5;
    return RUN_REMAP[idx] + static_cast<int32_t>(b_.get(RUN_FLC[idx]));
}

int Decoder::index(int32_t loc, int k) {
    if (loc < 15) return sym_d2(k);
    if (loc == 15) {
        if (b_.bit() == 0) return 0;
        if (b_.bit() == 0) return 2;
        return 1 + 2 * static_cast<int>(b_.bit());
    }
    return static_cast<int>(b_.bit());
}

int32_t Decoder::sign() { return -static_cast<int32_t>(b_.bit()); }

// ---- DC

void Decoder::decode_dc(Macroblock& mb) {
    std::vector<int32_t> lm(nch, 0);
    bool flags[3] = {false, false, false};
    if (cf == ColorFormat::Yuv444) {
        int idx = sym(2);
        flags[0] = (idx >> 2) != 0;
        flags[1] = ((idx >> 1) & 1) != 0;
        flags[2] = (idx & 1) != 0;
    }
    for (int i = 0; i < nch; ++i) {
        uint32_t bits = m_dc_.bits_for(i);
        int32_t v = 0;
        bool significant = cf == ColorFormat::Yuv444 ? flags[i] : b_.bit() == 1;
        if (significant) {
            v = abs_level(i == 0 || cf != ColorFormat::Yuv444 ? 3 : 4) - 1;
            ++lm[i];
        }
        if (bits > 0) v = (v << bits) | static_cast<int32_t>(b_.get(bits));
        if (v != 0 && b_.bit() == 1) v = -v;
        mb.dc[i][0] = v;
    }
    if (!(rules_.fixed_dc_ncomp && cf == ColorFormat::NComponent)) m_dc_.update(cf, nch, lm);
}

// ---- low-pass

// DecodeBlock: (run, level) pairs for one 15-coefficient block.
std::vector<std::pair<int32_t, int32_t>> Decoder::decode_block(bool chroma, int offset, int32_t loc) {
    int base = offset + (chroma ? 3 : 0);
    std::vector<std::pair<int32_t, int32_t>> out;
    int idx = sym_d2(base);
    int sr = idx & 1;
    int srn = idx >> 2;
    int cont = sr & srn;
    int32_t s = sign();
    int32_t lev = (idx & 2) ? (abs_level(6 + offset + cont) ^ s) - s : (1 | s);
    int32_t run = 0;
    if (sr == 0) run = sig_run(15 - loc);
    loc += run + 1;
    out.emplace_back(run, lev);
    while (srn != 0) {
        sr = srn & 1;
        run = 0;
        if (sr == 0) run = sig_run(15 - loc);
        loc += run + 1;
        idx = index(loc, base + cont + 1);
        srn = idx >> 1;
        cont &= srn;
        s = sign();
        lev = (idx & 1) ? (abs_level(6 + offset + cont) ^ s) - s : (1 | s);
        out.emplace_back(run, lev);
        if (out.size() > 16) throw Error("low-pass block overrun");
    }
    return out;
}

void Decoder::decode_lp(Macroblock& mb, bool reset_rgi, bool reset_ctx) {
    std::vector<int32_t> lm(nch, 0);
    if (reset_rgi) scan_lp_.reset_totals();
    int32_t cbp = 0;
    if (cf == ColorFormat::Yuv444) {
        int32_t cm = cbp_max_, cz = cbp_zero_;
        const int32_t imax = 3 * 4 - 5;
        if (cz <= 0 || cm < 0) {
            if (b_.bit() == 1) {
                cbp = 1;
                int32_t k = static_cast<int32_t>(b_.get(2));
                if (k != 0) cbp = k * 2 + static_cast<int32_t>(b_.get(1));
            }
            if (cm < cz) cbp = imax - cbp;
        } else {
            cbp = static_cast<int32_t>(b_.get(3));
        }
        cbp_max_ = std::clamp(cm + 1 - 4 * (cbp == imax ? 1 : 0), -8, 7);
        cbp_zero_ = std::clamp(cz + 1 - 4 * (cbp == 0 ? 1 : 0), -8, 7);
    } else {
        for (int c = 0; c < nch; ++c) cbp |= static_cast<int32_t>(b_.bit()) << c;
    }
    for (int c = 0; c < nch; ++c) {
        uint32_t bits = m_lp_.bits_for(c);
        auto& coef = mb.dc[c];
        if (cbp & 1) {
            auto pairs = decode_block(c > 0 && !ncomp_luma_, CTDC, 1);
            lm[c] += static_cast<int32_t>(pairs.size());
            int i = 1;
            for (auto [run, lev] : pairs) {
                i += run;
                if (i > 15) throw Error("low-pass run past the block");
                coef[scan_lp_.scan[i]] = lev;
                scan_lp_.hit(i);
                ++i;
            }
        }
        if (bits > 0) {
            for (int k = 1; k < 16; ++k) {
                int32_t& v = coef[k];
                if (v > 0) {
                    v = (v << bits) + static_cast<int32_t>(b_.get(bits));
                } else if (v < 0) {
                    v = (v << bits) - static_cast<int32_t>(b_.get(bits));
                } else {
                    int32_t r1 = static_cast<int32_t>(b_.peek(bits + 1));
                    int32_t w = (r1 & 1) ? -(r1 >> 1) : (r1 >> 1);
                    b_.pos += bits + (w != 0 ? 1 : 0);
                    v = w;
                }
            }
        }
        cbp >>= 1;
    }
    m_lp_.update(cf, nch, lm);
    if (reset_ctx) adapt_lp();
}

// ---- high-pass

// Shared tail of the block-pattern codes: 1 << n, the six two-block patterns, 0xf ^ (1 << n), 0xf.
int32_t Decoder::four_pattern(int code, int one, int two, int four) {
    if (code == one) return 1 << b_.get(2);
    if (code == two) {
        uint32_t n = b_.get(2);
        if (n == 0) return 3;
        if (n == 1) return 5;
        static const int32_t tab[4] = {6, 9, 10, 12};
        return tab[n * 2 + b_.bit() - 4];
    }
    if (code == two + 1) return 0xf ^ (1 << b_.get(2));
    if (code == four) return 0xf;
    return 0;
}

std::vector<int32_t> Decoder::decode_cbp() {
    static const int OFF[6] = {0, 4, 2, 8, 12, 1};
    static const uint32_t FLC[6] = {0, 2, 1, 2, 2, 0};
    static const int32_t OUT[16] = {0, 15, 3, 12, 1, 2, 4, 8, 5, 6, 9, 10, 7, 11, 13, 14};
    int nloop = cf == ColorFormat::Yuv444 ? 1 : nch;
    std::vector<int32_t> diff(nch, 0);
    for (int i = 0; i < nloop; ++i) {
        int32_t cy = 0, cu = 0, cv = 0;
        int n = sym_d(CBP1);
        int32_t blocks = four_pattern(n, 1, 2, 4);
        for (int blk = 0; blk < 4; ++blk) {
            if (!(blocks & (1 << blk))) continue;
            int s = sym(CBP);
            if (ah_[CBP].delta) ah_[CBP].disc += ah_[CBP].delta[s];
            int val = s + 1;
            int32_t nb = 0;
            if (val >= 6) {
                nb = b_.bit() == 1 ? 0x10 : (b_.bit() == 1 ? 0x20 : 0x30);
                if (val == 9) {
                    if (b_.bit() == 1) {
                    } else if (b_.bit() == 1) {
                        val = 10;
                    } else {
                        val = 11;
                    }
                }
                val -= 6;
            }
            int code1 = OFF[val];
            if (FLC[val] > 0) code1 += static_cast<int>(b_.get(FLC[val]));
            nb += OUT[code1];
            if (cf == ColorFormat::Yuv444) {
                cy |= (nb & 0xf) << (blk * 4);
                for (int k = 0; k < 2; ++k) {
                    if ((nb >> (k + 4)) & 1) {
                        int32_t code = four_pattern(sym(1), 0, 1, 3);
                        (k == 0 ? cu : cv) |= code << (blk * 4);
                    }
                }
            } else {
                cy |= nb << (blk * 4);
            }
        }
        diff[i] = cy;
        if (cf == ColorFormat::Yuv444) {
            diff[1] = cu;
            diff[2] = cv;
        }
    }
    return diff;
}

static int popcount16(int32_t v) {
    int n = 0;
    for (uint32_t x = static_cast<uint32_t>(v) & 0xffff; x; x &= x - 1) ++n;
    return n;
}

std::vector<int32_t> Decoder::pred_cbp(const std::vector<int32_t>& diff, int mbx, int mby) {
    std::vector<int32_t> out;
    for (int c = 0; c < nch; ++c) {
        size_t c1 = rules_.channel_cbp_models ? c : std::min(c, 1);
        int32_t cbp = diff[c];
        if (cbp_st_[c1] == 0) {
            if (mbx == 0) {
                cbp ^= mby == 0 ? 1 : (prev_[c][mbx].cbp >> 10) & 1;
            } else {
                cbp ^= (cur_[c][mbx - 1].cbp >> 5) & 1;
            }
            cbp ^= 0x02 & (cbp << 1);
            cbp ^= 0x10 & (cbp << 3);
            cbp ^= 0x20 & (cbp << 1);
            cbp ^= (cbp & 0x33) << 2;
            cbp ^= (cbp & 0xcc) << 6;
            cbp ^= (cbp & 0x3300) << 2;
        } else if (cbp_st_[c1] == 2) {
            cbp ^= 0xffff;
        }
        int32_t n = popcount16(cbp);
        cbp_c0_[c1] = std::clamp(cbp_c0_[c1] + n - 3, -16, 15);
        cbp_c1_[c1] = std::clamp(cbp_c1_[c1] + 16 - n - 3, -16, 15);
        if (cbp_c0_[c1] < 0)
            cbp_st_[c1] = cbp_c0_[c1] < cbp_c1_[c1] ? 1 : 2;
        else
            cbp_st_[c1] = cbp_c1_[c1] < 0 ? 2 : 0;
        cur_[c][mbx].cbp = cbp;
        out.push_back(cbp);
    }
    return out;
}

int Decoder::decode_block_hp(bool chroma, int32_t qp, std::array<int32_t, 16>& coef, bool vertical) {
    const int offset = CTDC + CONTEXTX;
    const int base = offset + (chroma ? 3 : 0);
    Scan& scan = vertical ? scan_v_ : scan_h_;
    int32_t loc = 1;
    int n = 1;
    int idx = sym_d2(base);
    int sr = idx & 1;
    int srn = idx >> 2;
    int cont = sr & srn;
    int32_t s = sign();
    int32_t lev = (qp ^ s) - s;
    if (idx & 2) lev *= abs_level(6 + offset + cont);
    if (sr == 0) loc += sig_run(15 - loc);
    loc &= 0xf;
    coef[scan.scan[loc]] = lev;
    scan.hit(loc);
    loc = (loc + 1) & 0xf;
    while (srn != 0) {
        sr = srn & 1;
        if (sr == 0) {
            loc += sig_run(15 - loc);
            if (loc >= 16) return 16;
        }
        idx = index(loc + 1, base + cont + 1);
        srn = idx >> 1;
        cont &= srn;
        s = sign();
        lev = (qp ^ s) - s;
        if (idx & 1) lev *= abs_level(6 + offset + cont);
        coef[scan.scan[loc]] = lev;
        scan.hit(loc);
        loc = (loc + 1) & 0xf;
        ++n;
    }
    return n;
}

void Decoder::decode_hp(Macroblock& mb, bool reset_rgi, bool reset_ctx) {
    if (reset_rgi) {
        scan_h_.reset_totals();
        scan_v_.reset_totals();
    }
    auto diff = decode_cbp();
    mb.cbp = pred_cbp(diff, mb.x, mb.y);
    bool vertical = mb.orient == 1;
    std::vector<int32_t> lm(nch, 0);
    for (int i = 0; i < nch; ++i) {
        int32_t cbp = mb.cbp[i];
        bool chroma = i > 0 && !ncomp_luma_;
        int32_t qp = qp_hp_[i] << m_ac_.bits_for(i);
        for (int idx = 0; idx < 16; ++idx) {
            if (cbp & 1) {
                std::array<int32_t, 16> blk{};
                int nz = decode_block_hp(chroma, qp, blk, vertical);
                if (nz > 16) throw Error("high-pass block overrun");
                std::copy(blk.begin(), blk.end(), mb.hp[i].begin() + BLK_OFFSET[idx]);
                lm[i] += nz;
            }
            cbp >>= 1;
        }
    }
    m_ac_.update(cf, nch, lm);
    if (reset_ctx) adapt_hp();
}

// ---- prediction

static int orientation(int32_t sh, int32_t sv) {
    if (sh * ORIENT_WEIGHT < sv) return 1;
    if (sv * ORIENT_WEIGHT < sh) return 0;
    return 2;
}

void Decoder::pred_dcac(Macroblock& mb) {
    const int mbx = mb.x, mby = mb.y;
    int dcm;
    if (mbx == 0 && mby == 0) {
        dcm = 3;
    } else if (mbx == 0) {
        dcm = 1;
    } else if (mby == 0) {
        dcm = 0;
    } else {
        int32_t il = cur_[0][mbx - 1].dc, it = prev_[0][mbx].dc, itl = prev_[0][mbx - 1].dc;
        int32_t sh = std::abs(itl - il), sv = std::abs(itl - it);
        if (cf == ColorFormat::Yuv444) {
            sh *= 2;
            sv *= 2;
            for (int c = 1; c < 3; ++c) {
                int32_t tl = prev_[c][mbx - 1].dc, l = cur_[c][mbx - 1].dc, t = prev_[c][mbx].dc;
                sh += std::abs(tl - l);
                sv += std::abs(tl - t);
            }
        }
        dcm = orientation(sh, sv);
    }
    for (int c = 0; c < nch; ++c) {
        auto& o = mb.dc[c];
        if (dcm == 1)
            o[0] += prev_[c][mbx].dc;
        else if (dcm == 0)
            o[0] += cur_[c][mbx - 1].dc;
        else if (dcm == 2)
            o[0] += (cur_[c][mbx - 1].dc + prev_[c][mbx].dc) >> 1;
        if (dcm == 1) {
            const auto& r = prev_[c][mbx].ad;
            o[4] += r[3];
            o[8] += r[4];
            o[12] += r[5];
        } else if (dcm == 0) {
            const auto& r = cur_[c][mbx - 1].ad;
            o[1] += r[0];
            o[2] += r[1];
            o[3] += r[2];
        }
    }
    const auto& p = mb.dc[0];
    int32_t sh = std::abs(p[1]) + std::abs(p[2]) + std::abs(p[3]);
    int32_t sv = std::abs(p[4]) + std::abs(p[8]) + std::abs(p[12]);
    if (cf == ColorFormat::Yuv444) {
        sh += std::abs(mb.dc[1][1]) + std::abs(mb.dc[2][1]);
        sv += std::abs(mb.dc[1][4]) + std::abs(mb.dc[2][4]);
    }
    mb.orient = 2 - orientation(sh, sv);
}

void Decoder::pred_ac(Macroblock& mb) const {
    int acm = 2 - mb.orient;
    for (auto& s : mb.hp) {
        if (acm == 1) {
            for (int j : {1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15})
                for (int k : {2, 10, 9}) s[16 * j + k] += s[16 * j - 16 + k];
        } else if (acm == 0) {
            for (int o = 64; o < 256; o += 16)
                for (int k : {1, 5, 6}) s[o + k] += s[o - 64 + k];
        }
    }
}

void Decoder::update_pred(const Macroblock& mb) {
    for (int c = 0; c < nch; ++c) {
        const auto& p = mb.dc[c];
        Pred& e = cur_[c][mb.x];
        e.dc = p[0];
        e.ad = {p[1], p[2], p[3], p[4], p[8], p[12]};
    }
}

// ---- frame

std::vector<Macroblock> Decoder::decode() {
    std::vector<Macroblock> mbs;
    mbs.reserve(static_cast<size_t>(mbw) * mbh);
    for (int mby = 0; mby < mbh; ++mby) {
        for (int mbx = 0; mbx < mbw; ++mbx) {
            bool reset_rgi = (mbx & 0xf) == 0;
            bool reset_ctx = reset_rgi || mbx + 1 == mbw;
            Macroblock mb;
            mb.x = mbx;
            mb.y = mby;
            mb.dc.assign(nch, {});
            mb.hp.assign(nch, {});
            decode_dc(mb);
            decode_lp(mb, reset_rgi, reset_ctx);
            pred_dcac(mb);
            if (header.subband < 2) {
                decode_hp(mb, reset_rgi, reset_ctx);
                pred_ac(mb);
            }
            if (b_.overrun())
                throw Error("ran past the end of the stream in macroblock (" + std::to_string(mbx) + ", " +
                            std::to_string(mby) + ")");
            update_pred(mb);
            mbs.push_back(std::move(mb));
        }
        std::swap(cur_, prev_);
    }
    return mbs;
}

std::string dump(const std::vector<Macroblock>& mbs) {
    std::string s;
    char buf[64];
    for (const auto& mb : mbs) {
        for (size_t c = 0; c < mb.dc.size(); ++c) {
            s += "LP " + std::to_string(mb.x) + " " + std::to_string(mb.y) + " " + std::to_string(c) + ":";
            for (int32_t v : mb.dc[c]) s += " " + std::to_string(v);
            s += "\n";
        }
        if (!mb.cbp.empty()) {
            int32_t cb[3] = {0, 0, 0};
            for (size_t c = 0; c < mb.cbp.size() && c < 3; ++c) cb[c] = mb.cbp[c];
            snprintf(buf, sizeof buf, "CBP %d %d: %x %x %x\n", mb.x, mb.y, static_cast<unsigned>(cb[0]),
                     static_cast<unsigned>(cb[1]), static_cast<unsigned>(cb[2]));
            s += buf;
        }
    }
    return s;
}

}  // namespace jxr
