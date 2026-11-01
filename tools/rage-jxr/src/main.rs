//! rage-jxr: decode RAGE (2011) megatexture map pages.
//!
//! RAGE's map pages hold four JPEG XR streams (diffuse, normal, specular, power) written by
//! id Software's own encoder, which departs from the standard in four places of its adaptive
//! entropy coding. See docs/research/formats/megatexture-pages.md. Only reads files you supply;
//! it writes the images you ask for and nothing else.

mod bits;
mod entropy;
mod pages;
mod png;
mod transform;

use entropy::{ColorFormat, Decoder, Res, Rules};
use pages::{MapPage, PagesFile, LAYERS};
use std::path::{Path, PathBuf};
use std::time::Instant;
use transform::Planes;

const USAGE: &str = "\
rage-jxr: decode RAGE megatexture map pages

Usage:
  rage-jxr decode <file.pages> <x> <y> <level> [-o out.png] [--layers] [--payload]
      Decode one page to PNG. <level> is the level in the file (0 = most detailed).
      --layers   also write the normal, specular and power layers (scales not yet confirmed)
      --payload  crop to the 120x120 payload, without the 4-texel border
  rage-jxr check <file.pages>
      Parse every stream of every page and report whether each ends exactly at its end.
  rage-jxr jxr <file.jxr> <out.ppm|out.png> [--rage] [--dump]
      Decode a standard JPEG XR file (spatial, one tile, 4:4:4) with jxrlib's RGB output,
      for checking the decoder against jxrlib. --rage applies RAGE's coding rules;
      --dump prints the low-pass coefficients and block patterns.
";

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let result = match args.first().map(String::as_str) {
        Some("decode") => cmd_decode(&args[1..]),
        Some("check") => cmd_check(&args[1..]),
        Some("jxr") => cmd_jxr(&args[1..]),
        _ => {
            eprint!("{USAGE}");
            std::process::exit(2);
        }
    };
    if let Err(e) = result {
        eprintln!("error: {e}");
        std::process::exit(1);
    }
}

fn flag(args: &[String], name: &str) -> bool {
    args.iter().any(|a| a == name)
}

fn option(args: &[String], name: &str) -> Option<String> {
    args.iter().position(|a| a == name).and_then(|i| args.get(i + 1).cloned())
}

fn positional(args: &[String]) -> Vec<&str> {
    let mut out = Vec::new();
    let mut skip = false;
    for a in args {
        if skip {
            skip = false;
        } else if a == "-o" {
            skip = true;
        } else if !a.starts_with("--") {
            out.push(a.as_str());
        }
    }
    out
}

fn num(s: &str, what: &str) -> Res<u32> {
    s.parse().map_err(|_| format!("{what} must be a number, got {s:?}"))
}

/// Entropy-decode and reconstruct one map-page layer.
fn decode_layer(stream: &[u8], layer: usize) -> Res<(Planes, ColorFormat)> {
    let size = MapPage::layer_size(layer);
    let mut d = Decoder::new(stream, Rules::RAGE, size, size)?;
    let mbs = d.decode()?;
    // RAGE's pages always use overlap mode 1 and JPEG XR's operators
    let planes = transform::reconstruct(&d.header, &mbs, d.mbw, d.mbh, 1, 1);
    Ok((planes, d.cf))
}

fn crop(pixels: &[u8], size: usize, channels: usize, border: usize) -> (Vec<u8>, usize) {
    let inner = size - 2 * border;
    let mut out = Vec::with_capacity(inner * inner * channels);
    for y in border..size - border {
        out.extend_from_slice(&pixels[(y * size + border) * channels..(y * size + size - border) * channels]);
    }
    (out, inner)
}

fn cmd_decode(args: &[String]) -> Res<()> {
    let pos = positional(args);
    if pos.len() != 4 {
        return Err(format!("decode needs <file.pages> <x> <y> <level>\n\n{USAGE}"));
    }
    let (x, y, level) = (num(pos[1], "x")?, num(pos[2], "y")?, num(pos[3], "level")?);
    let mut file = PagesFile::open(Path::new(pos[0]))?;
    let page = file.find(x, y, level).ok_or(format!("no page ({x}, {y}) at level {level}"))?;
    let body = file.body(&page)?;
    let map = MapPage::parse(&body)?;
    println!(
        "page ({x}, {y}) level {level}: {} bytes, mode {}{}",
        body.len(),
        map.mode,
        if map.mask.is_some() { ", with a 128x128 1-bit mask (not decoded)" } else { "" }
    );
    let out =option(args, "-o").map(PathBuf::from).unwrap_or_else(|| PathBuf::from(format!("page_{x}_{y}_{level}.png")));
    let payload = flag(args, "--payload");
    let layers: Vec<usize> = if flag(args, "--layers") { vec![0, 1, 2, 3] } else { vec![0] };
    for layer in layers {
        let stream = map.streams[layer];
        if stream.is_empty() {
            continue;
        }
        let (planes, cf) = decode_layer(stream, layer)?;
        let size = planes.width;
        let (pixels, channels) = match (layer, cf) {
            (_, ColorFormat::Yuv444) => (transform::rage_ycocg_to_rgb(&planes), 3),
            (1, _) => {
                // normal map: the two channels as red and green
                let (a, b) = (transform::rage_plane_to_gray(&planes, 0), transform::rage_plane_to_gray(&planes, 1));
                (a.iter().zip(&b).flat_map(|(&r, &g)| [r, g, 0]).collect(), 3)
            }
            _ => (transform::rage_plane_to_gray(&planes, 0), 1),
        };
        let border = if layer == 3 { 1 } else { 4 };
        let (pixels, size) = if payload { crop(&pixels, size, channels, border) } else { (pixels, size) };
        let path = if layer == 0 { out.clone() } else { suffixed(&out, LAYERS[layer]) };
        png::write_png(&path, size, size, channels, &pixels).map_err(|e| format!("{}: {e}", path.display()))?;
        println!("{} {}x{} -> {}", LAYERS[layer], size, size, path.display());
    }
    Ok(())
}

fn suffixed(path: &Path, suffix: &str) -> PathBuf {
    let stem = path.file_stem().and_then(|s| s.to_str()).unwrap_or("page");
    path.with_file_name(format!("{stem}_{suffix}.png"))
}

fn cmd_check(args: &[String]) -> Res<()> {
    let pos = positional(args);
    let path = pos.first().ok_or(format!("check needs <file.pages>\n\n{USAGE}"))?;
    let start = Instant::now();
    let mut file = PagesFile::open(Path::new(path))?;
    let entries = file.pages.clone();
    let (mut ok, mut total, mut other) = ([0usize; 4], [0usize; 4], 0usize);
    let mut failures = Vec::new();
    for page in &entries {
        let body = file.body(page)?;
        let Ok(map) = MapPage::parse(&body) else {
            other += 1;
            continue;
        };
        for (layer, stream) in map.streams.iter().enumerate() {
            if stream.is_empty() {
                continue;
            }
            total[layer] += 1;
            let size = MapPage::layer_size(layer);
            let result = Decoder::new(stream, Rules::RAGE, size, size).and_then(|mut d| d.decode().map(|_| d.slack()));
            match result {
                Ok(slack) if (0..16).contains(&slack) => ok[layer] += 1,
                Ok(slack) => failures.push(format!("page ({}, {}) level {} {}: {slack} bits left over", page.x, page.y, page.level, LAYERS[layer])),
                Err(e) => failures.push(format!("page ({}, {}) level {} {}: {e}", page.x, page.y, page.level, LAYERS[layer])),
            }
        }
    }
    println!("{} pages in {:.1}s", entries.len(), start.elapsed().as_secs_f64());
    for layer in 0..4 {
        println!("  {:<8} {} of {} streams parse exactly", LAYERS[layer], ok[layer], total[layer]);
    }
    if other > 0 {
        println!("  {other} pages are not in the map layout (model pages use another codec)");
    }
    for f in failures.iter().take(10) {
        println!("  failed: {f}");
    }
    if failures.len() > 10 {
        println!("  ... and {} more", failures.len() - 10);
    }
    Ok(())
}

/// A JPEG XR file: (codestream from the plane header, width, height, overlap, subversion).
fn jxr_codestream(data: &[u8]) -> Res<(&[u8], usize, usize, u32, u32)> {
    let sig = b"WMPHOTO\0";
    let at = data.windows(8).position(|w| w == sig).ok_or("no WMPHOTO signature")?;
    let h = &data[at + 8..];
    if h.len() < 12 {
        return Err("truncated image header".into());
    }
    let subversion = (h[0] & 15) as u32;
    let overlap = (h[1] & 3) as u32;
    if h[2] & 0x20 != 0 {
        return Err("windowing not supported".into());
    }
    if h[1] & 0x80 != 0 {
        return Err("tiled images not supported".into());
    }
    if h[1] & 0x40 != 0 {
        return Err("frequency-mode bitstreams not supported".into());
    }
    let short = h[2] & 0x80 != 0;
    let (w, ht, n) = if short {
        (u16::from_be_bytes([h[4], h[5]]) as usize + 1, u16::from_be_bytes([h[6], h[7]]) as usize + 1, 8)
    } else {
        (
            u32::from_be_bytes([h[4], h[5], h[6], h[7]]) as usize + 1,
            u32::from_be_bytes([h[8], h[9], h[10], h[11]]) as usize + 1,
            12,
        )
    };
    Ok((&h[n..], w, ht, overlap, subversion))
}

fn cmd_jxr(args: &[String]) -> Res<()> {
    let pos = positional(args);
    if pos.len() != 2 {
        return Err(format!("jxr needs <file.jxr> <out>\n\n{USAGE}"));
    }
    let data = std::fs::read(pos[0]).map_err(|e| format!("{}: {e}", pos[0]))?;
    let (code, w, h, overlap, subversion) = jxr_codestream(&data)?;
    let rules = if flag(args, "--rage") { Rules::RAGE } else { Rules::STANDARD };
    let mut d = Decoder::new(code, rules, w, h)?;
    let mbs = d.decode()?;
    if flag(args, "--dump") {
        print!("{}", entropy::dump(&mbs));
    }
    if d.cf != ColorFormat::Yuv444 {
        return Err("only 4:4:4 colour files can be written".into());
    }
    let planes = transform::reconstruct(&d.header, &mbs, d.mbw, d.mbh, overlap, subversion);
    let rgb = transform::jxr_yuv_to_rgb(&planes);
    // the frame is padded to whole macroblocks; keep the image's own size
    let mut out = Vec::with_capacity(w * h * 3);
    for row in rgb.chunks(planes.width * 3).take(h) {
        out.extend_from_slice(&row[..w * 3]);
    }
    let path = Path::new(pos[1]);
    let written = if path.extension().is_some_and(|e| e.eq_ignore_ascii_case("png")) {
        png::write_png(path, w, h, 3, &out)
    } else {
        png::write_ppm(path, w, h, &out)
    };
    written.map_err(|e| format!("{}: {e}", path.display()))
}
