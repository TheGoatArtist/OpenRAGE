# `.bmodel` static model format

- Game version / build: Steam build `1509967` (`Rage64.exe` 1.0.35.4669), with all DLC.
- Files examined: all 30,649 `.bmodel` entries in `base/gameresources.resources`, unpacked in
  memory and parsed, plus a hex reading of a few small ones. No geometry was copied.
- Tools used: Python scripts built on [tools/resources.py](../../../tools/resources.py), the
  megatexture decoder in [tools/rage-jxr](../../../tools/rage-jxr/README.md), and OpenRAGE's
  engine experiment (a stripped RBDOOM-3-BFG), which loads and draws these models.
- Confidence: **verified** for the file layout of vertex format `0x21F`, the triangle strips and
  their winding, and how texture coordinates reach the `_vmtr` megatexture (drawn correctly in the
  engine); **inferred** for the vertex normal and color encodings; **unknown** for the other
  vertex formats, several header and trailer fields, and the 10 per-surface floats.
- Date: 2026-11-01

## What we know

`.bmodel` files are RAGE's built static models: archive entries of type `model`, named like the
`.lwo` source they were built from (the entry's source name), under `generated/models/`,
`generated/terrain/` and `maps/game/<map>/`. The base archive holds 30,649 of them. Skeletal models
are a different format, `.bmd6model` (305 entries, not studied yet).

All numbers are **big-endian** except name lengths, which are little-endian.

### File layout

| Part | Contents |
| --- | --- |
| Header | Magic `18 4C 4D 42`, the source file's time, the surface count |
| Surfaces | One record per surface (below) |
| Trailer | The surface count again, then per surface its material name and three integers |

A **name** is a 4-byte little-endian length followed by that many characters, with no terminator.

Each **surface** record:

| Field | Size | Meaning |
| --- | --- | --- |
| Material | name | The surface's material, such as `textures/sawmill/metal12` |
| Unknown | 4 | Usually 0; also −1 or a small number |
| Vertex count | 4 | |
| Index count | 4 | |
| Vertex format | 4 | Flags; `0x21F` is the format described below |
| Scales and offsets | 10 floats | `1 1 1 0 0 0 1 1 0 0` in format `0x21F`. In the packed formats they hold values such as a 0.25 scale with large offsets, so probably position and texture coordinate scales and offsets (guess) |
| Vertexes | count × size | |
| Indexes | count × 2 | 16-bit vertex numbers |
| Bounds | 6 floats | Minimum and maximum corner |
| Unknown | 4 | 0 in every surface checked |
| End marker | 4 | The magic, `18 4C 4D 42`, again |

In the trailer, the last of the three integers equals the surface's vertex count minus one in the
models checked; the other two are unknown.

### Vertex format `0x21F`

32 bytes per vertex:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 12 | Position, 3 floats |
| 12 | 8 | Texture coordinates, 2 floats |
| 20 | 4 | Normal: 3 bytes with 128 as zero (value − 128) / 127, then a pad byte (inferred) |
| 24 | 4 | Tangent, same encoding; the fourth byte is 0 or 255, probably the bitangent's sign (guess) |
| 28 | 4 | Color, RGBA; mostly mid-grey such as `C8 C8 C8 FF`. Its use is unknown |

Positions use Doom 3's axes and units (Z up); the models draw upright and to scale in the engine.

864 of the models under `generated/models/` (props, weapons, vehicles and characters) use only this
format, and every one of their surfaces parses to its end marker and the file to its end.

### Indexes

The indexes are **triangle strips**, separated by `FFFF`. Within a strip, triangle k uses vertexes
k, k+1, k+2, with the first two swapped when k is odd. Read that way, every non-degenerate
triangle of the models checked faces the way its vertex normals point in Doom 3's convention, so
the strips can be drawn with id's usual front faces. Strips also contain degenerate triangles
(a repeated vertex) that join pieces; they should be dropped.

### Materials and texture coordinates

A surface's material is the declaration `generated/decls/material/<name>.decl` (see
[materials.md](materials.md)). For the 96% of materials that use the shared virtual texture
(`pagetablemap _vmtrpagetable`):

- `virtualmapping { w, h, x, y }` is the material's rectangle in the `_vmtr` megatexture, as
  fractions of the whole texture of `virtualtextures/_vmtr.pages`: w and h are its width and
  height, x and y its top-left corner.
- A vertex's texture coordinates (u, v), between 0 and 1, land at
  (x + u·w, y + v·h). The origin is top-left; nothing is flipped.

This was checked by decoding the keycard material's rectangle from `_vmtr.pages` and drawing the
keycard model's texture coordinates over it: every triangle edge lies on the texture's outlines.
In the engine experiment, models such as the Authority's Predator vehicle draw with their full
textures this way. For a sense of scale: `_vmtr.pages` is 512 pages (61,440 texels) across at its
most detailed level, so the keycard's 1/128 × 1/128 rectangle is 480 × 480 texels there.

Materials that don't map into `_vmtr` (additive glows, translucent and GUI materials, for example)
use ordinary textures and other render programs; those aren't handled yet.

### Other vertex formats

Map geometry uses other formats. Its files are `maps/game/<map>/_combo/_area<N>.bmodel` (one per
map area) and `megamodel_func_*.bmodel`. Formats seen include `0x263`, `0x277`, `0x1F`, `0x77`
and `0x63`, and their 10 floats look like real scales and offsets, so their positions and texture
coordinates are probably packed into small integers. Their vertex sizes and layouts aren't known
yet. 3,732 of the map files hold no surfaces at all.

## Open questions

- The layouts of the packed vertex formats, which hold all map geometry and point into each map's
  own megatexture.
- The unknown integer after the material name, and the trailer's first two integers.
- What the vertex color is for.
- Whether the tangent's fourth byte is the bitangent's sign.
- `.bmd6model`, the skeletal model format.
