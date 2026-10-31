# Megatexture `.pages` / `.pageLines` format

- Game version / build: Steam build `1509967` (`Rage64.exe` 1.0.35.4669), with all DLC.
- Files examined: all 87 `.pages` files and all 69 `.pageLines` files in `virtualtextures`,
  `mp/virtualtextures`, `dlc/dlc2/virtualtextures` and `dlc/dlc3/virtualtextures`. Only the file
  headers and the 40-byte header in front of each page were read. No page image data was decoded
  or copied.
- Tools used: hex dumps, then [tools/pages.py](../../../tools/pages.py), written from these notes.
- Confidence: **verified** for the file header fields listed as known, the page tree and
  `.pageLines` (every file); **unknown** for the image data inside each page and header field 0x08.
- Date: 2026-10-30

## What we know

Each `.pages` file is one megatexture: a single huge texture for a map or area, cut into square
tiles called pages. The pages form a **quadtree**: the top level is one page covering the whole
texture at low detail, and each level below splits every page into four, doubling the detail.
Level 0 is the most detailed. The size of a page in pixels is not confirmed yet.

Everything is little-endian, unlike the `.resources` archives.

### File header (bytes 0–31)

| Offset | Size | Meaning |
| --- | --- | --- |
| 0x00 | 4 | Magic `04 99 33 77` |
| 0x04 | 4 | Width in pages at level 0: 128, 256, 512 or 1024 |
| 0x08 | 4 | Unknown. Nonzero in most single-player and co-op maps; zero in `_vmtr`, the `online_*` maps, some terrains and all DLC |
| 0x0C | 4 | Number of levels (8 to 11). Width = 2^(levels − 1) |
| 0x10 | 4 | Number of possible pages in a full pyramid: (4^levels − 1) / 3 |
| 0x14 | 4 | Always 2 (0x40 in the empty DLC2 files). Possibly the 2-byte unit below (guess) |
| 0x18 | 8 | File size |

### Pages

At 0x20 sits a root header with the same layout as a page header (version 0) and one child, the
top page. Real pages follow from 0x48 to the end of the file, each one directly after the last.
Each page starts with a 40-byte header:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0x00 | 4 | Magic `04 ED BF CA` |
| 0x04 | 4 | Version: 5 in every file |
| 0x08 | 4 × 4 | File offset of each of the 4 child pages, in 2-byte units |
| 0x18 | 2 × 4 | Size of each child page, in 2-byte units. 0 = no child there |
| 0x20 | 4 | X in the low 16 bits, Y in the high 16 bits, counted in pages at this level |
| 0x24 | 4 | Level |

The children are in the order top-left, top-right, bottom-left, bottom-right: child *q* of the
page at (x, y) is at (2x + (q & 1), 2y + (q >> 1)) one level down. A page whose four sizes are all
0 is a leaf. A page's size includes its own header.

The rest of each page is compressed image data. Every page body seen so far starts with the bytes
`1E 1E 19 28` followed by a small 16-bit number (usually 4, 5 or 6). The compression is not
decoded yet (see open questions).

**File order.** Pages are stored coarsest level first: the top page, then all of the next level,
and so on down to level 0. So the low-detail overview is at the front of the file and the sharpest
detail at the end.

**Sparse trees.** Most maps store only part of the full pyramid. The texture doesn't subdivide
where it isn't needed, so the lowest levels are often less than 10% filled. Six maps store no level
0 at all, so their most detailed level is one step blurrier than the format allows:
`online_meteor` and `online_meteor_terrain` (in both `virtualtextures` and `mp/virtualtextures`),
`dirttrack_terrain`, `dusty8` and `rd_terrain`.

**DLC2.** The nine `sewer_level_0N.pages` files are 72-byte placeholders with a header and an
empty root. The sewer texture is all in `sewer_boneyard_export.pages`.

### `.pageLines`

A companion index next to each `.pages` file in `virtualtextures` and `mp/virtualtextures`. It
holds one 16-bit value per possible page (so its size is 2 × (4^levels − 1) / 3), ordered top level
first, row by row within each level:

- `0xFFFF`: the page isn't stored.
- Otherwise the low 15 bits are the number of the 64 KB block where the page starts
  (file offset ÷ 65,536), and bit 15 is set when the page runs into the next block.

So it tells the engine which 64 KB disk reads fetch a given page, without walking the tree. It can
be rebuilt from the `.pages` file alone, which may be why the DLC folders don't have one.

**Limit.** With 15 bits of block number, it can only address files up to 2 GB. The largest file
today is 1.8 GB (`virtualtextures/_vmtr.pages`). Higher-resolution texture mods for big maps would
hit this ceiling.

**Seven files ship a stale `.pageLines`.** In `dam_facility`, `ghost_hideout`, `subway_town`,
`wasteland1`, `bash_infest_coop`, `online_prime` and `online_prime_terrain`, the values follow the
same rule but describe an older layout of the `.pages` file. In `online_prime_terrain`, every page
after the first ~190 KB sits 476 bytes from where the index puts it, so 4,421 of its 59,054
entries are wrong. In `dam_facility` the shift is about 4 KB and varies, and nearly every entry is
wrong. It looks as if pages were re-encoded after the index was built. Whether the game's reads
are affected on these maps is not known.

### Check results

`tools/pages.py --check` walks the tree from the root, confirms each child is where its parent
says, confirms the pages cover the file with no gaps, and compares `.pageLines` with the value the
tree predicts. Results for this build:

| Check | Result |
| --- | --- |
| Page tree covers the whole file, every child in place | 87 of 87 files |
| `.pageLines` matches exactly | 62 of 69 files |
| `.pageLines` stale (see above) | 7 of 69 files |

## Open questions

- **The image data inside each page.** id's published talks on id Tech 5 describe pages stored
  in a compact custom format and converted to DXT while the game runs, and `Rage64.exe` contains a
  CUDA DXT encoder (`CudaDXTEncode.cu`). The body's first bytes and its small mode number are the
  starting points. This is the key piece for showing the original textures in OpenRAGE.
- **Header field 0x08.** It doesn't match any total we tried (bytes per level, leaf bytes, body
  bytes, level boundaries).
- **Page size in pixels**, and whether pages carry a border. The engine log mentions
  `_vmtr_v23_256_C.pages` and 4096 × 4096 physical caches.
- **What `_vmtr.pages` and `_vmtr_dlc.pages` hold.** They are the largest files and use the same
  format, but aren't named after a map. They may be shared textures for models and props (guess).
- **Whether the stale `.pageLines` files cause bad reads** in the original game, and what the
  engine does with the block-crossing flag.
