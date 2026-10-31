# Install inventory: RAGE (Steam, with DLC)

- Game version / build: Steam build ID `1509967`. `Rage64.exe` file version `1.0.35.4669`.
  `Rage.exe` (32-bit) product version `1.0.34.2015`. The renderer log reports
  `Version: 1.1700.354669`.
- Files examined: names and sizes only, from a stock Steam install with all DLC. Small plain-text
  config and log files were read. No game data was copied.
- Tools used: `tools/inventory.ps1` (now replaced by [tools/inventory.py](../../tools/inventory.py)) plus manual PowerShell.
- Confidence: **verified** for layout and sizes; **guess** wherever a file's *purpose* is marked as one.
- Date: 2026-10-28

## Totals

390 files, about **23.8 GB**. More than 80% of that is megatexture page data (`.pages`).

## Folder layout

| Folder | What it holds | Size |
| --- | --- | ---: |
| `\` | `Rage.exe` (32-bit), `Rage64.exe` (64-bit), Bink, Cg, CUDA and Steam DLLs | 71 MB |
| `\base` | Single-player archives, patch, streamed audio, configs | 2.3 GB |
| `\base\video` | Bink videos (intro, outro, loading) | 284 MB |
| `\base\generated` | Engine-generated cache (only 2 `.bcm` files in a stock install) | ~0 |
| `\virtualtextures` | Single-player megatextures: 44 `.pages` + 44 `.pageLines` | 11.7 GB |
| `\mp\base` | Multiplayer archives, patch, streamed audio, configs | 1.1 GB |
| `\mp\virtualtextures` | Multiplayer and co-op megatextures: 25 `.pages` + 25 `.pageLines` | 5.8 GB |
| `\dlc\dlc1` | `info.txt` only | ~0 |
| `\dlc\dlc2\virtualtextures` | 10 `.pages`, all `sewer_level_*` / `sewer_boneyard_*` | 326 MB |
| `\dlc\dlc3\base` | Its own `gameresources.resources`, `streamed.resources` and `english.streamed` | 497 MB |
| `\dlc\dlc3\virtualtextures` | 8 `.pages` (`bash_canyon`, `dlc_*`, `_vmtr_dlc`) | 1.5 GB |
| `\mods` | Empty except `place mods here.txt` | 0 |
| `\SAVES` | The player's saves and screenshots | small |
| `\directx`, `\msvc2010` | Redistributable installers (not game data) | 103 MB |

Single-player and multiplayer are **separate content trees** (`base` + `virtualtextures` and
`mp/base` + `mp/virtualtextures`), each with its own archives. This matters for the "unified
experience" goal.

## File types

| Extension | Count | Size | Purpose (confidence) |
| --- | ---: | ---: | --- |
| `.pages` | 87 | 19.3 GB | Megatexture page data, one per map/area (verified by name and by the engine log below) |
| `.pageLines` | 69 | 48 MB | Small companion to each `.pages` file in `virtualtextures` and `mp/virtualtextures`. DLC folders don't have them. A disk-block index (verified, see [formats/megatexture-pages.md](formats/megatexture-pages.md)) |
| `.resources` | 6 | 3.5 GB | Asset archives. Each tree has `gameresources` + `streamed` (verified by name) |
| `.patch` | 2 | 106 MB | `gameresources_sp_01.patch` / `gameresources_mp_01.patch`. Probably archives that override the base `.resources` (guess) |
| `.streamed` | 3 | 331 MB | `english.streamed`. Probably localized voice/audio (guess) |
| `.bik` | 8 | 358 MB | Bink video (verified, `binkw32/64.dll` present) |
| `.bcm` | 2 | tiny | In `generated/`. Possibly binary collision models (guess) |
| `.cfg` | many | tiny | Plain-text configs (controller layouts, `default.cfg`) |

## DLC mapping

Each `dlc\dlcN\info.txt` names the Steam app that unlocks it:

| Folder | Steam app | Notes |
| --- | --- | --- |
| `dlc1` | 9241 | No content files. Its content probably lives in the main archives or patches (guess) |
| `dlc2` | 9241 | Sewer levels. Probably *The Wasteland Sewers* (guess) |
| `dlc3` | 209460 | *The Scorchers* (size matches Steam depot 209460). Its `info.txt` also has a `hashSeed` for "resource name hashing", which suggests the archives look up files by hashed names. |

## Megatexture parameters seen in an engine log

A leftover `qconsole.log` from a RAGE Tool Kit session (April 2026, `rage tool kit\SAVES\base`)
shows the virtual texture system starting up. Observed values:

- Class name `idVirtualTexture`. Cache file `_vmtr_v23_256_C.pages` with a `.vmtr` index.
  The name *might* mean format version 23 and 256-pixel pages (guess).
- Physical page caches: several **4096×4096 DXT5** textures named `_physicalvmtrpages0-2`,
  `_physicaluniquepages0-2` and `_physicaluniquediffuseonlypages1`.
- Page table: **2048×2048, 12 mip levels, RG format** (`_vmtrpagetable`). `header.pagesWide: 2048`.
- The log prints `Pages resident per level` and `Pages locked`. That diagnostic output will be
  useful when we study pop-in.
- Renderer: OpenGL 3.2 context.

## Existing performance data

`base\rendererPerformanceLogs.txt` (593 KB) is written by the game itself. It holds **965
samples across 30 maps** with per-frame counters: draw elements, indices, vertices, occlusion
queries, CPU time, swap time. `GPUTimeMicroSec` is always 0. This gives us a baseline to compare
against later.

## Leads

1. **Doom 3 BFG Edition source (GPL-3.0, 2012).** The class names and file types here
   (`idSWF`, `idSessionLocal`, `.resources`, `generated/*.bimage`, `.bswf`, `.bcm`) also appear
   in Doom 3 BFG, which shares code with id Tech 5. Its public source may explain RAGE's
   formats closely, though versions will differ. **Licensing impact:** reusing any of that code
   would make OpenRAGE GPL-3.0.
2. **Tiny generated files for first hex-editor practice.** The Tool Kit leftovers include
   `.bimage` files of 70 and 78 bytes (`constantcolor_1_1_1_1_.bimage`, `nofalloff.bimage`).
   They're small enough to decode by hand.
3. ~~**Install the RAGE Tool Kit again.**~~ Done. See [toolkit-inventory.md](toolkit-inventory.md).
4. **Generated shaders.** The Tool Kit cache had 166 shader pairs (`.cg` + `.glsl`), which
   suggests shaders are written in Cg and converted to GLSL.

## Open questions

- ~~What is inside a `.pageLines` file, and why do the DLC folders not have them?~~ A 64 KB
  disk-block index that can be rebuilt from the `.pages` file. See
  [formats/megatexture-pages.md](formats/megatexture-pages.md).
- ~~How do `.patch` files override `.resources`?~~ Most likely by entry name. See
  [formats/resources-archive.md](formats/resources-archive.md).
- How are `.resources` entries found at runtime? Each entry stores readable names (see
  [formats/resources-archive.md](formats/resources-archive.md)), so what `hashSeed` is for is
  still open.
- What exactly does `dlc1` add?
