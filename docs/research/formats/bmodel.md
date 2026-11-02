# `.bmodel` static model and map geometry format

- Game version / build: Steam build `1509967` (`Rage64.exe` 1.0.35.4669), with all DLC.
- Files examined: every `.bmodel` entry in `base/gameresources.resources` (30,649),
  `mp/base/gameresources.resources` (11,799) and `dlc/dlc3/base/gameresources.resources` (7,863
  of its own), unpacked in memory and parsed, plus a hex reading of a few small ones. No geometry
  was copied.
- Tools used: Python scripts built on [tools/resources.py](../../../tools/resources.py), the
  megatexture decoder in [tools/rage-jxr](../../../tools/rage-jxr/README.md), and OpenRAGE's
  engine experiment (a stripped RBDOOM-3-BFG), which loads and draws these models and whole maps.
- Confidence: **verified** for the file layout, all six vertex formats, the index forms and their
  winding, the material ranges, and how texture coordinates reach the `_vmtr` megatexture and a
  map's own megatexture (all drawn correctly in the engine); **inferred** for the vertex normal and
  color encodings; **unknown** for a few header and trailer fields.
- Date: 2026-11-01

## What we know

`.bmodel` files are RAGE's built static models: archive entries of type `model`, named like the
`.lwo` source they were built from (the entry's source name). They hold both standalone models
(under `generated/models/` and `generated/terrain/`) and **map geometry** (under
`maps/game/<map>/_combo/`). Skeletal models are a different format, `.bmd6model` (305 entries,
not studied yet).

All numbers are **big-endian** except name lengths, which are little-endian.

### File layout

| Part | Contents |
| --- | --- |
| Header | Magic `18 4C 4D 42`, the source file's time, the surface count |
| Surfaces | One record per surface (below) |
| Trailer | Material ranges: a count, then per range a material name, the surface it applies to, and its first and last vertex |

A **name** is a 4-byte little-endian length followed by that many characters, with no terminator.

Each **surface** record:

| Field | Size | Meaning |
| --- | --- | --- |
| Material | name | The surface's material, such as `textures/sawmill/metal12` |
| Unknown | 4 | Usually 0; also −1 or a small number |
| Vertex count | 4 | |
| Index count | 4 | |
| Vertex format | 4 | Flags; see "Vertex formats" |
| Scales and offsets | 10 floats | Position scale (x, y, z), position offset (x, y, z), texture coordinate scale (s, t), texture coordinate offset (s, t) |
| Vertexes | count × size | |
| Indexes | count × 2 | 16-bit vertex numbers |
| Bounds | 6 floats | Minimum and maximum corner |
| Unknown | 4 | 0 in every surface checked |
| End marker | 4 | The magic, `18 4C 4D 42`, again |

**Material ranges.** Most models list one range per surface, naming the surface's own material
and covering all its vertexes. Some surfaces carry several materials: a surface named
`genericvmtr`, for example, splits its vertexes between a tire material and a vehicle wreck
material. Each vertex range then uses its own material. The ranges are
(material, surface, first vertex, last vertex); dusty8's map area files have none.

### Vertex formats

Six format values appear. The `0x200` flag selects the index form (below); the rest of the value
selects the vertex layout:

| Formats | Vertex size | Contents |
| --- | --- | --- |
| `0x21F`, `0x1F` | 32 bytes | Position (3 floats), texture coordinates (2 floats), normal (4 bytes), tangent (4 bytes), color (4 bytes) |
| `0x277`, `0x77` | 20 bytes | Position (3 signed shorts, then `01 00`), texture coordinates (2 signed shorts), normal (4 bytes), tangent (4 bytes) |
| `0x263`, `0x63` | 12 bytes | Position (3 signed shorts, then `01 00`), texture coordinates (2 signed shorts); no normal |

- **Packed values** are scaled and offset by the surface's 10 floats: x = short × scale + offset,
  and the same for each position axis and both texture coordinates. Map geometry typically uses a
  0.25 position scale (a quarter unit) with offsets near the area's position.
- **Normal and tangent**: 3 bytes with 128 as zero, (value − 128) / 127, then a fourth byte: a pad
  byte for the normal, and 0 or 255 for the tangent, probably the bitangent's sign (guess).
- **Color**: RGBA, mostly mid-grey such as `C8 C8 C8 FF`. Its use is unknown.

Positions use Doom 3's axes and units (Z up); models and maps draw upright and to scale in the
engine.

### Indexes

- **With `0x200` set: triangle strips**, separated by `FFFF`. Within a strip, triangle k uses
  vertexes k, k+1, k+2, with the first two swapped when k is odd. Read that way, every
  non-degenerate triangle checked faces the way its vertex normals point in Doom 3's convention.
  Strips also contain degenerate triangles (a repeated vertex) that join pieces; they should be
  dropped.
- **Without it: a plain triangle list**, three indexes per triangle.

### Check results

Parsed with this layout, every surface ends at its end marker and every file at its end:

| Archive | Files | Surfaces by format |
| --- | ---: | --- |
| `base/gameresources.resources` | 30,647 of 30,649 | `0x263` 24,272 · `0x277` 22,669 · `0x1F` 1,745 · `0x21F` 1,660 · `0x77` 475 · `0x63` 88 |
| `mp/base/gameresources.resources` | 11,799 of 11,799 | `0x277` 12,023 · `0x263` 10,652 · `0x21F` 991 · `0x1F` 662 · `0x77` 246 · `0x63` 58 |
| `dlc/dlc3/base/gameresources.resources` | 7,863 of 7,863 | (its other `.bmodel` entries point into the base game's archive) |

The two exceptions are `*_occlusion.bmodel` files, which start with a different value and are a
different structure (probably occlusion geometry; not studied). 3,732 of the base archive's map
files hold no surfaces at all.

### Materials and texture coordinates

A surface's material is a declaration: usually `generated/decls/material/<name>.decl`, but map
materials can name their own file, such as `maps/game/tracks/dusty8/mega.decl` (see
[materials.md](materials.md)). Two kinds place their texture in a megatexture:

- **`_vmtr` materials** (`pagetablemap _vmtrpagetable`, 96% of materials). `virtualmapping
  { w, h, x, y }` is the material's rectangle in `virtualtextures/_vmtr.pages`, as fractions of its
  whole texture: w and h are its width and height, x and y its top-left corner. A vertex's texture
  coordinates (u, v), between 0 and 1, land at (x + u·w, y + v·h); the origin is top-left and
  nothing is flipped. For scale: `_vmtr.pages` is 512 pages (61,440 texels) across at its most
  detailed level, so the keycard's 1/128 × 1/128 rectangle is 480 × 480 texels there.
- **Land materials**, used by map geometry (`landPageFile "<megatexture>"`). The texture
  coordinates are positions in that map's own megatexture, as fractions of its whole texture,
  directly; no rectangle is involved. A map can use several: dusty8's `mega`, `meganospec` and
  `megatrans` materials read `dusty8.pages`, and its terrain material reads
  `dirttrack_terrain.pages`. RAGE bakes the lighting into these textures (`landUniqueDiffuseOnly`
  and similar render programs).

How these were checked:

- The keycard model's texture coordinates, drawn over its decoded `_vmtr` rectangle, put every
  triangle edge on the texture's outlines.
- A top-down picture of dusty8, painting each terrain triangle from its megatexture coordinates,
  joins hundreds of separately stored areas into one continuous track and landscape.
- In the engine experiment, models such as the Authority's Predator vehicle draw with their full
  textures, and the whole of dusty8 (344 area and object models) draws with its megatextures.

Materials that use neither (additive glows, translucent, GUI and sky materials, for example) use
ordinary textures and other render programs; those aren't handled yet.

### Map geometry

A map's geometry is `maps/game/<map>/_combo/_area<N>.bmodel`, one file per area of the map's BSP,
plus `megamodel_*.bmodel` files for movers, doors and other dynamic pieces, `sky.bmodel`, and one
`<map>_occlusion.bmodel`. The map's `_combo/<map>.proc` file (text, `mapProcFile005`) holds the BSP
nodes and portals that split the world into those areas, but no geometry. dusty8 has 316 areas and
spans about 155,000 × 103,000 units.

## Open questions

- The `*_occlusion.bmodel` structure.
- The unknown integer after the material name.
- What the vertex color is for, and whether the tangent's fourth byte is the bitangent's sign.
- How RAGE draws materials that use neither megatexture (glows, translucent surfaces, skies).
- `.bmd6model`, the skeletal model format.
