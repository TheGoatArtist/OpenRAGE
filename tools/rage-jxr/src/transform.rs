//! JPEG XR pixel reconstruction: dequantization, inverse core transform, overlap filter, output.
//!
//! A line-for-line port of jxrlib's strInvTransform.c (BSD-2-Clause, Copyright (c) Microsoft
//! Corp.; see LICENSE-jxrlib.txt) for one tile, full frame, Y_ONLY / 4:4:4 / N-component.
//! It keeps jxrlib's macroblock buffer layout so the pointer offsets port unchanged: each row
//! buffer holds 256 values per macroblock, block (bx, by) at 64*bx + 16*by, and the 16 pixels of
//! a block in the order 0 1 5 4 / 2 3 7 6 / 10 11 15 14 / 8 9 13 12 (rows).

use crate::entropy::{quantizers, Macroblock, PlaneHeader, DCT_INDEX_DC, QPFRACBITS, SHIFTZERO};

type Buf = [i32];

#[inline]
fn at(x: &Buf, i: isize) -> i32 {
    x[i as usize]
}

#[inline]
fn set(x: &mut Buf, i: isize, v: i32) {
    x[i as usize] = v;
}

// ---------------------------------------------------------------- primitives

fn dct2x2dn(x: &mut Buf, pa: isize, pb: isize, pc: isize, pd: isize) {
    let (mut a, mut b, cc, mut d) = (at(x, pa), at(x, pb), at(x, pc), at(x, pd));
    a += d;
    b -= cc;
    let t = (a - b) >> 1;
    let c = t - d;
    d = t - cc;
    a -= d;
    b += c;
    set(x, pa, a);
    set(x, pb, b);
    set(x, pc, c);
    set(x, pd, d);
}

fn dct2x2up(x: &mut Buf, pa: isize, pb: isize, pc: isize, pd: isize) {
    let (mut a, mut b, cc, mut d) = (at(x, pa), at(x, pb), at(x, pc), at(x, pd));
    a += d;
    b -= cc;
    let t = (a - b + 1) >> 1;
    let c = t - d;
    d = t - cc;
    a -= d;
    b += c;
    set(x, pa, a);
    set(x, pb, b);
    set(x, pc, c);
    set(x, pd, d);
}

fn irotate1(x: &mut Buf, pa: isize, pb: isize) {
    let (mut a, mut b) = (at(x, pa), at(x, pb));
    a -= (b + 1) >> 1;
    b += (a + 1) >> 1;
    set(x, pa, a);
    set(x, pb, b);
}

fn inv_odd(x: &mut Buf, pa: isize, pb: isize, pc: isize, pd: isize) {
    let (mut a, mut b, mut c, mut d) = (at(x, pa), at(x, pb), at(x, pc), at(x, pd));
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
    set(x, pa, a);
    set(x, pb, b);
    set(x, pc, c);
    set(x, pd, d);
}

fn inv_odd_odd(x: &mut Buf, pa: isize, pb: isize, pc: isize, pd: isize) {
    let (mut a, mut b, mut c, mut d) = (at(x, pa), at(x, pb), at(x, pc), at(x, pd));
    d += a;
    c -= b;
    let t1 = d >> 1;
    a -= t1;
    let t2 = c >> 1;
    b += t2;
    a -= (b * 3 + 3) >> 3;
    b += (a * 3 + 3) >> 2;
    a -= (b * 3 + 4) >> 3;
    b -= t2;
    a += t1;
    c += b;
    d -= a;
    set(x, pa, a);
    set(x, pb, -b);
    set(x, pc, -c);
    set(x, pd, d);
}

fn inv_odd_odd_post(x: &mut Buf, pa: isize, pb: isize, pc: isize, pd: isize) {
    let (mut a, mut b, mut c, mut d) = (at(x, pa), at(x, pb), at(x, pc), at(x, pd));
    d += a;
    c -= b;
    let t1 = d >> 1;
    a -= t1;
    let t2 = c >> 1;
    b += t2;
    a -= (b * 3 + 6) >> 3;
    b += (a * 3 + 2) >> 2;
    a -= (b * 3 + 4) >> 3;
    b -= t2;
    a += t1;
    c += b;
    d -= a;
    set(x, pa, a);
    set(x, pb, b);
    set(x, pc, c);
    set(x, pd, d);
}

fn hst1(x: &mut Buf, pa: isize, pd: isize, alt: bool) {
    let (mut a, mut d) = (at(x, pa), at(x, pd));
    a += d;
    d = (a >> 1) - d;
    a += (d * 3) >> 3;
    d += (a * 3) >> 4;
    if alt {
        d += a >> 7;
        d -= a >> 10;
    }
    set(x, pa, a);
    set(x, pd, d);
}

fn hst1_edge(mut a: i32, mut d: i32) -> (i32, i32) {
    a += d;
    d = (a >> 1) - d;
    a += (d * 3) >> 3;
    d += (a * 3) >> 4;
    d += a >> 7;
    d -= a >> 10;
    a += (d * 3 + 4) >> 3;
    d -= a >> 1;
    a += d;
    (a, -d)
}

fn hst(x: &mut Buf, pa: isize, pb: isize, pc: isize, pd: isize) {
    let (mut a, mut b, mut c, mut d) = (at(x, pa), at(x, pb), at(x, pc), at(x, pd));
    b -= c;
    a += (d * 3 + 4) >> 3;
    d -= b >> 1;
    c = ((a - b) >> 1) - c;
    set(x, pa, a - c);
    set(x, pb, b + d);
    set(x, pc, d);
    set(x, pd, c);
}

/// strPost4 (HD Photo operators) or strPost4_alternate (JPEG XR operators).
fn post4(x: &mut Buf, pa: isize, pb: isize, pc: isize, pd: isize, alt: bool) {
    let (mut a, mut b, mut c, mut d) = (at(x, pa), at(x, pb), at(x, pc), at(x, pd));
    a += d;
    b += c;
    d -= (a + 1) >> 1;
    c -= (b + 1) >> 1;
    if alt {
        (a, d) = hst1_edge(a, d);
        (b, c) = hst1_edge(b, c);
    }
    c -= (d + 1) >> 1;
    d += (c + 1) >> 1;
    d += (a + 1) >> 1;
    c += (b + 1) >> 1;
    if alt {
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
    set(x, pa, a);
    set(x, pb, b);
    set(x, pc, c);
    set(x, pd, d);
}

fn idct_stage1(x: &mut Buf, p: isize) {
    dct2x2up(x, p, p + 1, p + 2, p + 3);
    inv_odd(x, p + 5, p + 4, p + 7, p + 6);
    inv_odd(x, p + 10, p + 8, p + 11, p + 9);
    inv_odd_odd(x, p + 15, p + 14, p + 13, p + 12);
    for k in 0..4 {
        dct2x2dn(x, p + k, p + 4 + k, p + 8 + k, p + 12 + k);
    }
}

fn idct_stage2(x: &mut Buf, p: isize) {
    inv_odd(x, p + 32, p + 48, p + 96, p + 112);
    inv_odd(x, p + 128, p + 192, p + 144, p + 208);
    inv_odd_odd(x, p + 160, p + 224, p + 176, p + 240);
    dct2x2up(x, p, p + 64, p + 16, p + 80);
    const O: [isize; 16] = [0, 192, 48, 240, 64, 128, 112, 176, 16, 208, 32, 224, 80, 144, 96, 160];
    for k in 0..4 {
        dct2x2dn(x, p + O[4 * k], p + O[4 * k + 1], p + O[4 * k + 2], p + O[4 * k + 3]);
    }
}

fn clip_dcl(dcl: i32, alt: i32) -> i32 {
    if dcl > 0 {
        if alt > 0 {
            dcl.min(alt)
        } else {
            0
        }
    } else if dcl < 0 {
        if alt < 0 {
            dcl.max(alt)
        } else {
            0
        }
    } else {
        0
    }
}

/// strPost4x4Stage1Split (with DC leakage compensation) or its _alternate version.
fn post4x4_stage1_split(x: &mut Buf, p0: isize, p1: isize, off: isize, hpqp: i32, hp_absent: bool, alt: bool) {
    let p2 = p0 + 72 - off;
    let p3 = p1 + 64 - off;
    let p0 = p0 + 12;
    let p1 = p1 + 4;
    for k in 0..4 {
        dct2x2dn(x, p0 + k, p2 + k, p1 + k, p3 + k);
    }
    inv_odd_odd_post(x, p3, p3 + 1, p3 + 2, p3 + 3);
    irotate1(x, p1 + 2, p1 + 3);
    irotate1(x, p1, p1 + 1);
    irotate1(x, p2 + 1, p2 + 3);
    irotate1(x, p2, p2 + 2);
    for k in 0..4 {
        hst1(x, p0 + k, p3 + k, alt);
    }
    for k in 0..4 {
        hst(x, p0 + k, p2 + k, p1 + k, p3 + k);
    }
    if alt {
        return;
    }
    for k in 0..4 {
        let (a, b, c, d) = (at(x, p0 + k), at(x, p1 + k), at(x, p2 + k), at(x, p3 + k));
        let tmp = (a + b + c + d) >> 1;
        let mut dcl = (tmp * 595 + 65536) >> 17;
        if (dcl.abs() < hpqp && hpqp > 20) || hp_absent {
            dcl = clip_dcl(dcl, (a - b - c + d) >> 1) >> 1;
            // DCCompensate(p0, p2, p1, p3)
            set(x, p0 + k, at(x, p0 + k) - dcl);
            set(x, p3 + k, at(x, p3 + k) - dcl);
            set(x, p2 + k, at(x, p2 + k) + dcl);
            set(x, p1 + k, at(x, p1 + k) + dcl);
        }
    }
}

fn post4x4_stage1(x: &mut Buf, p: isize, hpqp: i32, hp_absent: bool, alt: bool) {
    post4x4_stage1_split(x, p, p + 16, 0, hpqp, hp_absent, alt);
}

fn post4x4_stage2_split(x: &mut Buf, p0: isize, p1: isize, alt: bool) {
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

#[derive(Clone, Copy)]
struct Edges {
    left: bool,
    right: bool,
    top: bool,
    bottom: bool,
}

fn stage1_idct(x: &mut Buf, p0: isize, p1: isize, e: Edges) {
    if !e.top {
        let mut j = if e.left { 32 } else { -96 };
        while j < if e.right { 32 } else { 160 } {
            idct_stage1(x, p0 + j);
            idct_stage1(x, p0 + j + 16);
            j += 64;
        }
    }
    if !e.bottom {
        let mut j = if e.left { 0 } else { -128 };
        while j < if e.right { 0 } else { 128 } {
            idct_stage1(x, p1 + j);
            idct_stage1(x, p1 + j + 16);
            j += 64;
        }
    }
}

fn row_range(e: Edges) -> impl Iterator<Item = isize> {
    let start: isize = if e.left { 0 } else { -192 };
    let end: isize = if e.right { -64 } else { 64 };
    (start..end).step_by(64)
}

struct Stage {
    overlap: u32,
    scaled: bool,
    chroma: bool,
    hpqp: i32,
    hp_absent: bool,
}

/// invTransformMacroblock: HD Photo 1.0 operators (codec subversion 0).
fn inv_mb_hdphoto(x: &mut Buf, p0: isize, p1: isize, e: Edges, s: &Stage) {
    let (tb, lr) = (e.top || e.bottom, e.left || e.right);
    if !(e.bottom || e.right) {
        idct_stage2(x, p1);
        normalize(x, p1, s);
    }
    if s.overlap == 2 {
        if lr && !tb {
            let j = if e.left { 0 } else { -128 };
            post4(x, p0 + j + 32, p0 + j + 48, p1 + j, p1 + j + 16, false);
            post4(x, p0 + j + 96, p0 + j + 112, p1 + j + 64, p1 + j + 80, false);
        }
        if !lr {
            if tb {
                let p = if e.top { p1 } else { p0 + 32 };
                post4(x, p - 128, p - 64, p, p + 64, false);
                post4(x, p - 112, p - 48, p + 16, p + 80, false);
            } else {
                post4x4_stage2_split(x, p0, p1, false);
            }
        }
    }
    stage1_idct(x, p0, p1, e);
    if s.overlap == 0 {
        return;
    }
    if lr {
        let j: isize = if e.left { 10 } else { -64 + 14 };
        if !e.top {
            let p = p0 + 16 + j;
            post4(x, p, p - 2, p + 6, p + 8, false);
            post4(x, p + 1, p - 1, p + 7, p + 9, false);
            post4(x, p + 16, p + 14, p + 22, p + 24, false);
            post4(x, p + 17, p + 15, p + 23, p + 25, false);
        }
        if !e.bottom {
            let p = p1 + j;
            post4(x, p, p - 2, p + 6, p + 8, false);
            post4(x, p + 1, p - 1, p + 7, p + 9, false);
        }
        if !tb {
            post4(x, p0 + 48 + j, p0 + 48 + j - 2, p1 + j - 10, p1 + j - 8, false);
            post4(x, p0 + 48 + j + 1, p0 + 48 + j - 1, p1 + j - 9, p1 + j - 7, false);
        }
    }
    let (q, ha) = (s.hpqp, s.hp_absent);
    if e.top {
        for j in row_range(e) {
            let p = p1 + j;
            post4(x, p + 5, p + 4, p + 64, p + 65, false);
            post4(x, p + 7, p + 6, p + 66, p + 67, false);
            post4x4_stage1(x, p1 + j, q, ha, false);
        }
    } else if e.bottom {
        for j in row_range(e) {
            post4x4_stage1(x, p0 + 16 + j, q, ha, false);
            post4x4_stage1(x, p0 + 32 + j, q, ha, false);
            let p = p0 + 48 + j;
            post4(x, p + 15, p + 14, p + 74, p + 75, false);
            post4(x, p + 13, p + 12, p + 72, p + 73, false);
        }
    } else {
        for j in row_range(e) {
            post4x4_stage1(x, p0 + 16 + j, q, ha, false);
            post4x4_stage1(x, p0 + 32 + j, q, ha, false);
            post4x4_stage1_split(x, p0 + 48 + j, p1 + j, 0, q, ha, false);
            post4x4_stage1(x, p1 + j, q, ha, false);
        }
    }
}

/// invTransformMacroblock_alteredOperators_hard: JPEG XR operators, no tile boundaries.
fn inv_mb_jpegxr(x: &mut Buf, p0: isize, p1: isize, e: Edges, s: &Stage) {
    let (tb, lr) = (e.top || e.bottom, e.left || e.right);
    if !(e.bottom || e.right) {
        idct_stage2(x, p1);
        normalize(x, p1, s);
    }
    if s.overlap == 2 {
        if e.top && e.left {
            post4(x, p1, p1 + 64, p1 + 16, p1 + 80, true);
        }
        if e.top && e.right {
            post4(x, p1 - 128, p1 - 64, p1 - 112, p1 - 48, true);
        }
        if e.bottom && e.left {
            post4(x, p0 + 32, p0 + 96, p0 + 48, p0 + 112, true);
        }
        if e.bottom && e.right {
            post4(x, p0 - 96, p0 - 32, p0 - 80, p0 - 16, true);
        }
        if lr && !tb {
            for j in [(e.left, 0isize), (e.right, -128)].iter().filter(|v| v.0).map(|v| v.1) {
                post4(x, p0 + j + 32, p0 + j + 48, p1 + j, p1 + j + 16, true);
                post4(x, p0 + j + 96, p0 + j + 112, p1 + j + 64, p1 + j + 80, true);
            }
        }
        if !lr {
            if tb {
                if e.top {
                    post4(x, p1 - 128, p1 - 64, p1, p1 + 64, true);
                    post4(x, p1 - 112, p1 - 48, p1 + 16, p1 + 80, true);
                }
                if e.bottom {
                    let p = p0 + 32;
                    post4(x, p - 128, p - 64, p, p + 64, true);
                    post4(x, p - 112, p - 48, p + 16, p + 80, true);
                }
            } else {
                post4x4_stage2_split(x, p0, p1, true);
            }
        }
    }
    stage1_idct(x, p0, p1, e);
    if s.overlap == 0 {
        return;
    }
    if lr {
        if e.top && e.left {
            post4(x, p1, p1 + 1, p1 + 2, p1 + 3, true);
        }
        if e.top && e.right {
            post4(x, p1 - 59, p1 - 60, p1 - 57, p1 - 58, true);
        }
        if e.bottom && e.left {
            post4(x, p0 + 58, p0 + 59, p0 + 56, p0 + 57, true);
        }
        if e.bottom && e.right {
            post4(x, p0 - 1, p0 - 2, p0 - 3, p0 - 4, true);
        }
        for j in [(e.left, 10isize), (e.right, -64 + 14)].iter().filter(|v| v.0).map(|v| v.1) {
            if !e.top {
                let p = p0 + 16 + j;
                post4(x, p, p - 2, p + 6, p + 8, true);
                post4(x, p + 1, p - 1, p + 7, p + 9, true);
                post4(x, p + 16, p + 14, p + 22, p + 24, true);
                post4(x, p + 17, p + 15, p + 23, p + 25, true);
            }
            if !e.bottom {
                let p = p1 + j;
                post4(x, p, p - 2, p + 6, p + 8, true);
                post4(x, p + 1, p - 1, p + 7, p + 9, true);
            }
            if !tb {
                post4(x, p0 + 48 + j, p0 + 48 + j - 2, p1 + j - 10, p1 + j - 8, true);
                post4(x, p0 + 48 + j + 1, p0 + 48 + j - 1, p1 + j - 9, p1 + j - 7, true);
            }
        }
    }
    if e.top {
        for j in row_range(e) {
            let p = p1 + j;
            post4(x, p + 5, p + 4, p + 64, p + 65, true);
            post4(x, p + 7, p + 6, p + 66, p + 67, true);
            post4x4_stage1(x, p1 + j, 0, false, true);
        }
    }
    if e.bottom {
        for j in row_range(e) {
            post4x4_stage1(x, p0 + 16 + j, 0, false, true);
            post4x4_stage1(x, p0 + 32 + j, 0, false, true);
            let p = p0 + 48 + j;
            post4(x, p + 15, p + 14, p + 74, p + 75, true);
            post4(x, p + 13, p + 12, p + 72, p + 73, true);
        }
    }
    if !tb {
        for j in row_range(e) {
            post4x4_stage1(x, p0 + 16 + j, 0, false, true);
            post4x4_stage1(x, p0 + 32 + j, 0, false, true);
            post4x4_stage1_split(x, p0 + 48 + j, p1 + j, 0, 0, false, true);
            post4x4_stage1(x, p1 + j, 0, false, true);
        }
    }
}

/// strNormalizeDec: chroma DC values were coded at half scale.
fn normalize(x: &mut Buf, p: isize, s: &Stage) {
    if s.scaled && s.chroma {
        for i in (0..256).step_by(16) {
            let v = at(x, p + i);
            set(x, p + i, v + v);
        }
    }
}

// ---------------------------------------------------------------- frame

/// Reconstructed planes: one `width * height` array per channel, in the scaled integer domain.
pub struct Planes {
    pub width: usize,
    pub height: usize,
    pub scaled: bool,
    pub data: Vec<Vec<i32>>,
}

/// Run the inverse transform over a decoded frame. `subversion` 0 selects HD Photo 1.0's
/// operators, anything else JPEG XR's. `overlap` is 0, 1 or 2.
pub fn reconstruct(h: &PlaneHeader, mbs: &[Macroblock], mbw: usize, mbh: usize, overlap: u32, subversion: u32) -> Planes {
    let nch = h.channels;
    let q = quantizers(h);
    let hp_absent = h.subband >= 2;
    let jpegxr = subversion != 0;
    let (width, height) = (mbw * 16, mbh * 16);
    let mut data = vec![vec![0i32; width * height]; nch];
    let row = 256 * (mbw + 1);
    // each channel's buffer holds two macroblock rows: the previous one and the current one
    let mut bufs = vec![vec![0i32; 2 * row]; nch];
    let (mut prev, mut cur) = (0usize, row);
    // block positions inside a macroblock, by pixel row and column (jxrlib's idxCC)
    let idx: Vec<[usize; 16]> = (0..16)
        .map(|y| {
            let mut r = [0usize; 16];
            for (xx, v) in r.iter_mut().enumerate() {
                *v = [0, 1, 5, 4][xx & 3] + 64 * (xx >> 2) + [0, 2, 10, 8][y & 3] + 16 * (y >> 2);
            }
            r
        })
        .collect();
    for r in 0..=mbh {
        for b in bufs.iter_mut() {
            b[cur..cur + row].fill(0);
        }
        for c in 0..=mbw {
            let e = Edges { left: c == 0, right: c == mbw, top: r == 0, bottom: r == mbh };
            if r < mbh && c < mbw {
                let mb = &mbs[r * mbw + c];
                for ch in 0..nch {
                    let base = cur + 256 * c;
                    let b = &mut bufs[ch];
                    b[base..base + 256].copy_from_slice(&mb.hp[ch]);
                    b[base] = mb.dc[ch][0] * q.dc[ch];
                    for i in 1..16 {
                        b[base + DCT_INDEX_DC[i]] = mb.dc[ch][i] * q.lp[ch];
                    }
                }
            }
            for (ch, b) in bufs.iter_mut().enumerate() {
                let s = Stage {
                    overlap,
                    scaled: h.scaled,
                    chroma: ch != 0,
                    hpqp: if hp_absent { 255 } else { q.hp[ch] },
                    hp_absent,
                };
                let (p0, p1) = ((prev + 256 * c) as isize, (cur + 256 * c) as isize);
                if jpegxr {
                    inv_mb_jpegxr(b, p0, p1, e, &s);
                } else {
                    inv_mb_hdphoto(b, p0, p1, e, &s);
                }
            }
        }
        if r > 0 {
            for (ch, b) in bufs.iter().enumerate() {
                for (y, cc) in idx.iter().enumerate() {
                    let out = &mut data[ch][((r - 1) * 16 + y) * width..][..width];
                    for (xx, v) in out.iter_mut().enumerate() {
                        *v = b[prev + ((xx >> 4) << 8) + cc[xx & 15]];
                    }
                }
            }
        }
        std::mem::swap(&mut prev, &mut cur);
    }
    Planes { width, height, scaled: h.scaled, data }
}

// ---------------------------------------------------------------- output

/// RAGE's scale between the planes and 8-bit values, fitted against ground truth (inferred).
pub const RAGE_LUMA_SCALE: f64 = 9.6875;
pub const RAGE_CHROMA_SCALE: f64 = 2.0 * RAGE_LUMA_SCALE;

fn clamp8(v: f64) -> u8 {
    v.round().clamp(0.0, 255.0) as u8
}

/// A diffuse page: the three planes hold RAGE's Y, Co and Cg.
pub fn rage_ycocg_to_rgb(p: &Planes) -> Vec<u8> {
    let n = p.width * p.height;
    let mut out = Vec::with_capacity(n * 3);
    for i in 0..n {
        let y = p.data[0][i] as f64 / RAGE_LUMA_SCALE + 128.0;
        let co = p.data[1][i] as f64 / RAGE_CHROMA_SCALE;
        let cg = p.data[2][i] as f64 / RAGE_CHROMA_SCALE;
        out.extend_from_slice(&[clamp8(y + co - cg), clamp8(y + cg), clamp8(y - co - cg)]);
    }
    out
}

/// One plane as 8-bit grey with the luma scale (the scale of non-diffuse layers is not confirmed).
pub fn rage_plane_to_gray(p: &Planes, ch: usize) -> Vec<u8> {
    p.data[ch].iter().map(|&v| clamp8(v as f64 / RAGE_LUMA_SCALE + 128.0)).collect()
}

/// jxrlib's 8-bit RGB output for standard 4:4:4 streams (outputMBRow, CF_RGB).
pub fn jxr_yuv_to_rgb(p: &Planes) -> Vec<u8> {
    let shift = if p.scaled { SHIFTZERO + QPFRACBITS } else { 0 };
    let bias = (128 << shift) + if p.scaled { (1 << (SHIFTZERO + QPFRACBITS - 1)) - 1 } else { 0 };
    let n = p.width * p.height;
    let mut out = Vec::with_capacity(n * 3);
    for i in 0..n {
        let mut g = p.data[0][i] + bias;
        let mut r = -p.data[1][i];
        let mut b = p.data[2][i];
        g -= r >> 1;
        r -= ((b + 1) >> 1) - g;
        b += r;
        for v in [r, g, b] {
            out.push((v >> shift).clamp(0, 255) as u8);
        }
    }
    out
}
