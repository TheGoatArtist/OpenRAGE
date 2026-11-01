// JPEG XR inverse transform; see transform.h. Follows jxrlib's strInvTransform.c (BSD-2-Clause,
// Copyright (c) Microsoft Corp.); see LICENSE-jxrlib.txt.
//
// It keeps jxrlib's macroblock buffer layout so the pointer offsets port unchanged: each row
// buffer holds 256 values per macroblock, block (bx, by) at 64*bx + 16*by, and the 16 pixels of
// a block in the order 0 1 5 4 / 2 3 7 6 / 10 11 15 14 / 8 9 13 12 (rows). Each channel's buffer
// holds two macroblock rows (previous and current); p0 and p1 are offsets into it.
#include "transform.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <tuple>
#include <utility>

namespace jxr {

using Buf = std::vector<int32_t>;
using P = std::ptrdiff_t;

// ---------------------------------------------------------------- primitives

static void dct2x2dn(Buf& x, P pa, P pb, P pc, P pd) {
    int32_t a = x[pa], b = x[pb], cc = x[pc], d = x[pd];
    a += d;
    b -= cc;
    int32_t t = (a - b) >> 1;
    int32_t c = t - d;
    d = t - cc;
    a -= d;
    b += c;
    x[pa] = a, x[pb] = b, x[pc] = c, x[pd] = d;
}

static void dct2x2up(Buf& x, P pa, P pb, P pc, P pd) {
    int32_t a = x[pa], b = x[pb], cc = x[pc], d = x[pd];
    a += d;
    b -= cc;
    int32_t t = (a - b + 1) >> 1;
    int32_t c = t - d;
    d = t - cc;
    a -= d;
    b += c;
    x[pa] = a, x[pb] = b, x[pc] = c, x[pd] = d;
}

static void irotate1(Buf& x, P pa, P pb) {
    int32_t a = x[pa], b = x[pb];
    a -= (b + 1) >> 1;
    b += (a + 1) >> 1;
    x[pa] = a, x[pb] = b;
}

static void inv_odd(Buf& x, P pa, P pb, P pc, P pd) {
    int32_t a = x[pa], b = x[pb], c = x[pc], d = x[pd];
    b += d;
    a -= c;
    d -= b >> 1;
    c += (a + 1) >> 1;
    a -= (b * 3 + 4) >> 3;
    b += (a * 3 + 4) >> 3;
    c -= (d * 3 + 4) >> 3;
    d += (c * 3 + 4) >> 3;
    c -= (b + 1) >> 1;
    d = ((a + 1) >> 1) - d;
    b += c;
    a -= d;
    x[pa] = a, x[pb] = b, x[pc] = c, x[pd] = d;
}

static void inv_odd_odd(Buf& x, P pa, P pb, P pc, P pd) {
    int32_t a = x[pa], b = x[pb], c = x[pc], d = x[pd];
    d += a;
    c -= b;
    int32_t t1 = d >> 1;
    a -= t1;
    int32_t t2 = c >> 1;
    b += t2;
    a -= (b * 3 + 3) >> 3;
    b += (a * 3 + 3) >> 2;
    a -= (b * 3 + 4) >> 3;
    b -= t2;
    a += t1;
    c += b;
    d -= a;
    x[pa] = a, x[pb] = -b, x[pc] = -c, x[pd] = d;
}

static void inv_odd_odd_post(Buf& x, P pa, P pb, P pc, P pd) {
    int32_t a = x[pa], b = x[pb], c = x[pc], d = x[pd];
    d += a;
    c -= b;
    int32_t t1 = d >> 1;
    a -= t1;
    int32_t t2 = c >> 1;
    b += t2;
    a -= (b * 3 + 6) >> 3;
    b += (a * 3 + 2) >> 2;
    a -= (b * 3 + 4) >> 3;
    b -= t2;
    a += t1;
    c += b;
    d -= a;
    x[pa] = a, x[pb] = b, x[pc] = c, x[pd] = d;
}

static void hst1(Buf& x, P pa, P pd, bool alt) {
    int32_t a = x[pa], d = x[pd];
    a += d;
    d = (a >> 1) - d;
    a += (d * 3) >> 3;
    d += (a * 3) >> 4;
    if (alt) {
        d += a >> 7;
        d -= a >> 10;
    }
    x[pa] = a, x[pd] = d;
}

static std::pair<int32_t, int32_t> hst1_edge(int32_t a, int32_t d) {
    a += d;
    d = (a >> 1) - d;
    a += (d * 3) >> 3;
    d += (a * 3) >> 4;
    d += a >> 7;
    d -= a >> 10;
    a += (d * 3 + 4) >> 3;
    d -= a >> 1;
    a += d;
    return {a, -d};
}

static void hst(Buf& x, P pa, P pb, P pc, P pd) {
    int32_t a = x[pa], b = x[pb], c = x[pc], d = x[pd];
    b -= c;
    a += (d * 3 + 4) >> 3;
    d -= b >> 1;
    c = ((a - b) >> 1) - c;
    x[pa] = a - c, x[pb] = b + d, x[pc] = d, x[pd] = c;
}

// strPost4 (HD Photo operators) or strPost4_alternate (JPEG XR operators).
static void post4(Buf& x, P pa, P pb, P pc, P pd, bool alt) {
    int32_t a = x[pa], b = x[pb], c = x[pc], d = x[pd];
    a += d;
    b += c;
    d -= (a + 1) >> 1;
    c -= (b + 1) >> 1;
    if (alt) {
        std::tie(a, d) = hst1_edge(a, d);
        std::tie(b, c) = hst1_edge(b, c);
    }
    c -= (d + 1) >> 1;
    d += (c + 1) >> 1;
    d += (a + 1) >> 1;
    c += (b + 1) >> 1;
    if (alt) {
        a -= d;
        b -= c;
    } else {
        a -= d - ((d * 3 + 16) >> 5);
        b -= c - ((c * 3 + 16) >> 5);
        d += (a * 3 + 8) >> 4;
        c += (b * 3 + 8) >> 4;
        a += (d * 3 + 16) >> 5;
        b += (c * 3 + 16) >> 5;
    }
    x[pa] = a, x[pb] = b, x[pc] = c, x[pd] = d;
}

static void idct_stage1(Buf& x, P p) {
    dct2x2up(x, p, p + 1, p + 2, p + 3);
    inv_odd(x, p + 5, p + 4, p + 7, p + 6);
    inv_odd(x, p + 10, p + 8, p + 11, p + 9);
    inv_odd_odd(x, p + 15, p + 14, p + 13, p + 12);
    for (P k = 0; k < 4; ++k) dct2x2dn(x, p + k, p + 4 + k, p + 8 + k, p + 12 + k);
}

static void idct_stage2(Buf& x, P p) {
    inv_odd(x, p + 32, p + 48, p + 96, p + 112);
    inv_odd(x, p + 128, p + 192, p + 144, p + 208);
    inv_odd_odd(x, p + 160, p + 224, p + 176, p + 240);
    dct2x2up(x, p, p + 64, p + 16, p + 80);
    static const P O[16] = {0, 192, 48, 240, 64, 128, 112, 176, 16, 208, 32, 224, 80, 144, 96, 160};
    for (int k = 0; k < 4; ++k) dct2x2dn(x, p + O[4 * k], p + O[4 * k + 1], p + O[4 * k + 2], p + O[4 * k + 3]);
}

static int32_t clip_dcl(int32_t dcl, int32_t alt) {
    if (dcl > 0) return alt > 0 ? std::min(dcl, alt) : 0;
    if (dcl < 0) return alt < 0 ? std::max(dcl, alt) : 0;
    return 0;
}

// strPost4x4Stage1Split (with DC leakage compensation) or its _alternate version.
static void post4x4_stage1_split(Buf& x, P p0, P p1, P off, int32_t hpqp, bool hp_absent, bool alt) {
    P p2 = p0 + 72 - off;
    P p3 = p1 + 64 - off;
    p0 += 12;
    p1 += 4;
    for (P k = 0; k < 4; ++k) dct2x2dn(x, p0 + k, p2 + k, p1 + k, p3 + k);
    inv_odd_odd_post(x, p3, p3 + 1, p3 + 2, p3 + 3);
    irotate1(x, p1 + 2, p1 + 3);
    irotate1(x, p1, p1 + 1);
    irotate1(x, p2 + 1, p2 + 3);
    irotate1(x, p2, p2 + 2);
    for (P k = 0; k < 4; ++k) hst1(x, p0 + k, p3 + k, alt);
    for (P k = 0; k < 4; ++k) hst(x, p0 + k, p2 + k, p1 + k, p3 + k);
    if (alt) return;
    for (P k = 0; k < 4; ++k) {
        int32_t a = x[p0 + k], b = x[p1 + k], c = x[p2 + k], d = x[p3 + k];
        int32_t tmp = (a + b + c + d) >> 1;
        int32_t dcl = (tmp * 595 + 65536) >> 17;
        if ((std::abs(dcl) < hpqp && hpqp > 20) || hp_absent) {
            dcl = clip_dcl(dcl, (a - b - c + d) >> 1) >> 1;
            // DCCompensate(p0, p2, p1, p3)
            x[p0 + k] -= dcl;
            x[p3 + k] -= dcl;
            x[p2 + k] += dcl;
            x[p1 + k] += dcl;
        }
    }
}

static void post4x4_stage1(Buf& x, P p, int32_t hpqp, bool hp_absent, bool alt) {
    post4x4_stage1_split(x, p, p + 16, 0, hpqp, hp_absent, alt);
}

static void post4x4_stage2_split(Buf& x, P p0, P p1, bool alt) {
    dct2x2dn(x, p0 - 96, p0 + 96, p1 - 112, p1 + 80);
    dct2x2dn(x, p0 - 32, p0 + 32, p1 - 48, p1 + 16);
    dct2x2dn(x, p0 - 80, p0 + 112, p1 - 128, p1 + 64);
    dct2x2dn(x, p0 - 16, p0 + 48, p1 - 64, p1);
    inv_odd_odd_post(x, p1, p1 + 64, p1 + 16, p1 + 80);
    irotate1(x, p0 + 48, p0 + 32);
    irotate1(x, p0 + 112, p0 + 96);
    irotate1(x, p1 - 64, p1 - 128);
    irotate1(x, p1 - 48, p1 - 112);
    hst1(x, p0 - 96, p1 + 80, alt);
    hst1(x, p0 - 32, p1 + 16, alt);
    hst1(x, p0 - 80, p1 + 64, alt);
    hst1(x, p0 - 16, p1, alt);
    hst(x, p0 - 96, p1 - 112, p0 + 96, p1 + 80);
    hst(x, p0 - 32, p1 - 48, p0 + 32, p1 + 16);
    hst(x, p0 - 80, p1 - 128, p0 + 112, p1 + 64);
    hst(x, p0 - 16, p1 - 64, p0 + 48, p1);
}

// ---------------------------------------------------------------- macroblock drivers

struct Edges {
    bool left, right, top, bottom;
};

struct Stage {
    int overlap;
    bool scaled, chroma;
    int32_t hpqp;
    bool hp_absent;
};

static void stage1_idct(Buf& x, P p0, P p1, Edges e) {
    if (!e.top)
        for (P j = e.left ? 32 : -96; j < (e.right ? 32 : 160); j += 64) {
            idct_stage1(x, p0 + j);
            idct_stage1(x, p0 + j + 16);
        }
    if (!e.bottom)
        for (P j = e.left ? 0 : -128; j < (e.right ? 0 : 128); j += 64) {
            idct_stage1(x, p1 + j);
            idct_stage1(x, p1 + j + 16);
        }
}

// The stage-1 overlap loop runs over these offsets.
static P row_start(Edges e) { return e.left ? 0 : -192; }
static P row_end(Edges e) { return e.right ? -64 : 64; }

// strNormalizeDec: chroma DC values were coded at half scale.
static void normalize(Buf& x, P p, const Stage& s) {
    if (s.scaled && s.chroma)
        for (P i = 0; i < 256; i += 16) x[p + i] += x[p + i];
}

// invTransformMacroblock: HD Photo 1.0 operators (codec subversion 0).
static void inv_mb_hdphoto(Buf& x, P p0, P p1, Edges e, const Stage& s) {
    bool tb = e.top || e.bottom, lr = e.left || e.right;
    if (!(e.bottom || e.right)) {
        idct_stage2(x, p1);
        normalize(x, p1, s);
    }
    if (s.overlap == 2) {
        if (lr && !tb) {
            P j = e.left ? 0 : -128;
            post4(x, p0 + j + 32, p0 + j + 48, p1 + j, p1 + j + 16, false);
            post4(x, p0 + j + 96, p0 + j + 112, p1 + j + 64, p1 + j + 80, false);
        }
        if (!lr) {
            if (tb) {
                P p = e.top ? p1 : p0 + 32;
                post4(x, p - 128, p - 64, p, p + 64, false);
                post4(x, p - 112, p - 48, p + 16, p + 80, false);
            } else {
                post4x4_stage2_split(x, p0, p1, false);
            }
        }
    }
    stage1_idct(x, p0, p1, e);
    if (s.overlap == 0) return;
    if (lr) {
        P j = e.left ? 10 : -64 + 14;
        if (!e.top) {
            P p = p0 + 16 + j;
            post4(x, p, p - 2, p + 6, p + 8, false);
            post4(x, p + 1, p - 1, p + 7, p + 9, false);
            post4(x, p + 16, p + 14, p + 22, p + 24, false);
            post4(x, p + 17, p + 15, p + 23, p + 25, false);
        }
        if (!e.bottom) {
            P p = p1 + j;
            post4(x, p, p - 2, p + 6, p + 8, false);
            post4(x, p + 1, p - 1, p + 7, p + 9, false);
        }
        if (!tb) {
            post4(x, p0 + 48 + j, p0 + 48 + j - 2, p1 + j - 10, p1 + j - 8, false);
            post4(x, p0 + 48 + j + 1, p0 + 48 + j - 1, p1 + j - 9, p1 + j - 7, false);
        }
    }
    const int32_t q = s.hpqp;
    const bool ha = s.hp_absent;
    for (P j = row_start(e); j < row_end(e); j += 64) {
        if (e.top) {
            P p = p1 + j;
            post4(x, p + 5, p + 4, p + 64, p + 65, false);
            post4(x, p + 7, p + 6, p + 66, p + 67, false);
            post4x4_stage1(x, p1 + j, q, ha, false);
        } else if (e.bottom) {
            post4x4_stage1(x, p0 + 16 + j, q, ha, false);
            post4x4_stage1(x, p0 + 32 + j, q, ha, false);
            P p = p0 + 48 + j;
            post4(x, p + 15, p + 14, p + 74, p + 75, false);
            post4(x, p + 13, p + 12, p + 72, p + 73, false);
        } else {
            post4x4_stage1(x, p0 + 16 + j, q, ha, false);
            post4x4_stage1(x, p0 + 32 + j, q, ha, false);
            post4x4_stage1_split(x, p0 + 48 + j, p1 + j, 0, q, ha, false);
            post4x4_stage1(x, p1 + j, q, ha, false);
        }
    }
}

// invTransformMacroblock_alteredOperators_hard: JPEG XR operators, no tile boundaries.
static void inv_mb_jpegxr(Buf& x, P p0, P p1, Edges e, const Stage& s) {
    bool tb = e.top || e.bottom, lr = e.left || e.right;
    if (!(e.bottom || e.right)) {
        idct_stage2(x, p1);
        normalize(x, p1, s);
    }
    if (s.overlap == 2) {
        if (e.top && e.left) post4(x, p1, p1 + 64, p1 + 16, p1 + 80, true);
        if (e.top && e.right) post4(x, p1 - 128, p1 - 64, p1 - 112, p1 - 48, true);
        if (e.bottom && e.left) post4(x, p0 + 32, p0 + 96, p0 + 48, p0 + 112, true);
        if (e.bottom && e.right) post4(x, p0 - 96, p0 - 32, p0 - 80, p0 - 16, true);
        if (lr && !tb) {
            for (P j : {P(0), P(-128)}) {
                if ((j == 0 && !e.left) || (j != 0 && !e.right)) continue;
                post4(x, p0 + j + 32, p0 + j + 48, p1 + j, p1 + j + 16, true);
                post4(x, p0 + j + 96, p0 + j + 112, p1 + j + 64, p1 + j + 80, true);
            }
        }
        if (!lr) {
            if (tb) {
                if (e.top) {
                    post4(x, p1 - 128, p1 - 64, p1, p1 + 64, true);
                    post4(x, p1 - 112, p1 - 48, p1 + 16, p1 + 80, true);
                }
                if (e.bottom) {
                    P p = p0 + 32;
                    post4(x, p - 128, p - 64, p, p + 64, true);
                    post4(x, p - 112, p - 48, p + 16, p + 80, true);
                }
            } else {
                post4x4_stage2_split(x, p0, p1, true);
            }
        }
    }
    stage1_idct(x, p0, p1, e);
    if (s.overlap == 0) return;
    if (lr) {
        if (e.top && e.left) post4(x, p1, p1 + 1, p1 + 2, p1 + 3, true);
        if (e.top && e.right) post4(x, p1 - 59, p1 - 60, p1 - 57, p1 - 58, true);
        if (e.bottom && e.left) post4(x, p0 + 58, p0 + 59, p0 + 56, p0 + 57, true);
        if (e.bottom && e.right) post4(x, p0 - 1, p0 - 2, p0 - 3, p0 - 4, true);
        for (P j : {P(10), P(-64 + 14)}) {
            if ((j == 10 && !e.left) || (j != 10 && !e.right)) continue;
            if (!e.top) {
                P p = p0 + 16 + j;
                post4(x, p, p - 2, p + 6, p + 8, true);
                post4(x, p + 1, p - 1, p + 7, p + 9, true);
                post4(x, p + 16, p + 14, p + 22, p + 24, true);
                post4(x, p + 17, p + 15, p + 23, p + 25, true);
            }
            if (!e.bottom) {
                P p = p1 + j;
                post4(x, p, p - 2, p + 6, p + 8, true);
                post4(x, p + 1, p - 1, p + 7, p + 9, true);
            }
            if (!tb) {
                post4(x, p0 + 48 + j, p0 + 48 + j - 2, p1 + j - 10, p1 + j - 8, true);
                post4(x, p0 + 48 + j + 1, p0 + 48 + j - 1, p1 + j - 9, p1 + j - 7, true);
            }
        }
    }
    if (e.top)
        for (P j = row_start(e); j < row_end(e); j += 64) {
            P p = p1 + j;
            post4(x, p + 5, p + 4, p + 64, p + 65, true);
            post4(x, p + 7, p + 6, p + 66, p + 67, true);
            post4x4_stage1(x, p1 + j, 0, false, true);
        }
    if (e.bottom)
        for (P j = row_start(e); j < row_end(e); j += 64) {
            post4x4_stage1(x, p0 + 16 + j, 0, false, true);
            post4x4_stage1(x, p0 + 32 + j, 0, false, true);
            P p = p0 + 48 + j;
            post4(x, p + 15, p + 14, p + 74, p + 75, true);
            post4(x, p + 13, p + 12, p + 72, p + 73, true);
        }
    if (!tb)
        for (P j = row_start(e); j < row_end(e); j += 64) {
            post4x4_stage1(x, p0 + 16 + j, 0, false, true);
            post4x4_stage1(x, p0 + 32 + j, 0, false, true);
            post4x4_stage1_split(x, p0 + 48 + j, p1 + j, 0, 0, false, true);
            post4x4_stage1(x, p1 + j, 0, false, true);
        }
}

// ---------------------------------------------------------------- frame

Planes reconstruct(const PlaneHeader& h, const std::vector<Macroblock>& mbs, int mbw, int mbh, int overlap,
                   int subversion) {
    const int nch = h.channels;
    const Quantizers q = quantizers(h);
    const bool hp_absent = h.subband >= 2;
    const bool jpegxr = subversion != 0;
    Planes out;
    out.width = mbw * 16;
    out.height = mbh * 16;
    out.scaled = h.scaled;
    out.data.assign(nch, std::vector<int32_t>(static_cast<size_t>(out.width) * out.height));
    const size_t row = 256 * static_cast<size_t>(mbw + 1);
    std::vector<Buf> bufs(nch, Buf(2 * row, 0));
    size_t prev = 0, cur = row;
    // block positions inside a macroblock, by pixel row and column (jxrlib's idxCC)
    static const int in_col[4] = {0, 1, 5, 4}, in_row[4] = {0, 2, 10, 8};
    int idx[16][16];
    for (int y = 0; y < 16; ++y)
        for (int xx = 0; xx < 16; ++xx) idx[y][xx] = in_col[xx & 3] + 64 * (xx >> 2) + in_row[y & 3] + 16 * (y >> 2);
    for (int r = 0; r <= mbh; ++r) {
        for (auto& b : bufs) std::fill(b.begin() + cur, b.begin() + cur + row, 0);
        for (int c = 0; c <= mbw; ++c) {
            Edges e{c == 0, c == mbw, r == 0, r == mbh};
            if (r < mbh && c < mbw) {
                const Macroblock& mb = mbs[static_cast<size_t>(r) * mbw + c];
                for (int ch = 0; ch < nch; ++ch) {
                    size_t base = cur + 256 * static_cast<size_t>(c);
                    Buf& b = bufs[ch];
                    std::copy(mb.hp[ch].begin(), mb.hp[ch].end(), b.begin() + base);
                    b[base] = mb.dc[ch][0] * q.dc[ch];
                    for (int i = 1; i < 16; ++i) b[base + DCT_INDEX_DC[i]] = mb.dc[ch][i] * q.lp[ch];
                }
            }
            for (int ch = 0; ch < nch; ++ch) {
                Stage s{overlap, h.scaled, ch != 0, hp_absent ? 255 : q.hp[ch], hp_absent};
                P p0 = static_cast<P>(prev + 256 * c), p1 = static_cast<P>(cur + 256 * c);
                if (jpegxr)
                    inv_mb_jpegxr(bufs[ch], p0, p1, e, s);
                else
                    inv_mb_hdphoto(bufs[ch], p0, p1, e, s);
            }
        }
        if (r > 0)
            for (int ch = 0; ch < nch; ++ch)
                for (int y = 0; y < 16; ++y) {
                    int32_t* dst = &out.data[ch][static_cast<size_t>((r - 1) * 16 + y) * out.width];
                    for (int xx = 0; xx < out.width; ++xx)
                        dst[xx] = bufs[ch][prev + ((xx >> 4) << 8) + idx[y][xx & 15]];
                }
        std::swap(prev, cur);
    }
    return out;
}

// ---------------------------------------------------------------- output

static uint8_t clamp8(double v) { return static_cast<uint8_t>(std::clamp(std::round(v), 0.0, 255.0)); }

std::vector<uint8_t> rage_ycocg_to_rgb(const Planes& p) {
    size_t n = static_cast<size_t>(p.width) * p.height;
    std::vector<uint8_t> out;
    out.reserve(n * 3);
    for (size_t i = 0; i < n; ++i) {
        double y = p.data[0][i] / RAGE_LUMA_SCALE + 128.0;
        double co = p.data[1][i] / RAGE_CHROMA_SCALE;
        double cg = p.data[2][i] / RAGE_CHROMA_SCALE;
        out.push_back(clamp8(y + co - cg));
        out.push_back(clamp8(y + cg));
        out.push_back(clamp8(y - co - cg));
    }
    return out;
}

std::vector<uint8_t> rage_plane_to_gray(const Planes& p, int ch) {
    std::vector<uint8_t> out;
    out.reserve(p.data[ch].size());
    for (int32_t v : p.data[ch]) out.push_back(clamp8(v / RAGE_LUMA_SCALE + 128.0));
    return out;
}

std::vector<uint8_t> jxr_yuv_to_rgb(const Planes& p) {
    const int shift = p.scaled ? SHIFTZERO + QPFRACBITS : 0;
    const int32_t bias = (128 << shift) + (p.scaled ? (1 << (SHIFTZERO + QPFRACBITS - 1)) - 1 : 0);
    size_t n = static_cast<size_t>(p.width) * p.height;
    std::vector<uint8_t> out;
    out.reserve(n * 3);
    for (size_t i = 0; i < n; ++i) {
        int32_t g = p.data[0][i] + bias, r = -p.data[1][i], b = p.data[2][i];
        g -= r >> 1;
        r -= ((b + 1) >> 1) - g;
        b += r;
        for (int32_t v : {r, g, b}) out.push_back(static_cast<uint8_t>(std::clamp(v >> shift, 0, 255)));
    }
    return out;
}

}  // namespace jxr
