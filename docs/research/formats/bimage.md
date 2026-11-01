# `.bimage` texture format

- Game version / build: Steam build `1509967` (`Rage64.exe` 1.0.35.4669), with all DLC.
- Files examined: every `.bimage` entry in `base/gameresources.resources`,
  `base/gameresources_sp_01.patch`, `mp/base/gameresources.resources`,
  `mp/base/gameresources_mp_01.patch` and `dlc/dlc3/base/gameresources.resources` (2,810
  textures). Entries were unpacked in memory to read their headers and mip records, then thrown
  away. No pixel data was decoded or copied.
- Tools used: hex dumps, then [tools/bimage.py](../../../tools/bimage.py), written from these
  notes. Compared with Doom 3 BFG Edition's GPL source (`neo/renderer/BinaryImageData.h`,
  `BinaryImage.cpp`, `ImageOpts.h`) and a Wolfenstein: The New Order extraction script.
- Confidence: **verified** for the header fields listed as known, the mip records, cube maps and
  the size of each format (every texture); **partially verified** for the format names (sizes and
  the Wolfenstein script agree, but no pixels were decoded); **unknown** for three small header
  fields.
- Date: 2026-11-01

## What we know

A `.bimage` is one ordinary texture, already converted to the format the GPU uses. They live
inside the `.resources` archives under `generated/`, alongside the asset they were built from.
Most of RAGE's world surfaces are in the megatextures instead (see
[megatexture-pages.md](megatexture-pages.md)), so `.bimage` files cover the rest: the HUD and
menus, fonts, particles and decals, light shapes, environment cube maps and some model textures.
Each main archive holds only 200–290 MB of them.

Numbers are big-endian unless noted, like the archives.

### Header (bytes 0–41)

| Offset | Size | Meaning |
| --- | --- | --- |
| 0x00 | 4 | Time the source file was saved, in seconds since 1970. `FF FF FF FF` for images the engine generates itself (such as `constantcolor_*`), 0 for the Flash UI images |
| 0x04 | 4 | Magic `07 4D 49 42`: "BIM" plus version 7 |
| 0x08 | 4 | Texture type: 0 = 2D, 2 = cube map |
| 0x0C | 4 | Width in pixels |
| 0x10 | 4 | Height in pixels |
| 0x14 | 4 | Unknown. Always 0 |
| 0x18 | 4 | Number of mip levels (1 to 8 seen) |
| 0x1C | 4 | Unknown. 0 or 1 in the first byte, the rest zero |
| 0x20 | 4 | Format code in the first byte (see below), the rest zero |
| 0x24 | 4 | Unknown. 0, 1 or 5 in the first byte, the rest zero |
| 0x28 | 2 | Unknown. 0, 1, 4 or 5 in the first byte, then zero |

The fields from 0x1C on keep their value in the first byte, so they may be little-endian numbers,
unlike the rest of the header (guess).

Source dates in the base archive run from April to August 2011.

### Mip records

Right after the header come the images, one record per mip level (and per face, for cube maps),
largest first. Each record is 20 bytes followed by its data:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0x00 | 4 | Mip level, from 0 |
| 0x04 | 4 | Face: always 0 for 2D textures, 0–5 for cube maps |
| 0x08 | 4 | Width of this level |
| 0x0C | 4 | Height of this level |
| 0x10 | 4 | Size of the data that follows |

A cube map stores all six faces of level 0, then all six of level 1, and so on. In every texture
tested, the records fill the file exactly.

The two tiny `.bimage` files left over from the Tool Kit (see
[install-inventory.md](../install-inventory.md#leads)) fit this layout. Both are BC1 with one mip
level: `constantcolor_1_1_1_1_.bimage` (70 bytes) is the 42-byte header, one record and a single
8-byte block for a 4×4 image; `nofalloff.bimage` (78 bytes) is a 7×4 image, so two blocks.

### Formats

| Code | Bits per pixel | Format | Textures |
| ---: | ---: | --- | ---: |
| 10 | 4 | BC1 / DXT1 (4×4 blocks) | 1,332 |
| 11 | 8 | BC3 / DXT5 (4×4 blocks) | 1,344 |
| 5 | 8 | One channel, uncompressed. Used by fonts | 113 |
| 8 | 8 | One channel, uncompressed | 9 |
| 3 | 32 | Four channels, uncompressed | 12 |

Every level of every texture is exactly the size its format predicts, with BC1 and BC3 sizes
rounded up to whole 4×4 blocks. The BC1 and BC3 names also match the Wolfenstein script, which
decodes codes 10 and 11 that way. What separates codes 5 and 8, and the channel order of code 3,
needs a decoded image to confirm.

BC1 and BC3 are the formats GPUs read directly, so these textures can be uploaded to Vulkan
without any decoding.

### Archive entries

`.bimage` files appear under three entry types: `image` (most textures), `atlas` (particle and
decal sheets) and `file` (images used by the Flash-based UI, under `generated/swf/`).

## Compared with Doom 3 BFG Edition

BFG's `.bimage` is the same design at a different version (10 instead of 7):

- **The same:** the big-endian source time and "BIM" + version magic, texture type 2 for cube
  maps, and the 20-byte mip records (level, face, width, height, size) with cube faces stored the
  same way.
- **Different:** BFG's header is 32 bytes in the order type, format, color format, width, height,
  levels. RAGE's is 42 bytes, puts width and height before the level count, and moves the format
  after it. RAGE uses 0 for 2D textures where BFG uses 1. The format codes are numbered
  differently: BFG has DXT1 = 7 and DXT5 = 8.

The Wolfenstein: The New Order script reads width, height, format and data at other offsets,
which suggests Wolfenstein uses yet another version.

## Open questions

- **The fields at 0x1C, 0x24 and 0x28.** They don't line up with texture type or format alone.
  BFG's header has a color-format field (normal map, YCoCg, and so on) that may correspond to one
  of them.
- **Formats 5, 8 and 3.** Which channels they hold, confirmed by decoding a texture.
- **Why so few model textures.** Some props have `.bimage` textures of their own; most model
  surfaces probably come from `_vmtr.pages` (guess).
