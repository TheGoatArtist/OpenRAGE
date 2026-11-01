//! Reading pages out of a megatexture `.pages` file (docs/research/formats/megatexture-pages.md).

use std::cmp::Reverse;
use std::collections::BinaryHeap;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::Path;

const FILE_MAGIC: u32 = 0x7733_9904;
const PAGE_MAGIC: u32 = 0xCABF_ED04;
const ROOT_OFFSET: u64 = 0x20;
const PAGE_HEADER: usize = 40;
const UNIT: u64 = 2; // child offsets and sizes count 2-byte units

#[derive(Clone, Copy, Debug)]
pub struct PageEntry {
    pub offset: u64,
    pub size: u64,
    pub x: u32,
    pub y: u32,
    pub level: u32,
}

pub struct PagesFile {
    file: File,
    pub pages: Vec<PageEntry>,
}

fn words(buf: &[u8]) -> Vec<u32> {
    buf.chunks_exact(4).map(|c| u32::from_le_bytes([c[0], c[1], c[2], c[3]])).collect()
}

/// (offset, size, quadrant) of each child named in a page header.
fn children(w: &[u32]) -> Vec<(u64, u64, u32)> {
    let sizes = [w[6] & 0xFFFF, w[6] >> 16, w[7] & 0xFFFF, w[7] >> 16];
    (0..4)
        .filter(|&q| sizes[q] != 0)
        .map(|q| (w[2 + q] as u64 * UNIT, sizes[q] as u64 * UNIT, q as u32))
        .collect()
}

impl PagesFile {
    /// Open a `.pages` file and walk its page tree (headers only), in file order.
    pub fn open(path: &Path) -> Result<PagesFile, String> {
        let mut file = File::open(path).map_err(|e| format!("{}: {e}", path.display()))?;
        let mut head = [0u8; 32];
        file.read_exact(&mut head).map_err(|e| e.to_string())?;
        if words(&head)[0] != FILE_MAGIC {
            return Err(format!("{}: not a .pages file", path.display()));
        }
        let len = file.metadata().map_err(|e| e.to_string())?.len();
        let mut header = [0u8; PAGE_HEADER];
        file.seek(SeekFrom::Start(ROOT_OFFSET)).map_err(|e| e.to_string())?;
        file.read_exact(&mut header).map_err(|e| e.to_string())?;
        let mut pending = BinaryHeap::new();
        for (off, size, _) in children(&words(&header)) {
            pending.push(Reverse((off, size)));
        }
        let mut pages = Vec::new();
        while let Some(Reverse((offset, size))) = pending.pop() {
            if offset + PAGE_HEADER as u64 > len {
                continue;
            }
            file.seek(SeekFrom::Start(offset)).map_err(|e| e.to_string())?;
            file.read_exact(&mut header).map_err(|e| e.to_string())?;
            let w = words(&header);
            if w[0] != PAGE_MAGIC {
                continue;
            }
            pages.push(PageEntry { offset, size, x: w[8] & 0xFFFF, y: w[8] >> 16, level: w[9] });
            for (child, child_size, _) in children(&w) {
                pending.push(Reverse((child, child_size)));
            }
        }
        Ok(PagesFile { file, pages })
    }

    pub fn find(&self, x: u32, y: u32, level: u32) -> Option<PageEntry> {
        self.pages.iter().copied().find(|p| p.x == x && p.y == y && p.level == level)
    }

    /// The page's compressed body (everything after its 40-byte header).
    pub fn body(&mut self, page: &PageEntry) -> Result<Vec<u8>, String> {
        let mut buf = vec![0u8; page.size.saturating_sub(PAGE_HEADER as u64) as usize];
        self.file.seek(SeekFrom::Start(page.offset + PAGE_HEADER as u64)).map_err(|e| e.to_string())?;
        self.file.read_exact(&mut buf).map_err(|e| e.to_string())?;
        Ok(buf)
    }
}

pub const LAYERS: [&str; 4] = ["diffuse", "normal", "specular", "power"];

/// A page body in the map layout: four JPEG XR layer streams and an optional mask.
pub struct MapPage<'a> {
    pub mode: u16,
    pub streams: [&'a [u8]; 4],
    pub mask: Option<&'a [u8]>,
}

impl<'a> MapPage<'a> {
    pub fn parse(body: &'a [u8]) -> Result<MapPage<'a>, String> {
        if body.len() < 16 {
            return Err("page body too short".into());
        }
        let mode = u16::from_le_bytes([body[4], body[5]]);
        let sizes: Vec<usize> = (0..4).map(|i| u16::from_be_bytes([body[6 + 2 * i], body[7 + 2 * i]]) as usize).collect();
        let streams_end = 16 + sizes.iter().sum::<usize>();
        let mask_len = if mode & 8 != 0 { 2048 } else { 0 };
        let padded = (streams_end + 1) & !1;
        if streams_end > body.len() || (padded + mask_len != body.len() && streams_end + mask_len != body.len()) {
            return Err("not a map-layout page (the stream sizes don't add up; model pages use another codec)".into());
        }
        let mut pos = 16;
        let mut streams: [&[u8]; 4] = [&[]; 4];
        for (i, &n) in sizes.iter().enumerate() {
            streams[i] = &body[pos..pos + n];
            pos += n;
        }
        let mask = (mask_len > 0).then(|| &body[body.len() - mask_len..]);
        Ok(MapPage { mode, streams, mask })
    }

    /// Pixel size of a layer: the power layer is 32×32, the others 128×128.
    pub fn layer_size(layer: usize) -> usize {
        if layer == 3 {
            32
        } else {
            128
        }
    }
}
