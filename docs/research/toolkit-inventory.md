# Install inventory: RAGE Tool Kit

- Version / build: Steam app `205860`, build ID `43627`. Its `Rage64.exe` reports file version
  `1.0.35.4669`, the same as the game's.
- Files examined: names and sizes of the whole install, the first bytes of a few files of each
  type, the list of entries in one `.pk5`, and three pages of the bundled documentation. No
  Tool Kit content was copied into the repository.
- Tools used: [tools/inventory.py](../../tools/inventory.py) plus manual PowerShell.
- Confidence: **verified** for layout, sizes and the first bytes; **guess** wherever a file's
  *purpose* is marked as one.
- Date: 2026-10-28

> **Read [legal.md](../legal.md#rage-tool-kit-terms) first.** The Tool Kit has its own terms.
> Nothing from it, and nothing it builds, goes into this repository.

## Summary

38,682 files, about **33.6 GB**. The game ships its assets packed into `.resources` archives,
but the Tool Kit contains the **source assets**, much of it as plain text. It can also **build**
`.resources` and `.pages` files from them. That makes it the best reference we have for the
game's formats.

## Folder layout

| Folder | Files | Size | What it holds |
| --- | ---: | ---: | --- |
| `\` | 7 | 46 MB | `Rage64.exe` (engine with the idStudio editor), Cg, CUDA, Bink and Steam DLLs |
| `\docs` | 173 | 26 MB | Official HTML documentation for idStudio (36 pages) |
| `\virtualtextures` | 13 | 1.9 GB | Megatexture files for the maps shipped as source |
| `\mods` | 0 | 0 | Where built mod packages are written |
| `\base\models` | 6,135 | 15.0 GB | Model source: `.lwo` meshes and `.tga` textures |
| `\base\stamps` | 1,952 | 4.9 GB | Megatexture stamp images |
| `\base\md6` | 4,711 | 3.4 GB | Skeletal models and animations (`.md6mesh`, `.md6anim`) |
| `\base\textures` | 2,061 | 3.0 GB | Textures |
| `\base\land` | 126 | 1.4 GB | Megatexture layer files (`.land`) |
| `\base\terrain` | 88 | 1.3 GB | Terrain source |
| `\base\compressed` | 11,335 | 898 MB | Sounds (`.msadpcm`) |
| `\base\cache` | 490 | 640 MB | Binary caches (`.data` + `.index` pairs) |
| `\base\maps` | 124 | 397 MB | Level sources (`.map`) and built level data (`.pk5`) |
| `\base\swf` | 118 | 269 MB | Flash UI sources (`.fla`) and built `.swf` files |
| `\base\decls` | 1,915 | 51 MB | Text definitions, in 25 subfolders |
| `\base\voicetrack` | 8,353 | 34 MB | Lip-sync tracks (`.vtr`) |
| other `\base` folders | 904 | 311 MB | `env`, `fonts`, `lights`, `lights_blended`, `strings`, `script`, `editors`, `aas`, `video`, `generated` |

## Formats

| Type | Count | Text or binary | First bytes / notes |
| --- | ---: | --- | --- |
| `.map` | 12 | Text | Starts `Version 3`, then `entity { entityDef world { inherit = "worldspawn"; ... } }` with `brushDef3` brushes. Close to Doom 3's map format, but the version and the entity syntax differ. |
| `.pk5` | 9 | Binary | Starts `50 4B 03 04`: an **ordinary ZIP**. `wellspring.pk5` holds 2,828 entries: `wellspring.proc`, collision models (`.bcm`) and brush models (`.bmodel`). |
| `.md6mesh` | 194 | Text | Starts `MD6Version 1`, then an `init { commandLine "-ex mesh ..." }` block. Likely the successor to Doom 3's `.md5mesh` (guess). |
| `.md6anim` | 4,475 | Text | Starts `MD6Version 5`, same `init` block |
| `.decl` | 27 | Text | Material-like blocks (`stageProgram`, `depthFillProgram`) |
| `.def` | 98 | Text | `entityDef` blocks with C-style `#include` |
| `.m2` | 321 | Text | Starts with a comment banner. In `decls/m2` and `decls/m2_terrain`. Possibly materials (guess). |
| `.vtr` | 8,353 | Text | `voiceTrack { visemeSet ... }`: lip-sync data |
| `.land` | 10 (+ `.land1`/`2`/`3`/`8`) | Text | Starts `newFile`. Per the docs, the megatexture's editable layers ("the .psd to the megatexture's .png") |
| `.lang` | 9 | Text | String tables (`#str_...` keys) |
| `.msadpcm` | 11,336 | Binary | Starts `RIFF`: a standard WAV container with MS-ADPCM audio |
| `.data` / `.index` | 245 each | Binary | Start with `idCacheF` / `idIndexF`. Tool Kit caches. |
| `.vd5` | 13 | Binary | Starts `.5DV` |
| `.tga`, `.lwo`, `.fla`, `.swf` | — | Binary | Standard formats (Targa, LightWave, Flash) |

`base\decls` subfolders: `af, animwebs, cloth, def, detail, ebolts, effects, env, flares,
foliage, fx, ik, m2, m2_terrain, md6def, particles, reachik, renderprogs, ribbons, skins,
sound, typeinfo, vehicleProps, visemes, voiceover`.

## Shipped maps

Sources (`.map` + built `.pk5`) for: `wellspring`, `well`, `dusty8` (race track), three AI test
maps, two tutorial maps (`basic_env`, `advanced_env`) and `doNotTouch`.

## Rage64.exe

The Tool Kit's `Rage64.exe` has the **same version and the same exact size** (41,502,728
bytes) as the game's, but different contents (SHA-256 starts `660EDE52` vs `54C80B45`). Both
are probably the same engine build, with the editor enabled or included in the Tool Kit's
copy (guess). Comparing the two in Ghidra could show exactly what differs.

## How mods are built

From the bundled `Building and Sharing Mods` page:

- In idStudio, **Build → Build and Share Mod** packages one or more maps into
  `rage tool kit\mods\<name>\`.
- A package has a `base` folder (a large resource file, generated language files and configs)
  and a `virtualtextures` folder (a `.vmtr` and one `.pages` per map).
- Players copy it into `RAGE\mods\`, and the game shows a **Mods** menu entry that reboots the
  game with that package.
- The build temporarily swaps the main menu (`shell_redo.swf`) for a version with a Mods button.

## From the documentation: megatexture layers

The `Mega editor` page says megatextures cover **all** level geometry, not just terrain.
They're built from three layers stored in `.land` files:

1. **Source geometry textures:** the materials on the level's geometry, stitched together.
2. **Tiles:** a grid of tiling textures painted over layer 1, blended at cell edges.
3. **Stamps:** non-tiled images projected onto the geometry, which can affect diffuse, bump or
   specular separately, and stay editable and reorderable.

Only one megatexture can be edited per idStudio session.

## Leads

1. **Build small known test files.** Build the `basic_env` tutorial map as a mod. The output
   is a small `.resources` + `.pages` pair whose contents we know. That makes it far easier to
   map the archive and page formats than the 1.2 GB game archive. Keep the output on your own
   machine (see [legal.md](../legal.md#rage-tool-kit-terms)).
2. **Crack name hashing with known names.** If `.resources` stores hashed names (the
   `hashSeed` in `dlc/dlc3/info.txt` hints at this), the Tool Kit gives about 38,000 real asset
   paths. Hash them with candidate methods until the results match the archive.
3. **Read the text formats.** `.map`, `.md6mesh`, `.md6anim`, `.decl` and `.def` are readable
   as-is. They show what the engine needs to load, which a reimplementation must support.
4. **`.pk5` is ZIP.** Built level data can be listed with any ZIP tool, which makes `.proc` and
   `.bcm` easy to study and compare with Doom 3 BFG's versions.
5. **The two `Rage64.exe` files.** Diffing them may reveal editor-only code paths and
   developer settings.

## Open questions

- What does a built mod's `.resources` look like compared with `gameresources.resources`?
- Does the Tool Kit's `Rage64.exe` differ only in a few bytes, or in large sections?
- What exactly are `.m2` and `.vd5`?
- How does `.proc` compare with Doom 3 BFG's `.proc`?
