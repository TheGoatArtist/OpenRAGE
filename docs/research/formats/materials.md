# Material declarations

- Game version / build: Steam build `1509967` (`Rage64.exe` 1.0.35.4669), with all DLC.
- Files examined: all 4,021 `material` entries in `base/gameresources.resources`, unpacked in
  memory and counted. No material text was copied.
- Tools used: Python scripts built on [tools/resources.py](../../../tools/resources.py), and
  OpenRAGE's engine experiment (a stripped RBDOOM-3-BFG) for the on-screen test.
- Confidence: **verified** for the file layout, the counts below and what `virtualmapping` means
  (models drawn with it in the engine experiment); **unknown** for most keys' exact effect.
- Date: 2026-11-01

## What we know

Each material is its own archive entry of type `material`, named
`generated/decls/material/<material name>.decl`. The entry's source name is the material name,
often with a `.tga` extension. The entries are small plain-text files, 2 MB for all 4,021.

### Syntax

Unlike Doom 3, a RAGE material has no stages. It's one brace block holding one setting per line:
a key, a tab, then a value. A value is one of:

- a bare word, such as a program name or `_vmtrpagetable`;
- a quoted string, usually an image path;
- a number;
- a brace tuple of numbers, such as `{ 1.000000, 512.000000, ... }`.

A few keys use mixed case (`stageProgram`, `virtualMapping`), so matching should ignore case.

### Most common keys

| Key | Materials | What it seems to be |
| --- | ---: | --- |
| `pagetablemap` | 3,864 | Almost always `_vmtrpagetable`: the material samples the shared virtual texture |
| `virtualtexturefeedbackfloat` | 3,864 | A tuple, probably for the virtual texture feedback pass |
| `virtualmapping` / `usevirtualmapping` | 2,995 | Where the material's texture sits inside the virtual texture (see below) |
| `diffusemap`, `bumpmap`, `specularmap` | ~2,200–2,400 each | Quoted source image paths (`.tga`) |
| `renderbump` | 1,806 | Build-time options for baking normal maps; not used at runtime (guess) |
| `powermip` | 1,798 | A number, probably for the specular power maps |
| `stageprogram` | 1,515 | The render program for the material's main pass, such as `virtualguiblend` or `basicadd` |
| `transmap` | 758 | A translucent texture, usually an ordinary `.bimage` |
| `ambientprogram` | 598 | Programs such as `charactervmtr` and `weaponvmtr` |
| `surfacetype` | 765 | Footstep and impact type, such as `surftype_metal` (18 types) |
| `interactionprogram` | 254 | Lighting programs such as `intergrazing` and `intertwoside` |

Program names refer to the archive's `renderProg` entries (479 of them).

### Materials and megatextures

- **96% of materials (3,864) are virtual-textured.** Their `diffusemap`, `bumpmap` and
  `specularmap` name source art that is not in the archives as a `.bimage`: it was baked into the
  megatexture page files.
- **`virtualmapping { w, h, x, y }` places the material in the shared virtual texture.** It is a
  rectangle in `virtualtextures/_vmtr.pages`, as fractions of its whole width and height: w and h
  are the size, such as 0.003906 (1/256) or 0.007813 (1/128), and x and y the top-left corner.
  A model's texture coordinates (u, v) land at (x + u·w, y + v·h). Verified by decoding the
  rectangles and drawing models with them; see [bmodel.md](bmodel.md). The 2,995 materials that
  use it each have their own value.
- **Land materials name their map's page file.** 69 materials have `landPageFile` (such as
  `"dusty8"` or `"bash_tv"`), `landModelFile` and `landDimensions`. These are the terrain
  materials for maps with their own `.pages` file.
- **Only 822 materials use ordinary textures**, mostly as `transmap` (605), plus editor images,
  light projections and falloffs, scanline overlays and environment maps. These can already be
  loaded; see [bimage.md](bimage.md).

So almost every RAGE world and model surface needs the megatexture page codec before it can be
shown with its own textures.

### Engine test

In the engine experiment, a test material using RAGE's YCoCg sky texture
(`models/mapobjects/skies/w1_skydome_01`) as a diffuse map was drawn on a panel. The engine's
lighting shader converted the YCoCg and the sky showed with correct colors. Ordinary RAGE
textures can therefore be drawn correctly on surfaces, as long as the engine knows which ones are
YCoCg.

## Open questions

- What `virtualtexturefeedbackfloat` holds.
- What each render program does, from the `renderProg` entries.
- Whether RAGE's diffuse textures are all YCoCg, as in Doom 3 BFG, or only some.
