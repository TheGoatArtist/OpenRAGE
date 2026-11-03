# Megatexture `.pages` / `.pageLines` format

- Game version / build: Steam build `1509967` (`Rage64.exe` 1.0.35.4669), with all DLC.
- Files examined: all 87 `.pages` files and all 69 `.pageLines` files in `virtualtextures`,
  `mp/virtualtextures`, `dlc/dlc2/virtualtextures` and `dlc/dlc3/virtualtextures`. For the tree,
  only the file headers and the 40-byte header in front of each page were read. For the page body
  layout, the bodies of every page in three files were read in memory and measured. No image data
  was decoded or copied.
- Tools used: hex dumps, then [tools/pages.py](../../../tools/pages.py), written from these notes;
  Python experiments for the codec, then [tools/rage-jxr](../../../tools/rage-jxr/README.md), a
  JPEG XR decoder in C++ that follows jxrlib's structure; the game's `vt_export` command and the
  RAGE Tool Kit for ground truth.
- Confidence: **verified** for the file header fields listed as known, the page tree and
  `.pageLines` (every file), and for the map page body layout (three files); **verified against
  the game's own export** for the model (`_vmtr`) page codec; **verified** for the map pages'
  entropy coding (every stream of a 2,480-page Tool Kit map) and transform; **inferred** for the
  exact scale of the map pages' final colour conversion and for colour specular's chroma centre;
  **unknown** for header field 0x08.
- Date: 2026-10-30, page body added 2026-11-01, map codec solved 2026-11-01, colour specular
  corrected 2026-11-02

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

The rest of each page is compressed image data, described next.

### Page body

A page is 128×128 texels: a 120×120 payload with a 4-texel border copied from its neighbors, as
id's own paper on RAGE's virtual textures states (van Waveren, *Software Virtual Textures*, 2012).
Its first four bytes are **per-layer quality settings** (diffuse, normal, specular, power), not a
magic number. Two body layouts exist: the **map layout**, used by the map page files, and the
**model layout**, used by `_vmtr.pages`. The model layout is fully decoded (see "Model pages").

#### Map layout

Checked on every page of `scientist`, `dusty8` and `bash_tv_entrance` (15,797 pages).

| Offset | Size | Meaning |
| --- | --- | --- |
| 0x00 | 4 | Quality per layer: `1E 1E 19 28` = 30, 30, 25, 40 in every page seen |
| 0x04 | 2 | Mode, little-endian (see below) |
| 0x06 | 2 × 4 | Sizes of the four layer streams, **big-endian** |
| 0x0E | 2 | Always 0 |
| 0x10 | | The four streams, one after another, then padding to an even length |
| | 2,048 | Only when mode bit 3 is set: a 1-bit-per-texel mask (128 × 128 / 8) |

The sizes add up exactly in every page checked. The mask is almost entirely `00` and `FF` bytes,
which fits the "1-bit alpha channel (cover mask)" the paper describes.

**The four streams are the page's layers.** The paper lists 10 channels per texel: a YCoCg
diffuse map, a normal map's X and Y, a specular power map, a specular map (mono or RGB) and the
cover mask. The streams match that:

| Stream | Header | Typical size | Layer (inferred) |
| --- | --- | --- | --- |
| 1 | 15 bytes, starts `71 00` | 2–3 KB | Diffuse |
| 2 | 15 bytes, starts `D1 10` | 1–2 KB, mostly zeros | Normal map |
| 3 | 15 bytes (`71 01`) when mode bit 2 is 0, 11 bytes (`D1 00`) when it's 1 | 1–2 KB | Specular, color or mono |
| 4 | 11 bytes, starts `D1 00` | about 140 bytes | Specular power |

Each stream header is the same in every page for that stream. Its middle part repeats one
5-nibble group three times in the 15-byte headers and appears once in the 11-byte ones, so it
probably holds per-channel settings such as quantizers (guess). All headers end with
`FF 00 00 01 00`.

**Mode bits** (values seen: 0, 1, 2, 4, 5, 6 and the same plus 8): bit 3 adds the mask; bit 2
selects a mono specular stream; the low bits (0, 1 or 2) are not known. They are not the overlap
mode: every page tested decodes best with overlap mode 1 whatever its low bits.

### The codecs

The game's executable names two page codecs in its log strings ("DCT pages" and "HDP pages") and
ships CUDA kernels named after them: an inverse 8×8 DCT with run-length input (`IDCT`,
`RLE_IDCT`, `Idct1D`), HD Photo's inverse transform and overlap filter (`InvTransformHDP`,
`InvFilterHDP`), and re-encoders to YCoCg DXT5, normal-map DXT and specular. Only names were read,
not the GPU code. The kernels take 16-bit coefficients, so the entropy decoding happens on the
CPU, in code that is encrypted in the shipped executable.

id's papers describe the same split: pages on disk use "a JPEG-like DCT based format" or HD Photo
/ JPEG XR (2012), and the DCT format is JPEG with YCoCg instead of YCbCr, zig-zag order, and
Huffman plus run-length coding (*Real-Time Texture Streaming & Decompression*, 2006).

**Map pages are JPEG XR (HD Photo) codestreams with the image header stripped.** Each stream is
a JPEG XR image-plane header, then `FF` (the escape value meaning "no profile/level field"), then
one spatial-mode tile packet (`00 00 01 00`). Parsing the plane headers gives:

| Stream | JPEG XR plane | Quantizer index |
| --- | --- | --- |
| Diffuse | YUV 4:4:4, 3 channels | Y 30, color 60 ("mixed") |
| Normal | N-component, 2 channels | 30 |
| Specular (mono pages) | N-component, 1 channel | 25 |
| Power | N-component, 1 channel | 40 |

All use scaled arithmetic and the "no flexbits" subband setting. **The four quality bytes at the
start of the page body are these quantizer indices** (30, 30, 25, 40). RAGE leaves out the
`WMPHOTO` signature and the 8-byte image header (version, overlap mode, size, formats).

A standard decoder (Windows' codec, or Microsoft's reference implementation jxrlib, BSD) reads
the first macroblock of each stream correctly and then loses its place. The power stream of
`wasteland1` page (0,0) at file level 5, for example, is bit-identical to a standard 32×32 flat
image for its first 75 bits and then falls further behind with each macroblock.

**RAGE's map codec is standard JPEG XR with four changes to the adaptive entropy coding.** They
were found with a decoder written for OpenRAGE (first in Python, now
[tools/rage-jxr](../../../tools/rage-jxr/README.md) in C++) that follows jxrlib's spatial-mode
parser line by line. On standard streams it reproduces jxrlib's coefficients exactly:

1. **Each channel has its own bit-precision model, weighted like luma.** JPEG XR adapts how many
   raw bits it spends per coefficient, with one model for luma and one shared by both chroma
   channels, which counts their nonzero values with a smaller weight. RAGE keeps one model per
   channel for each band (DC, low-pass, high-pass), and updates every one with the luma weights.
2. **Each channel has its own coded-block-pattern model.** JPEG XR shares one CBP prediction
   model between U and V; RAGE gives every channel its own, each starting from the standard
   initial state.
3. **N-component streams never update the DC model.** It stays at 8 raw bits per DC value. In
   colour streams the DC model adapts normally (per channel, as above).
4. **N-component streams code every channel with the luma contexts.** JPEG XR codes channels
   after the first with the chroma code tables; RAGE uses the luma tables for all of them. Colour
   streams use the chroma tables as normal.

With these four changes, **every stream tested parses to the end of its stream**, each ending
within its last byte: all four layers of every page of the Tool Kit's sample map (2,480 pages)
and of the retail `wasteland1` (549,714 pages, 2.2 million streams), `scientist` (3,271) and
`dusty8` (6,427) megatextures (`rage-jxr check`).
Each change on its own was needed: without change 1, textured colour pages lose their place in
the second macroblock row; without 3 or 4, textured normal maps fail.

Everything after the entropy coding is standard JPEG XR:

- **Overlap mode 1** (the first-stage overlap filter only) and **JPEG XR's operators** (codec
  subversion 1, not HD Photo 1.0's), on every page tested. The page's mode bits don't select it.
- The decoder's pixel stage (dequantization, prediction, inverse transform and overlap filter)
  matches jxrlib's output byte for byte on standard streams, in all three overlap modes with both
  operator sets (`rage-jxr jxr`, 24 test images).
- Image sizes: diffuse, normal and specular are 128×128; power is 32×32.

**The diffuse planes hold RAGE's YCoCg, not JPEG XR's colour.** JPEG XR's own YUV→RGB output comes
out dark and green; reading the three planes as Y, Co and Cg and converting the way model pages
do (R = Y + Co − Cg, G = Y + Cg, B = Y − Co − Cg) gives the right image. Fitted against the Tool
Kit ground truth (see the open questions), Y = plane / 9.69 + 128 (r = 0.9999 over 768
macroblock means), and Co and Cg are about plane / 20, which fits chroma at half the luma scale
(1/19.4); the exact constants are inferred, not derived. With those values:

- the Tool Kit pages match visually; what's left looks like the reference's own JPEG noise, with
  no block structure;
- the retail `wasteland1` page (0,0) matches the game's own `vt_export` RGB to a mean of
  2.9/255 per channel.

Also learned:

- On terrain pages the game only uses the diffuse layer: the exported normal, specular, power
  and mask of `wasteland1` page (0,0) are constants (flat normal, power 32, specular 0, full
  coverage).

#### Model pages (`_vmtr`): decoded

Decoded from the bitstream and checked against ground truth exported by the game itself (see
"Ground truth with `vt_export`"). Checked on `_vmtr.pages` pages at file levels 3 and 5.

| Offset | Size | Meaning |
| --- | --- | --- |
| 0x00 | 4 | Quality per layer: 90, 80, 60, 80 |
| 0x04 | 1 | Always 0x50 so far |
| 0x05 | 1 | 0, 1 or 2; meaning not known (mode values 0x050, 0x150, 0x250) |
| 0x06 | 1 | 1 when the specular layer is one channel, 0 when it is colour (inferred, see below) |
| 0x07 | 5 | Small flags, mostly 0 |
| 0x0C | 4 × 5 | Sizes of five streams, big-endian: diffuse, normal, specular, power, mask (2,048 or 0) |
| 0x20 | | The streams, one after another, then one zero byte if needed to make the length even |

Each stream is **baseline JPEG entropy-coded data**: the standard JPEG Huffman tables (ITU T.81
Annex K), read most significant bit first, with no `FF 00` byte stuffing, no markers and no
per-stream header. Each block is a DC difference (predicted from the previous block of the same
channel, starting at 0) followed by run/size-coded AC values in standard zig-zag order. This is the
format in id's 2006 paper.

| Stream | Content | Blocks |
| --- | --- | --- |
| Diffuse | YCoCg 4:2:0. Per 16×16 area: four Y blocks (top-left, top-right, bottom-left, bottom-right; luma tables), then Co, then Cg (chroma tables). Areas in rows, left to right | 384 |
| Normal | Two channels, alternating per 8×8 tile (X, Y, X, Y …), luma tables | 512 |
| Specular | Byte 6 = 1: one channel, luma tables. Byte 6 = 0: colour, coded like the diffuse stream but with the chroma centred at 0 (see below) | 256 or 384 |
| Power | One channel at 32×32 (a quarter of the page in each direction), luma tables | 16 |
| Mask | Not JPEG: 128 × 128 bits, row by row, **least significant bit first** in each byte | 2,048 bytes |

**Reconstruction:**

- **Quantization:** the standard JPEG tables (Annex K, luma and chroma) scaled by the layer's
  quality byte the way libjpeg does it: scale = 5000 / q below 50, otherwise 200 − 2q; each entry
  becomes (entry × scale + 50) / 100, clamped to 1–255. The diffuse layer uses the luma table for Y
  and the chroma table for Co and Cg; the other layers use the luma table with their own quality.
- **Inverse DCT:** the standard 8×8 JPEG IDCT.
- **Y** gets the usual +128 JPEG level shift.
- **Co and Cg** are stored at half scale: with `c` the IDCT output (no level shift),
  `Co = 2 × (c − 64)`, and the same for Cg. Co is the first chroma block of each area. Then
  `R = Y + Co − Cg`, `G = Y + Cg`, `B = Y − Co − Cg`.
- **Normal channels** are also at half scale: with `n` the IDCT output, the value the game exports
  is `2 × (n + 64)`.
- **Specular and power** get the +128 level shift and are used as is.
- **Colour specular** is reconstructed like the diffuse layer, except that its Co and Cg are at
  half scale around **0**, not 64: `Co = 2 × c`.

**How well it matches the game's own output:** the decoded Co and Cg match the game's exported
physical diffuse texture to within 0.5/255 on average, both normal channels correlate at 0.999 with
the exported normal texture, power matches to about 1.3/255, and the mask matches exactly. Y
correlates at 0.98 with the exported physical Y, which applies a linear remap (about
0.87·Y + 23) that lifts dark values; the RGB file the game exports includes that lift. Which is the
"true" brightness for rendering is still to be confirmed.

**Whole files.** The C++ decoder in [tools/rage-jxr](../../../tools/rage-jxr/README.md) decodes
model pages too, identically to the Python prototype on the ground-truth page. `rage-jxr check`
parses every stream of every page of all three model files, and each one ends within its last
byte: `_vmtr.pages` (215,091 pages), the multiplayer folder's `_vmtr.pages` (136,138) and DLC3's
`_vmtr_dlc.pages` (12,924), 1.46 million streams in all. That check found two things the first
pages hadn't shown:

- About half the bodies have one zero byte after the streams, padding them to an even length.
- About 40% of pages have **colour specular**: byte 6 is 0 and the specular stream has the
  diffuse stream's structure (YCoCg 4:2:0, four Y blocks then two chroma blocks per 16×16 area,
  chroma tables). In a sample, byte 6 = 0 matched the colour structure and byte 6 = 1 the mono
  one in every page.
- **Colour specular's chroma is centred at 0** (inferred; no ground truth has colour specular yet).
  Decoded with the diffuse layer's centre of 64, 150 colour specular pages at level 3 average RGB
  (9, 3, 185): nearly every texel clips and only 0.4% are near grey (channels within 16 of each
  other). With 0 they average (9, 9, 10), 93% near grey, matching the one-channel specular pages'
  average of 10.7; level 5 gives the same. A centre of 32 or −64 is clearly worse. With the wrong
  centre, colour specular decodes as saturated blue and magenta.

#### Ground truth with `vt_export`

The retail game's console command `vt_export <pageSrc> [<level> <x> <y> <w> <h>] [tga]` writes the
decoded texels of a loaded virtual texture to `base\screenshots\` in the game folder (not the
save folder). Region units are texels at that level. `vt_info` lists the page sources: 1 is
`_vmtr`, 2 is `_vmtr_dlc`, and the current map's file comes after. The game maps every page file
into one 2048-page-wide, 12-level space, so a file's level appears in the game as file level +
(12 − the file's level count): +2 for `_vmtr` (10 levels), +1 for an 11-level map. Page (x, y)'s
120×120 payload is texels x·120 to x·120+119. Each export writes four files: the RGB diffuse, the
physical YCoCg diffuse (Co, Cg, 0, Y), the normal/power texture (power, normal Y, 0, normal X) and
the specular/mask texture (specular RGB, mask). The exported images are decoded game data and stay
on the player's machine.

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

- **The exact final colour conversion of map pages.** The Y and chroma scales above are fitted,
  not derived, and decoded luma still differs from the retail `vt_export` layer by about 4
  levels RMS (r = 0.996). The residual is noise-like and largest at the centre of each 4×4
  block. The export may not be exact either: on a model page, whose codec is proven, its Y layer
  shows a brightness-dependent bias. Settling this needs either a cleaner ground truth or the
  game's own conversion.
- **Ground truth for map pages.** In the RAGE Tool Kit (not the retail game, where the command
  isn't registered), `vt_processPageFile -dct -pageFile ../virtualtextures/basic_env_v001.pages
  -outFile basic_env_v001_dct.pages` re-encodes a page file into the DCT format, which decodes
  exactly. It converted all 2,480 pages of the sample map (7.9 MB → 14 MB, written next to the
  original). It is lossy itself (JPEG-like, chroma at half resolution), so it confirms structure
  and scale but can't confirm the map decoder to the last bit.
- **What the low mode bits and the stream-header fields mean**, and which pages are DCT and which
  are HD Photo.
- **Header field 0x08.** It doesn't match any total we tried (bytes per level, leaf bytes, body
  bytes, level boundaries).
- **Page size in pixels**, and whether pages carry a border. The engine log mentions
  `_vmtr_v23_256_C.pages` and 4096 × 4096 physical caches.
- **What `_vmtr.pages` and `_vmtr_dlc.pages` hold.** They are the largest files and use the same
  format, but aren't named after a map. They may be shared textures for models and props (guess).
- **Whether the stale `.pageLines` files cause bad reads** in the original game, and what the
  engine does with the block-crossing flag.
