//! JPEG XR codestream parsing: plane header, adaptive entropy decoding, prediction.
//!
//! Covers what RAGE's map pages use: spatial bitstream, one tile, Y_ONLY / YUV 4:4:4 /
//! N-component planes. The structure follows Microsoft's jxrlib (segdec.c, strdec.c,
//! adapthuff.c, strPredQuant*.c, image.c), BSD-2-Clause, Copyright (c) Microsoft Corp.;
//! see LICENSE-jxrlib.txt. RAGE's departures from the standard are the switches in `Rules`.

use crate::bits::Bits;

pub type Res<T> = Result<T, String>;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum ColorFormat {
    YOnly,
    Yuv444,
    NComponent,
}

/// How the adaptive coding behaves. `STANDARD` is JPEG XR; `RAGE` is what id's encoder wrote.
#[derive(Clone, Copy, Debug)]
pub struct Rules {
    /// Every channel has its own bit-precision model, updated with the luma weights.
    pub channel_models: bool,
    /// Every channel has its own coded-block-pattern model.
    pub channel_cbp_models: bool,
    /// N-component streams never update the DC model.
    pub fixed_dc_ncomp: bool,
    /// N-component streams code every channel with the luma contexts.
    pub ncomp_luma_contexts: bool,
}

impl Rules {
    pub const STANDARD: Rules = Rules {
        channel_models: false,
        channel_cbp_models: false,
        fixed_dc_ncomp: false,
        ncomp_luma_contexts: false,
    };
    pub const RAGE: Rules = Rules {
        channel_models: true,
        channel_cbp_models: true,
        fixed_dc_ncomp: true,
        ncomp_luma_contexts: true,
    };
}

// ---------------------------------------------------------------- plane header

#[derive(Clone, Debug)]
pub struct PlaneHeader {
    pub cf: ColorFormat,
    pub scaled: bool,
    /// 0 all, 1 no flexbits, 2 no high-pass, 3 DC only.
    pub subband: u32,
    pub channels: usize,
    pub qdc: Vec<u32>,
    pub qlp: Vec<u32>,
    pub qhp: Option<Vec<u32>>,
    pub hp_from_lp: bool,
}

fn quantizer(b: &mut Bits, channels: usize) -> Vec<u32> {
    let mode = if channels > 1 { b.get(2) } else { 0 };
    let first = b.get(8);
    match mode {
        0 => vec![first; channels],
        1 => {
            let rest = b.get(8);
            let mut q = vec![rest; channels];
            q[0] = first;
            q
        }
        _ => {
            let mut q = vec![first];
            for _ in 1..channels {
                q.push(b.get(8));
            }
            q
        }
    }
}

fn plane_header(b: &mut Bits) -> Res<PlaneHeader> {
    let cf = match b.get(3) {
        0 => ColorFormat::YOnly,
        3 => ColorFormat::Yuv444,
        6 => ColorFormat::NComponent,
        other => return Err(format!("colour format {other} not supported")),
    };
    let scaled = b.get(1) == 1;
    let subband = b.get(4);
    let channels = match cf {
        ColorFormat::Yuv444 => {
            b.get(8);
            3
        }
        ColorFormat::NComponent => {
            let n = b.get(4) as usize + 1;
            b.get(4);
            n
        }
        ColorFormat::YOnly => 1,
    };
    if b.get(1) != 1 {
        return Err("per-tile DC quantizer not supported".into());
    }
    let qdc = quantizer(b, channels);
    let qlp = if b.get(1) == 1 {
        qdc.clone()
    } else if b.get(1) == 1 {
        quantizer(b, channels)
    } else {
        return Err("per-tile LP quantizer not supported".into());
    };
    let mut qhp = None;
    let mut hp_from_lp = false;
    if subband < 2 {
        if b.get(1) == 1 {
            qhp = Some(qlp.clone());
            hp_from_lp = true;
        } else if b.get(1) == 1 {
            qhp = Some(quantizer(b, channels));
        } else {
            return Err("per-tile HP quantizer not supported".into());
        }
    }
    b.pos = (b.pos + 7) & !7;
    Ok(PlaneHeader { cf, scaled, subband, channels, qdc, qlp, qhp, hp_from_lp })
}

/// Skip to the spatial packet start code 00 00 01 xx and past it.
fn find_tile(b: &mut Bits) -> Res<()> {
    let mut pos = b.pos;
    loop {
        match (b.byte_at(pos), b.byte_at(pos + 8), b.byte_at(pos + 16)) {
            (Some(0), Some(0), Some(1)) => {
                b.pos = pos + 32;
                return Ok(());
            }
            (Some(_), Some(_), Some(_)) => pos += 8,
            _ => return Err("no tile start code".into()),
        }
    }
}

pub const SHIFTZERO: u32 = 1;
pub const QPFRACBITS: u32 = 2;

fn qp_value(index: u32, shift: u32) -> i32 {
    if index == 0 {
        1
    } else if index < 16 {
        (index << shift) as i32
    } else {
        ((16 + (index & 0xf)) << (((index >> 4) - 1) + shift)) as i32
    }
}

/// Scaled-arithmetic quantizer values per channel (remapQP / formatQuantizer).
pub struct Quantizers {
    pub dc: Vec<i32>,
    pub lp: Vec<i32>,
    pub hp: Vec<i32>,
}

pub fn quantizers(h: &PlaneHeader) -> Quantizers {
    let band = |idx: &[u32], shifted: bool| -> Vec<i32> {
        (0..h.channels)
            .map(|c| qp_value(idx[c], if c > 0 && shifted { SHIFTZERO - 1 } else { SHIFTZERO }))
            .collect()
    };
    let dc = band(&h.qdc, true);
    let lp = band(&h.qlp, true);
    let hp = match &h.qhp {
        Some(_) if h.hp_from_lp => lp.clone(),
        Some(q) => band(q, false),
        None => vec![1; h.channels],
    };
    Quantizers { dc, lp, hp }
}

// ---------------------------------------------------------------- adaptive code tables (adapthuff.c)

type Table = &'static [(u32, u32)]; // (code, length) per symbol

static T4: [Table; 1] = [&[(1, 1), (1, 2), (0, 3), (1, 3)]];
static T5: [Table; 2] = [
    &[(1, 1), (1, 2), (1, 3), (0, 4), (1, 4)],
    &[(1, 1), (0, 3), (1, 3), (2, 3), (3, 3)],
];
static T6: [Table; 4] = [
    &[(1, 1), (0, 5), (1, 3), (1, 5), (1, 2), (1, 4)],
    &[(1, 2), (0, 4), (2, 2), (1, 4), (3, 2), (1, 3)],
    &[(0, 4), (1, 4), (1, 2), (2, 2), (3, 2), (1, 3)],
    &[(0, 5), (1, 5), (1, 2), (1, 1), (1, 4), (1, 3)],
];
static T7: [Table; 2] = [
    &[(1, 2), (2, 2), (3, 2), (1, 3), (1, 4), (0, 5), (1, 5)],
    &[(1, 1), (1, 2), (1, 3), (1, 4), (1, 5), (0, 6), (1, 6)],
];
static T8: [Table; 2] = [
    &[(2, 2), (1, 3), (1, 5), (1, 4), (3, 2), (2, 3), (0, 5), (3, 3)],
    &[(1, 3), (2, 3), (1, 4), (3, 3), (4, 3), (5, 3), (0, 4), (3, 2)],
];
static T9: [Table; 2] = [
    &[(2, 3), (0, 5), (2, 4), (1, 5), (2, 5), (1, 1), (3, 3), (3, 5), (3, 4)],
    &[(1, 1), (1, 3), (2, 3), (1, 4), (1, 6), (3, 3), (1, 5), (0, 7), (1, 7)],
];
static T12: [Table; 5] = [
    &[(1, 5), (1, 6), (0, 7), (1, 7), (4, 5), (2, 3), (5, 5), (1, 1), (6, 5), (1, 4), (7, 5), (3, 3)],
    &[(2, 4), (2, 5), (0, 6), (1, 6), (3, 4), (2, 3), (3, 5), (3, 2), (3, 3), (4, 3), (1, 5), (5, 3)],
    &[(3, 2), (1, 3), (0, 7), (1, 7), (1, 5), (2, 3), (2, 7), (3, 3), (4, 3), (5, 3), (3, 7), (1, 4)],
    &[(1, 3), (3, 2), (0, 7), (1, 5), (2, 5), (2, 3), (1, 7), (3, 3), (3, 5), (4, 3), (1, 6), (5, 3)],
    &[(2, 3), (1, 1), (1, 7), (1, 4), (2, 7), (3, 3), (0, 8), (2, 4), (3, 7), (3, 4), (1, 8), (1, 5)],
];
static DELTA5: [i32; 5] = [0, -1, 0, 1, 1];
static DELTA6: [[i32; 6]; 3] = [[-1, 1, 1, 1, 0, 1], [-2, 0, 0, 2, 0, 0], [-1, -1, 0, 1, -2, 0]];
static DELTA7: [i32; 7] = [1, 0, -1, -1, -1, -1, -1];
static DELTA9: [i32; 9] = [2, 2, 1, 1, -1, -2, -2, -2, -3];
static DELTA12: [[i32; 12]; 4] = [
    [1, 1, 1, 1, 1, 0, 0, -1, 2, 1, 0, 0],
    [2, 2, -1, -1, -1, 0, -2, -1, 0, 0, -2, -1],
    [-1, 1, 0, 2, 0, 0, 0, 0, -2, 0, 1, 1],
    [0, 1, 0, 1, -2, 0, -1, -1, -2, -1, -2, -2],
];
const THRESHOLD: i32 = 8;
const MEMORY: i32 = 8;

fn max_tables(n: usize) -> usize {
    match n {
        4 => 1,
        5 => 2,
        6 => 4,
        7 => 2,
        8 => 2,
        9 => 2,
        12 => 5,
        _ => unreachable!(),
    }
}

/// One adaptive Huffman context (CAdaptiveHuffman).
#[derive(Clone)]
struct Ah {
    n: usize,
    init: bool,
    disc: i32,
    disc1: i32,
    lower: i32,
    upper: i32,
    t: usize,
    table: Table,
    delta: &'static [i32],
    delta1: &'static [i32],
}

impl Ah {
    fn new(n: usize) -> Ah {
        Ah { n, init: false, disc: 0, disc1: 0, lower: 0, upper: 0, t: 0, table: &[], delta: &[], delta1: &[] }
    }

    /// AdaptDiscriminant: move to a neighbouring table when the statistics say so.
    fn adapt(&mut self) {
        let n = self.n;
        let second = n == 6 || n == 12;
        if !self.init {
            self.init = true;
            self.disc = 0;
            self.disc1 = 0;
            self.t = if second { 1 } else { 0 };
        }
        let dl = self.disc;
        let dh = if second { self.disc1 } else { self.disc };
        let mut change = false;
        if dl < self.lower {
            self.t -= 1;
            change = true;
        } else if dh > self.upper {
            self.t += 1;
            change = true;
        }
        if change {
            self.disc = 0;
            self.disc1 = 0;
        }
        let lim = THRESHOLD * MEMORY;
        self.disc = self.disc.clamp(-lim, lim);
        self.disc1 = self.disc1.clamp(-lim, lim);
        let t = self.t;
        let mx = max_tables(n);
        self.lower = if t == 0 { i32::MIN } else { -THRESHOLD };
        self.upper = if t == mx - 1 { 1 << 30 } else { THRESHOLD };
        // delta rows for the 6- and 12-symbol tables: toward the lower table and the upper one
        let lower_row = (t + usize::from(t == 0)).saturating_sub(1);
        let upper_row = t.saturating_sub(usize::from(t + 1 == mx));
        match n {
            4 => (self.table, self.delta) = (T4[0], &[]),
            5 => (self.table, self.delta) = (T5[t], &DELTA5),
            6 => {
                self.table = T6[t];
                self.delta1 = &DELTA6[upper_row];
                self.delta = &DELTA6[lower_row];
            }
            7 => (self.table, self.delta) = (T7[t], &DELTA7),
            8 => (self.table, self.delta) = (T8[0], &[]),
            9 => (self.table, self.delta) = (T9[t], &DELTA9),
            12 => {
                self.table = T12[t];
                self.delta1 = &DELTA12[upper_row];
                self.delta = &DELTA12[lower_row];
            }
            _ => unreachable!(),
        }
    }

    fn decode(&self, b: &mut Bits) -> Res<usize> {
        let (mut code, mut len) = (0u32, 0u32);
        while len < 16 {
            code = (code << 1) | b.bit();
            len += 1;
            if let Some(s) = self.table.iter().position(|&e| e == (code, len)) {
                return Ok(s);
            }
        }
        Err(format!("invalid code at bit {}", b.pos))
    }
}

// ---------------------------------------------------------------- constants (image.c, strcodec.c)

const ZZ_LP: [usize; 16] = [0, 1, 4, 5, 2, 8, 6, 9, 3, 12, 10, 7, 13, 11, 14, 15];
const ZZ_H: [usize; 16] = [0, 1, 4, 5, 2, 8, 6, 9, 3, 12, 10, 7, 13, 11, 14, 15];
const ZZ_V: [usize; 16] = [0, 4, 8, 5, 1, 12, 9, 6, 2, 13, 3, 15, 7, 10, 14, 11];
const DCT_INDEX: [usize; 16] = [0, 5, 1, 6, 10, 12, 8, 14, 2, 4, 3, 7, 9, 13, 11, 15];
pub const DCT_INDEX_DC: [usize; 16] = [0, 128, 64, 208, 32, 240, 48, 224, 16, 192, 80, 144, 112, 176, 96, 160];
const BLK_OFFSET: [usize; 16] = [0, 64, 16, 80, 128, 192, 144, 208, 32, 96, 48, 112, 160, 224, 176, 240];
const RUN_BIN: [i32; 15] = [-1, -1, -1, -1, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0];
const RUN_FLC: [u32; 15] = [0, 0, 1, 1, 3, 0, 0, 1, 1, 2, 0, 0, 0, 0, 1];
const RUN_REMAP: [i32; 15] = [1, 2, 3, 5, 7, 1, 2, 3, 5, 7, 1, 2, 3, 4, 5];
const ALPHABET: [usize; 21] = [5, 4, 8, 7, 7, 12, 6, 6, 12, 6, 6, 7, 7, 12, 6, 6, 12, 6, 6, 7, 7];
const CTDC: usize = 5;
const CONTEXTX: usize = 8;
const CBP: usize = 21; // index of the CBP context in `Decoder::ah`
const CBP1: usize = 22;
const MAXTOTAL: i32 = 32767;
const ORIENT_WEIGHT: i32 = 4;
const MODELWEIGHT: i32 = 70;
const BAND_DC: usize = 0;
const BAND_LP: usize = 1;
const BAND_AC: usize = 2;
const W0: [i32; 3] = [240, 12, 1];
const W1: [[i32; 16]; 3] = [
    [0, 240, 120, 80, 60, 48, 40, 34, 30, 27, 24, 22, 20, 18, 17, 16],
    [0, 12, 6, 4, 3, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1],
    [0, 16, 8, 5, 4, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 1],
];

/// Bit-precision model for one band (CAdaptiveModel).
struct Model {
    band: usize,
    per_channel: bool,
    bits: Vec<i32>,
    state: Vec<i32>,
}

impl Model {
    fn new(band: usize, bits: i32, nch: usize, per_channel: bool) -> Model {
        let n = if per_channel { nch } else { 2 };
        Model { band, per_channel, bits: vec![bits; n], state: vec![0; n] }
    }

    fn slot(&self, c: usize) -> usize {
        if self.per_channel {
            c
        } else {
            c.min(1)
        }
    }

    fn bits(&self, c: usize) -> u32 {
        self.bits[self.slot(c)] as u32
    }

    /// UpdateModelMB. `lmc` holds the nonzero count of each channel.
    fn update(&mut self, cf: ColorFormat, nch: usize, lmc: &[i32]) {
        let band = self.band;
        let lm: Vec<i32> = if self.per_channel {
            lmc.iter().map(|&n| n * W0[band]).collect()
        } else {
            let mut chroma = lmc[1..].iter().sum::<i32>() * W1[band][nch - 1];
            if band == BAND_AC {
                chroma >>= 4;
            }
            vec![lmc[0] * W0[band], chroma]
        };
        for j in 0..self.bits.len() {
            let mut ms = self.state[j];
            let mut d = (lm[j] - MODELWEIGHT) >> 2;
            if d <= -8 {
                d = (d + 4).max(-16);
                ms += d;
                if ms < -8 {
                    if self.bits[j] == 0 {
                        ms = -8;
                    } else {
                        ms = 0;
                        self.bits[j] -= 1;
                    }
                }
            } else if d >= 8 {
                d = (d - 4).min(15);
                ms += d;
                if ms > 8 {
                    if self.bits[j] >= 15 {
                        self.bits[j] = 15;
                        ms = 8;
                    } else {
                        ms = 0;
                        self.bits[j] += 1;
                    }
                }
            }
            self.state[j] = ms;
            if cf == ColorFormat::YOnly {
                break;
            }
        }
    }
}

/// Adaptive scan order (CAdaptiveScan).
struct Scan {
    scan: [usize; 16],
    total: [i32; 16],
}

impl Scan {
    fn new(order: [usize; 16]) -> Scan {
        Scan { scan: order, total: [0; 16] }
    }

    fn reset_totals(&mut self) {
        self.total[0] = MAXTOTAL;
        for k in 1..16 {
            self.total[k] = 34 - 2 * k as i32;
        }
    }

    fn hit(&mut self, i: usize) {
        self.total[i] += 1;
        if i > 0 && self.total[i] > self.total[i - 1] {
            self.total.swap(i, i - 1);
            self.scan.swap(i, i - 1);
        }
    }
}

#[derive(Clone, Copy, Default)]
struct Pred {
    dc: i32,
    ad: [i32; 6],
    cbp: i32,
}

/// One decoded macroblock: per channel, the 16 DC/low-pass values and the 256 high-pass values
/// in jxrlib's buffer layout (block (bx, by) at 64*bx + 16*by).
pub struct Macroblock {
    pub x: usize,
    pub y: usize,
    pub dc: Vec<[i32; 16]>,
    pub hp: Vec<[i32; 256]>,
    pub cbp: Vec<i32>,
    pub orient: i32,
}

// ---------------------------------------------------------------- the decoder

pub struct Decoder<'a> {
    rules: Rules,
    b: Bits<'a>,
    pub header: PlaneHeader,
    pub cf: ColorFormat,
    pub nch: usize,
    pub mbw: usize,
    pub mbh: usize,
    qp_hp: Vec<i32>,
    ncomp_luma: bool,
    ah: Vec<Ah>,
    scan_lp: Scan,
    scan_h: Scan,
    scan_v: Scan,
    m_ac: Model,
    m_lp: Model,
    m_dc: Model,
    cbp_max: i32,
    cbp_zero: i32,
    cbp_c0: Vec<i32>,
    cbp_c1: Vec<i32>,
    cbp_st: Vec<i32>,
    cur: Vec<Vec<Pred>>,
    prev: Vec<Vec<Pred>>,
}

impl<'a> Decoder<'a> {
    /// `code` starts at the image-plane header (as RAGE stores it).
    pub fn new(code: &'a [u8], rules: Rules, width: usize, height: usize) -> Res<Decoder<'a>> {
        let mut b = Bits::new(code);
        let header = plane_header(&mut b)?;
        find_tile(&mut b)?;
        let cf = header.cf;
        let nch = header.channels;
        let (mbw, mbh) = (width.div_ceil(16), height.div_ceil(16));
        let qp_hp = quantizers(&header).hp;
        let mut ah: Vec<Ah> = ALPHABET.iter().map(|&n| Ah::new(n)).collect();
        ah.push(Ah::new(if cf == ColorFormat::Yuv444 { 9 } else { 5 }));
        ah.push(Ah::new(5));
        let ncbp = if rules.channel_cbp_models { nch.max(2) } else { 2 };
        let pm = rules.channel_models;
        let mut d = Decoder {
            rules,
            b,
            header,
            cf,
            nch,
            mbw,
            mbh,
            qp_hp,
            ncomp_luma: rules.ncomp_luma_contexts && cf == ColorFormat::NComponent,
            ah,
            scan_lp: Scan::new(ZZ_LP),
            scan_h: Scan::new(ZZ_H.map(|z| DCT_INDEX[z])),
            scan_v: Scan::new(ZZ_V.map(|z| DCT_INDEX[z])),
            m_ac: Model::new(BAND_AC, 0, nch, pm),
            m_lp: Model::new(BAND_LP, 4, nch, pm),
            m_dc: Model::new(BAND_DC, 8, nch, pm),
            cbp_max: 1,
            cbp_zero: 1,
            cbp_c0: vec![-4; ncbp],
            cbp_c1: vec![4; ncbp],
            cbp_st: vec![0; ncbp],
            cur: vec![vec![Pred::default(); mbw]; nch],
            prev: vec![vec![Pred::default(); mbw]; nch],
        };
        d.adapt_lp();
        d.adapt_hp();
        Ok(d)
    }

    /// Bits left between the parse position and the end of the stream (negative = overran).
    pub fn slack(&self) -> isize {
        self.b.len_bits() as isize - self.b.pos as isize
    }

    fn adapt_lp(&mut self) {
        for k in 0..CTDC + CONTEXTX {
            self.ah[k].adapt();
        }
    }

    fn adapt_hp(&mut self) {
        self.ah[CBP].adapt();
        self.ah[CBP1].adapt();
        for k in 0..CONTEXTX {
            self.ah[k + CTDC + CONTEXTX].adapt();
        }
    }

    // ---- symbol helpers

    fn sym(&mut self, k: usize) -> Res<usize> {
        self.ah[k].decode(&mut self.b)
    }

    /// getHuff plus the discriminant update.
    fn sym_d(&mut self, k: usize) -> Res<usize> {
        let s = self.sym(k)?;
        self.ah[k].disc += self.ah[k].delta[s];
        Ok(s)
    }

    fn sym_d2(&mut self, k: usize) -> Res<usize> {
        let s = self.sym(k)?;
        self.ah[k].disc += self.ah[k].delta[s];
        self.ah[k].disc1 += self.ah[k].delta1[s];
        Ok(s)
    }

    fn abs_level(&mut self, k: usize) -> Res<i32> {
        let i = self.sym_d(k)?;
        if i < 2 {
            return Ok(i as i32 + 2);
        }
        if i < 6 {
            return Ok([2, 3, 4, 6, 10, 14][i] + self.b.get([0, 0, 1, 2, 2, 2][i]) as i32);
        }
        let mut fixed = self.b.get(4) + 4;
        if fixed == 19 {
            fixed += self.b.get(2);
            if fixed == 22 {
                fixed += self.b.get(3);
            }
        }
        Ok(2 + (1 << fixed) + self.b.get(fixed) as i32)
    }

    fn sig_run(&mut self, max_run: i32) -> Res<i32> {
        if max_run < 5 {
            if max_run == 1 || self.b.bit() == 1 {
                return Ok(1);
            }
            if max_run == 2 || self.b.bit() == 1 {
                return Ok(2);
            }
            if max_run == 3 || self.b.bit() == 1 {
                return Ok(3);
            }
            return Ok(4);
        }
        let idx = (self.sym(0)? as i32 + RUN_BIN[max_run as usize] * 5) as usize;
        Ok(RUN_REMAP[idx] + self.b.get(RUN_FLC[idx]) as i32)
    }

    fn index(&mut self, loc: i32, k: usize) -> Res<usize> {
        if loc < 15 {
            return self.sym_d2(k);
        }
        if loc == 15 {
            if self.b.bit() == 0 {
                return Ok(0);
            }
            if self.b.bit() == 0 {
                return Ok(2);
            }
            return Ok(1 + 2 * self.b.bit() as usize);
        }
        Ok(self.b.bit() as usize)
    }

    fn sign(&mut self) -> i32 {
        -(self.b.bit() as i32)
    }

    // ---- DC

    fn decode_dc(&mut self, mb: &mut Macroblock) -> Res<()> {
        let nch = self.nch;
        let mut lm = vec![0i32; nch];
        let flags: Vec<bool> = if self.cf == ColorFormat::Yuv444 {
            let idx = self.sym(2)?;
            vec![idx >> 2 != 0, (idx >> 1) & 1 != 0, idx & 1 != 0]
        } else {
            Vec::new()
        };
        for i in 0..nch {
            let bits = self.m_dc.bits(i);
            let mut v = 0i32;
            let significant = if self.cf == ColorFormat::Yuv444 { flags[i] } else { self.b.bit() == 1 };
            if significant {
                v = self.abs_level(if i == 0 || self.cf != ColorFormat::Yuv444 { 3 } else { 4 })? - 1;
                lm[i] += 1;
            }
            if bits > 0 {
                v = (v << bits) | self.b.get(bits) as i32;
            }
            if v != 0 && self.b.bit() == 1 {
                v = -v;
            }
            mb.dc[i][0] = v;
        }
        if !(self.rules.fixed_dc_ncomp && self.cf == ColorFormat::NComponent) {
            self.m_dc.update(self.cf, nch, &lm);
        }
        Ok(())
    }

    // ---- low-pass

    /// DecodeBlock: (run, level) pairs for one 15-coefficient block.
    fn decode_block(&mut self, chroma: bool, offset: usize, mut loc: i32) -> Res<Vec<(i32, i32)>> {
        let base = offset + if chroma { 3 } else { 0 };
        let mut out = Vec::new();
        let idx = self.sym_d2(base)?;
        let sr = idx & 1;
        let mut srn = idx >> 2;
        let mut cont = sr & srn;
        let s = self.sign();
        let lev = if idx & 2 != 0 { (self.abs_level(6 + offset + cont)? ^ s) - s } else { 1 | s };
        let mut run = 0;
        if sr == 0 {
            run = self.sig_run(15 - loc)?;
        }
        loc += run + 1;
        out.push((run, lev));
        while srn != 0 {
            let sr = srn & 1;
            let mut run = 0;
            if sr == 0 {
                run = self.sig_run(15 - loc)?;
            }
            loc += run + 1;
            let idx = self.index(loc, base + cont + 1)?;
            srn = idx >> 1;
            cont &= srn;
            let s = self.sign();
            let lev = if idx & 1 != 0 { (self.abs_level(6 + offset + cont)? ^ s) - s } else { 1 | s };
            out.push((run, lev));
            if out.len() > 16 {
                return Err("low-pass block overrun".into());
            }
        }
        Ok(out)
    }

    fn decode_lp(&mut self, mb: &mut Macroblock, reset_rgi: bool, reset_ctx: bool) -> Res<()> {
        let nch = self.nch;
        let mut lm = vec![0i32; nch];
        if reset_rgi {
            self.scan_lp.reset_totals();
        }
        let mut cbp;
        if self.cf == ColorFormat::Yuv444 {
            let (cm, cz) = (self.cbp_max, self.cbp_zero);
            let imax = 3 * 4 - 5;
            if cz <= 0 || cm < 0 {
                cbp = 0;
                if self.b.bit() == 1 {
                    cbp = 1;
                    let k = self.b.get(2) as i32;
                    if k != 0 {
                        cbp = k * 2 + self.b.get(1) as i32;
                    }
                }
                if cm < cz {
                    cbp = imax - cbp;
                }
            } else {
                cbp = self.b.get(3) as i32;
            }
            self.cbp_max = (cm + 1 - 4 * i32::from(cbp == imax)).clamp(-8, 7);
            self.cbp_zero = (cz + 1 - 4 * i32::from(cbp == 0)).clamp(-8, 7);
        } else {
            cbp = 0;
            for c in 0..nch {
                cbp |= (self.b.bit() as i32) << c;
            }
        }
        for c in 0..nch {
            let bits = self.m_lp.bits(c);
            if cbp & 1 != 0 {
                let pairs = self.decode_block(c > 0 && !self.ncomp_luma, CTDC, 1)?;
                lm[c] += pairs.len() as i32;
                let mut i = 1usize;
                for (run, lev) in pairs {
                    i += run as usize;
                    if i > 15 {
                        return Err("low-pass run past the block".into());
                    }
                    mb.dc[c][self.scan_lp.scan[i]] = lev;
                    self.scan_lp.hit(i);
                    i += 1;
                }
            }
            if bits > 0 {
                let coef = &mut mb.dc[c];
                for v in coef.iter_mut().skip(1) {
                    if *v > 0 {
                        *v = (*v << bits) + self.b.get(bits) as i32;
                    } else if *v < 0 {
                        *v = (*v << bits) - self.b.get(bits) as i32;
                    } else {
                        let r1 = self.b.peek(bits + 1) as i32;
                        let w = if r1 & 1 != 0 { -(r1 >> 1) } else { r1 >> 1 };
                        self.b.pos += bits as usize + usize::from(w != 0);
                        *v = w;
                    }
                }
            }
            cbp >>= 1;
        }
        self.m_lp.update(self.cf, nch, &lm);
        if reset_ctx {
            self.adapt_lp();
        }
        Ok(())
    }

    // ---- high-pass

    fn four_pattern(&mut self, code: usize, one: usize, two: usize, four: usize) -> i32 {
        // shared tail of the CBP codes: 1 << n, the six two-block patterns, 0xf ^ (1 << n), 0xf
        if code == one {
            1 << self.b.get(2)
        } else if code == two {
            let n = self.b.get(2);
            match n {
                0 => 3,
                1 => 5,
                _ => [6, 9, 10, 12][(n * 2 + self.b.bit() - 4) as usize],
            }
        } else if code == two + 1 {
            0xf ^ (1 << self.b.get(2))
        } else if code == four {
            0xf
        } else {
            0
        }
    }

    fn decode_cbp(&mut self) -> Res<Vec<i32>> {
        let nloop = if self.cf == ColorFormat::Yuv444 { 1 } else { self.nch };
        let mut diff = vec![0i32; self.nch];
        for i in 0..nloop {
            let (mut cy, mut cu, mut cv) = (0i32, 0i32, 0i32);
            let n = self.sym_d(CBP1)?;
            let blocks = self.four_pattern(n, 1, 2, 4);
            for blk in 0..4 {
                if blocks & (1 << blk) == 0 {
                    continue;
                }
                let s = self.sym(CBP)?;
                if !self.ah[CBP].delta.is_empty() {
                    self.ah[CBP].disc += self.ah[CBP].delta[s];
                }
                let mut val = s + 1;
                let mut nb = 0i32;
                if val >= 6 {
                    nb = if self.b.bit() == 1 {
                        0x10
                    } else if self.b.bit() == 1 {
                        0x20
                    } else {
                        0x30
                    };
                    if val == 9 {
                        if self.b.bit() == 1 {
                        } else if self.b.bit() == 1 {
                            val = 10;
                        } else {
                            val = 11;
                        }
                    }
                    val -= 6;
                }
                let mut code1 = [0, 4, 2, 8, 12, 1][val];
                let flc = [0, 2, 1, 2, 2, 0][val];
                if flc > 0 {
                    code1 += self.b.get(flc) as usize;
                }
                nb += [0, 15, 3, 12, 1, 2, 4, 8, 5, 6, 9, 10, 7, 11, 13, 14][code1];
                if self.cf == ColorFormat::Yuv444 {
                    cy |= (nb & 0xf) << (blk * 4);
                    for k in 0..2 {
                        if (nb >> (k + 4)) & 1 != 0 {
                            let c = self.sym(1)?;
                            let code = self.four_pattern(c, 0, 1, 3);
                            if k == 0 {
                                cu |= code << (blk * 4);
                            } else {
                                cv |= code << (blk * 4);
                            }
                        }
                    }
                } else {
                    cy |= nb << (blk * 4);
                }
            }
            diff[i] = cy;
            if self.cf == ColorFormat::Yuv444 {
                diff[1] = cu;
                diff[2] = cv;
            }
        }
        Ok(diff)
    }

    fn pred_cbp(&mut self, diff: &[i32], mbx: usize, mby: usize) -> Vec<i32> {
        let mut out = Vec::with_capacity(self.nch);
        for c in 0..self.nch {
            let c1 = if self.rules.channel_cbp_models { c } else { c.min(1) };
            let mut cbp = diff[c];
            if self.cbp_st[c1] == 0 {
                if mbx == 0 {
                    if mby == 0 {
                        cbp ^= 1;
                    } else {
                        cbp ^= (self.prev[c][mbx].cbp >> 10) & 1;
                    }
                } else {
                    cbp ^= (self.cur[c][mbx - 1].cbp >> 5) & 1;
                }
                cbp ^= 0x02 & (cbp << 1);
                cbp ^= 0x10 & (cbp << 3);
                cbp ^= 0x20 & (cbp << 1);
                cbp ^= (cbp & 0x33) << 2;
                cbp ^= (cbp & 0xcc) << 6;
                cbp ^= (cbp & 0x3300) << 2;
            } else if self.cbp_st[c1] == 2 {
                cbp ^= 0xffff;
            }
            let n = (cbp & 0xffff).count_ones() as i32;
            let sat = |x: i32| x.clamp(-16, 15);
            self.cbp_c0[c1] = sat(self.cbp_c0[c1] + n - 3);
            self.cbp_c1[c1] = sat(self.cbp_c1[c1] + 16 - n - 3);
            self.cbp_st[c1] = if self.cbp_c0[c1] < 0 {
                if self.cbp_c0[c1] < self.cbp_c1[c1] {
                    1
                } else {
                    2
                }
            } else if self.cbp_c1[c1] < 0 {
                2
            } else {
                0
            };
            self.cur[c][mbx].cbp = cbp;
            out.push(cbp);
        }
        out
    }

    fn decode_block_hp(&mut self, chroma: bool, qp: i32, coef: &mut [i32; 16], vertical: bool) -> Res<i32> {
        let offset = CTDC + CONTEXTX;
        let base = offset + if chroma { 3 } else { 0 };
        let mut loc: i32 = 1;
        let mut n = 1;
        let idx = self.sym_d2(base)?;
        let sr = idx & 1;
        let mut srn = idx >> 2;
        let mut cont = sr & srn;
        let s = self.sign();
        let mut lev = (qp ^ s) - s;
        if idx & 2 != 0 {
            lev *= self.abs_level(6 + offset + cont)?;
        }
        if sr == 0 {
            loc += self.sig_run(15 - loc)?;
        }
        loc &= 0xf;
        let scan = if vertical { &mut self.scan_v } else { &mut self.scan_h };
        coef[scan.scan[loc as usize]] = lev;
        scan.hit(loc as usize);
        loc = (loc + 1) & 0xf;
        while srn != 0 {
            let sr = srn & 1;
            if sr == 0 {
                loc += self.sig_run(15 - loc)?;
                if loc >= 16 {
                    return Ok(16);
                }
            }
            let idx = self.index(loc + 1, base + cont + 1)?;
            srn = idx >> 1;
            cont &= srn;
            let s = self.sign();
            let mut lev = (qp ^ s) - s;
            if idx & 1 != 0 {
                lev *= self.abs_level(6 + offset + cont)?;
            }
            let scan = if vertical { &mut self.scan_v } else { &mut self.scan_h };
            coef[scan.scan[loc as usize]] = lev;
            scan.hit(loc as usize);
            loc = (loc + 1) & 0xf;
            n += 1;
        }
        Ok(n)
    }

    fn decode_hp(&mut self, mb: &mut Macroblock, reset_rgi: bool, reset_ctx: bool) -> Res<()> {
        if reset_rgi {
            self.scan_h.reset_totals();
            self.scan_v.reset_totals();
        }
        let diff = self.decode_cbp()?;
        mb.cbp = self.pred_cbp(&diff, mb.x, mb.y);
        let vertical = mb.orient == 1;
        let mut lm = vec![0i32; self.nch];
        for i in 0..self.nch {
            let mut cbp = mb.cbp[i];
            let bits = self.m_ac.bits(i);
            let chroma = i > 0 && !self.ncomp_luma;
            let qp = self.qp_hp[i] << bits;
            for idx in 0..16 {
                if cbp & 1 != 0 {
                    let mut blk = [0i32; 16];
                    let nz = self.decode_block_hp(chroma, qp, &mut blk, vertical)?;
                    if nz > 16 {
                        return Err("high-pass block overrun".into());
                    }
                    let base = BLK_OFFSET[idx];
                    mb.hp[i][base..base + 16].copy_from_slice(&blk);
                    lm[i] += nz;
                }
                cbp >>= 1;
            }
        }
        self.m_ac.update(self.cf, self.nch, &lm);
        if reset_ctx {
            self.adapt_hp();
        }
        Ok(())
    }

    // ---- prediction

    fn pred_dcac(&mut self, mb: &mut Macroblock) {
        let (mbx, mby) = (mb.x, mb.y);
        let dcm = if mbx == 0 && mby == 0 {
            3
        } else if mbx == 0 {
            1
        } else if mby == 0 {
            0
        } else {
            let (il, it, itl) = (self.cur[0][mbx - 1].dc, self.prev[0][mbx].dc, self.prev[0][mbx - 1].dc);
            let (mut sh, mut sv) = ((itl - il).abs(), (itl - it).abs());
            if self.cf == ColorFormat::Yuv444 {
                sh *= 2;
                sv *= 2;
                for c in 1..3 {
                    let (tl, l, t) = (self.prev[c][mbx - 1].dc, self.cur[c][mbx - 1].dc, self.prev[c][mbx].dc);
                    sh += (tl - l).abs();
                    sv += (tl - t).abs();
                }
            }
            if sh * ORIENT_WEIGHT < sv {
                1
            } else if sv * ORIENT_WEIGHT < sh {
                0
            } else {
                2
            }
        };
        for c in 0..self.nch {
            let o = &mut mb.dc[c];
            match dcm {
                1 => o[0] += self.prev[c][mbx].dc,
                0 => o[0] += self.cur[c][mbx - 1].dc,
                2 => o[0] += (self.cur[c][mbx - 1].dc + self.prev[c][mbx].dc) >> 1,
                _ => {}
            }
            if dcm == 1 {
                let r = self.prev[c][mbx].ad;
                o[4] += r[3];
                o[8] += r[4];
                o[12] += r[5];
            } else if dcm == 0 {
                let r = self.cur[c][mbx - 1].ad;
                o[1] += r[0];
                o[2] += r[1];
                o[3] += r[2];
            }
        }
        let p = mb.dc[0];
        let mut sh = p[1].abs() + p[2].abs() + p[3].abs();
        let mut sv = p[4].abs() + p[8].abs() + p[12].abs();
        if self.cf == ColorFormat::Yuv444 {
            let (u, v) = (mb.dc[1], mb.dc[2]);
            sh += u[1].abs() + v[1].abs();
            sv += u[4].abs() + v[4].abs();
        }
        let acm = if sh * ORIENT_WEIGHT < sv {
            1
        } else if sv * ORIENT_WEIGHT < sh {
            0
        } else {
            2
        };
        mb.orient = 2 - acm;
    }

    fn pred_ac(&self, mb: &mut Macroblock) {
        let acm = 2 - mb.orient;
        for s in mb.hp.iter_mut() {
            if acm == 1 {
                for j in [1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15] {
                    let (o, r) = (16 * j, 16 * j - 16);
                    for k in [2, 10, 9] {
                        s[o + k] += s[r + k];
                    }
                }
            } else if acm == 0 {
                for o in (64..256).step_by(16) {
                    for k in [1, 5, 6] {
                        s[o + k] += s[o - 64 + k];
                    }
                }
            }
        }
    }

    fn update_pred(&mut self, mb: &Macroblock) {
        for c in 0..self.nch {
            let p = &mb.dc[c];
            let e = &mut self.cur[c][mb.x];
            e.dc = p[0];
            e.ad = [p[1], p[2], p[3], p[4], p[8], p[12]];
        }
    }

    // ---- frame

    pub fn decode(&mut self) -> Res<Vec<Macroblock>> {
        let mut mbs = Vec::with_capacity(self.mbw * self.mbh);
        for mby in 0..self.mbh {
            for mbx in 0..self.mbw {
                let reset_rgi = mbx & 0xf == 0;
                let reset_ctx = reset_rgi || mbx + 1 == self.mbw;
                let mut mb = Macroblock {
                    x: mbx,
                    y: mby,
                    dc: vec![[0; 16]; self.nch],
                    hp: vec![[0; 256]; self.nch],
                    cbp: Vec::new(),
                    orient: 0,
                };
                self.decode_dc(&mut mb)?;
                self.decode_lp(&mut mb, reset_rgi, reset_ctx)?;
                self.pred_dcac(&mut mb);
                if self.header.subband < 2 {
                    self.decode_hp(&mut mb, reset_rgi, reset_ctx)?;
                    self.pred_ac(&mut mb);
                }
                if self.b.overrun() {
                    return Err(format!("ran past the end of the stream in macroblock ({mbx}, {mby})"));
                }
                self.update_pred(&mb);
                mbs.push(mb);
            }
            std::mem::swap(&mut self.cur, &mut self.prev);
        }
        Ok(mbs)
    }
}

/// The coefficient dump format of the jxrlib experiment build, for cross-checking.
pub fn dump(mbs: &[Macroblock]) -> String {
    let mut s = String::new();
    for mb in mbs {
        for (c, dc) in mb.dc.iter().enumerate() {
            let vals: Vec<String> = dc.iter().map(|v| v.to_string()).collect();
            s += &format!("LP {} {} {}: {}\n", mb.x, mb.y, c, vals.join(" "));
        }
        if !mb.cbp.is_empty() {
            let mut cb = mb.cbp.clone();
            cb.resize(3, 0);
            s += &format!("CBP {} {}: {:x} {:x} {:x}\n", mb.x, mb.y, cb[0], cb[1], cb[2]);
        }
    }
    s
}
