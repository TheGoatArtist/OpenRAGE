# `.bcm` collision model format

- Game version / build: Steam build `1509967` (`Rage64.exe` 1.0.35.4669), with all DLC.
- Files examined: every `.bcm` entry in `base/gameresources.resources` (4,996) and
  `mp/base/gameresources.resources` (1,569), unpacked in memory and parsed, plus a hex reading of
  a few. No collision data was copied.
- Tools used: Python scripts built on [tools/resources.py](../../../tools/resources.py), and
  OpenRAGE's engine experiment (a stripped RBDOOM-3-BFG), which loads these models into Doom 3's
  collision system.
- Confidence: **verified** for the file layout, the tables, polygons, edges, brushes and planes (the
  player stands and lands on RAGE's terrain and structures in the engine); **inferred** for what the
  contents bits mean; **unknown** for several header fields, the edge flag `0x4000`, and the
  streamed collision of the two open-world maps.
- Date: 2026-11-02

## What we know

RAGE's collision models descend from Doom 3's: vertices, edges, polygons made of edge loops,
convex brushes made of planes, and an axial BSP tree with polygon and brush references. They're
archive entries of type `cm`: one per collidable model (`generated/models/…/<model>.bcm`), one per
map trigger and mover (`maps/game/<…>/<map>/*.bcm`), and one for a map's world
(`maps/game/<…>/<map>/_combo/world.bcm`).

All numbers are **big-endian**.

### File layout

| Offset | Size | Meaning |
| --- | --- | --- |
| 0x00 | 4 | Magic `BCM6` |
| 0x04 | 4 | Unknown (varies; perhaps a checksum) |
| 0x08 | 24 | Bounds: minimum and maximum corner, 6 floats |
| 0x20 | 4 | The union of the contents of all materials (0x4000 for a trigger, 1 for a solid prop) |
| 0x24 | 4 | Unknown, usually 0 |
| 0x28 | 4 | Number of top-level tree nodes |
| 0x2C | 4 | Unknown (usually 1) |
| 0x30 | 16 × n | The top-level tree nodes |
| | | The models: per model a 32-byte entry (block size, `0x20`, bounds), then the block |
| end − 4 | 4 | Magic `BCM6` again |

Most files hold one model and no top-level tree. A map's `world.bcm` splits the world into many
model blocks with the tree: dusty8's has 93 nodes and 94 blocks. Its nodes appear to be a split
distance, two children (negative for a block) and an axis; the engine doesn't need them.

### Model block

A block starts with its size (twice), its bounds, a zero, nine (count, offset) pairs and 4 bytes
of padding. The offsets are from the block's start, and the tables follow one another with up to
15 bytes of padding:

| Table | Record | Contents |
| --- | --- | --- |
| Nodes | 16 bytes | Plane type (0, 1, 2 for an axis, −1 for a leaf), distance, two 16-bit children, first reference (16-bit), polygon count and brush count (one byte each) |
| References | 2 bytes | Polygon numbers, then brush numbers, listed by the nodes |
| Materials | 16 bytes | Contents, flags, a small number (perhaps the surface type), and a `FFFFFF..` word |
| Polygons | 16 bytes | Quantized bounds (12 bytes), material (1 byte), edge count (1 byte), first edge reference (16-bit) |
| Edge references | 2 bytes | Edge number in the low 14 bits; `0x8000` reverses the edge; `0x4000` is another flag (unknown) |
| Edges | 4 bytes | Two 16-bit vertex numbers |
| Vertices | 16 bytes | Position (3 floats) and padding |
| Brushes | 16 bytes | Quantized bounds (12 bytes), material (1 byte), plane count (1 byte), first plane (16-bit) |
| Planes | 16 bytes | Normal and d, with normal · point + d = 0; normals face out of the brush |

- A polygon's vertices are the first vertex of each of its edges, in order (the second vertex
  where the edge is reversed). The loop runs **counter-clockwise around the outward normal**, as
  checked on a trigger box. The plane isn't stored and is computed from the vertices.
- Polygon counts per block stay well inside 16 bits (dusty8's largest block has 1,272 polygons and
  2,047 edges).

### Contents

The material's first word is a contents mask. The most common values are 1, 0x8, 0x18, 0x38 and
0x1A0; trigger volumes use 0x4000. Since the collision code descends from Doom 3's, bits 1 to 64
are probably Doom 3's (solid, opaque, water, player clip, monster clip, moveable clip, IK clip), so
0x38 would be invisible clip for players, monsters and moveables (inferred). The other bits,
such as 0x80, 0x100 and 0x800, are unknown; 0x9F8 appears on parts of dusty8's world.

### Check results

With this layout every table fits its block, and every file ends at its closing magic: 4,993 of
the base archive's 4,996 files and all 1,569 of the multiplayer archive's. The exceptions:

- **`wasteland1` and `wasteland2`'s `world.bcm`** (112 KB and 59 KB) list their blocks' entries
  but not the blocks themselves. The two open-world hubs stream their world collision from
  `base/streamed.resources`, a 904 MB file in a different format (it starts `00 16 00 A4`), not
  studied yet.
- One race trigger file doesn't end with the magic (not looked at yet).

## Open questions

- `streamed.resources`, which holds the open-world maps' collision (and perhaps more).
- The contents bits beyond the Doom 3 ones, and the material record's other words.
- The edge flag `0x4000` (perhaps Doom 3's "internal edge").
- The header's unknown fields.
