# Roadmap (draft)

> These are **possible phases under discussion**. They're not a finalized roadmap, and they
> don't mean implementation has started.

## Start here: the first milestone

The first milestone is to **learn to read the game's files, and measure megatexture pop-in
before trying to fix it.** Two tracks run at the same time and meet at one decision:

```
             ┌─ M1 Archives ──────── M2 Megatexture pages ─┐
M0 Setup ────┤                                             ├── M4 Decide what to build
             └─ M3 Pop-in baseline (no code) ──────────────┘
```

| Who | Setup |
| --- | --- |
| TheGoatArtist | Project lead, native Windows install |
| akacross | CachyOS, Steam with Proton Experimental |

### What we already know

From both installs (Steam build `1509967`) and the Doom 3 BFG source. Only file names, sizes,
small text files and the first bytes of each archive were read.

- **Both installs are the same build.** Every game data file matches in count and size: 87
  `.pages`, 69 `.pageLines`, 6 `.resources`, 2 `.patch`, 3 `.streamed`, 8 `.bik`. Proton and
  Windows installs are interchangeable for research.
- **A `.patch` is the same container format as a `.resources`.** `gameresources.resources` and
  `gameresources_sp_01.patch` both start with the bytes `22 94 AB CD`.
- **RAGE's archive layout differs from Doom 3 BFG's.** BFG archives start with `0xD000000D`.
  Use BFG to learn the concepts (header, file table, entries), not as a spec.
- **There's a second archive format.** `streamed.resources` and `english.streamed` don't start
  with that magic.
- **The `.pages` header matches the engine log.** A `.pages` file starts with `0x77339904`
  (stored little-endian), the same `header.magic` the engine prints in `qconsole.log`. The log
  also gives `pagesWide 2048`, `numLevels 12`, `layoutVersion 0` and `diskOffsetScale 2`. Those
  are the fields to find in the file.
- **The physical page cache size is the first pop-in setting to test.** A retail log shows
  16384×16384 DXT5 physical page caches, with id5Tweaker forcing `vt_pageImageSize*`. The Tool
  Kit log in [install-inventory.md](research/install-inventory.md) shows 4096×4096.
- **Doom 3 BFG code to read:** `neo/framework/File_Resource.h` / `.cpp` (class
  `idResourceContainer`) for `.resources`, and `neo/renderer/BinaryImage.cpp` /
  `BinaryImageData.h` for `.bimage`. RBDOOM-3-BFG has the same files but changed the `.bimage`
  version from 10 to 11, so check details against
  [id's original](https://github.com/id-Software/DOOM-3-BFG).

### First tasks

**TheGoatArtist**

1. Check your install is stock: no `dinput8.dll`, `id5Tweaker.ini`, ReShade or other mods in the
   RAGE folder. If you use id5Tweaker, say so.
2. Record the stock Wellspring clip (M3) and save the logs.
3. Open `gameresources.resources` in ImHex. Note the first 64 bytes and look for values that
   could be a table offset or an entry count (M1).
4. Warm-up: decode one of the 70-byte Tool Kit `.bimage` files using BFG's header layout.

**akacross**

1. Record the stock Wellspring clip on Proton. The install is already set to stock.
2. Read BFG's `idResourceContainer` and write `research/formats/resources-archive.md` in your
   own words (M1).
3. Open a small PR: fix the inventory notes and make `tools/inventory.py` skip `SAVES/` (M0).

**Both**

1. Take a short call to settle the three decisions below. Record them in [legal.md](legal.md)
   and [CONTRIBUTING.md](../CONTRIBUTING.md).
2. Create one GitHub issue per milestone step so the work is visible and can be split up.

### M0: Setup and decisions

*Both. About half a day.*

The repo is hosted now, but [CONTRIBUTING.md](../CONTRIBUTING.md) still says the workflow will
be set up later. Two people working in parallel need it in place first.

- Settle license, reverse-engineering approach and workflow (see [Decisions](#decisions)).
- Write the workflow section in `CONTRIBUTING.md`: a branch per change, the other person
  reviews, merge after approval.
- Fix [install-inventory.md](research/install-inventory.md): the Linux install has no
  `dlc/dlc1` folder; `base/generated/` is created by the engine and isn't in a fresh install;
  add the missing `mp/base/video` row (3 `.bik`, 74 MB).
- Make [tools/inventory.py](../tools/inventory.py) skip `SAVES/` so file totals compare between
  machines.

**Done when** the decisions are written down and the inventory PR is merged.

### M1: Read the archives

*akacross reads BFG; TheGoatArtist works in ImHex.*

Almost every asset (maps, models, materials, declarations, images) is inside `.resources`.
Until we can list what's in them, nothing else can be studied. This is the gate for the whole
project.

1. Write notes on how BFG's container works: header fields, where the file table lives, what
   each entry stores, how the game finds a file by name.
2. In ImHex, map RAGE's header after the `22 94 AB CD` magic. Save the pattern as
   `research/formats/resources.hexpat`.
3. Find the file table. Check whether it stores names or hashes. The `hashSeed` in
   `dlc/dlc3/info.txt` ("resource name hashing") suggests hashes.
4. Write `tools/resources_list.py`. It lists entries (name or hash, offset, size) from the
   user's own install and extracts nothing, like `inventory.py`.
5. Compare `gameresources_sp_01.patch` with `gameresources.resources` to see how a patch
   overrides entries.
6. Start identifying the second format used by `streamed.resources` and `.streamed`.

**Done when** the tool lists every entry in each `gameresources.resources` (single-player,
multiplayer and DLC3) and both `.patch` files, and the entry sizes fit inside each file's size.

### M2: Read the megatexture pages

*Both. Can start alongside M1.*

The megatexture is the heart of the project's goals, and its files sit outside the archives. We
also have the engine's own printout of the header to check against.

1. Write `pages.hexpat` for the header and find where `pagesWide`, `numLevels`,
   `layoutVersion` and `diskOffsetScale` are stored.
2. Pick the smallest `.pages` / `.pageLines` pair and work out what `.pageLines` holds. Find out
   why the DLC folders don't have them.
3. Read J.M.P. van Waveren's published id Tech 5 talks and papers on virtual texturing and DXT
   transcoding for background.
4. Write `tools/pages_info.py`: header fields and page counts per mip level.

Decoding page pixels is the hard part. Don't block on it. Map the layout first.

**Done when** the tool's output for `_vmtr.pages` matches what `qconsole.log` prints, and it
runs on all 87 `.pages` files.

### M3: Pop-in baseline

*Both. No code. Can start today.*

The [vision](vision.md) says to profile before fixing. These clips and logs are the first real
data on what makes pop-in better or worse, and on Proton versus Windows.

1. Both installs stock, same save, same route into Wellspring, 30 seconds, same in-game
   settings (resolution, anti-aliasing, vsync, texture settings).
2. Write down GPU, driver, OS, and on Linux the Proton version. akacross: RTX 3070, driver
   615.78.08, Proton Experimental 11.0-100, 1920×1080.
3. After each run, keep `SAVES/base/qconsole.log` and `base/rendererPerformanceLogs.txt`. Copy
   the relevant numbers into notes. If the log isn't written, add `+set logfile 2` to the Steam
   launch options.
4. Then change one setting at a time: `vt_pageImageSize*` (4096, 8192, 16384), `vt_maxPPF` (16,
   32, 64), frame cap (60 vs 144), and on Windows with NVIDIA, `vt_useCudaTranscode`. CUDA
   transcoding probably isn't available under Proton; the log will say.
5. Record each run in `research/engine/megatexture-popin.md` as a table: run, machine, settings,
   what you saw, key log lines.

**Done when** both stock clips are recorded, there's at least one single-setting sweep, and the
notes say which settings change pop-in and by how much.

### M4: Decide what to build first

*Both. After M1 to M3.*

With the formats readable and pop-in measured, we can choose a direction based on evidence.

- **A. Improve the original game.** A loader inside the real `Rage64.exe`, the way id5Tweaker
  works. It gives the fastest visible results and keeps the original game at the center. It
  needs Ghidra and the reverse-engineering rule settled. Nothing from the game is
  redistributed.
- **B. Reimplement the engine.** An OpenMW-style engine that reads RAGE data from the user's
  install, possibly starting from RBDOOM-3-BFG. This is phase 2 below and the long road.

**Recommendation:** do both in small form. Build a measurement overlay (A) that shows page
residency and transcode time in-game, because video clips are a blunt instrument. As B's first
step, build a viewer that renders one megatexture region from a `.pages` file.

**Done when** the choice and the reasons are written in this file.

### Decisions

| Decision | Recommendation | Why |
| --- | --- | --- |
| License | GPL-3.0 | Doom 3 BFG and RBDOOM-3-BFG are the closest public relatives of id Tech 5, and both are GPL-3.0. GPL keeps the option of reusing their code. MIT or zlib would rule that out. |
| Reverse-engineering approach | Documented understanding | A strict clean room needs two separate groups. With two people, one of us could never write code. Either way, no decompiled or disassembled code goes in the repo. |
| Workflow and tools | Branch and PR for every change, the other person reviews. Python for research tools now. | The engine language and build system can wait until M4, when we know what we're building. |

See [legal.md](legal.md) for the full options.

### Rules

- Never commit game files or decompiled code. Read from the user's install instead. See
  [CONTRIBUTING.md](../CONTRIBUTING.md).
- Every note says which build, which file and how you found it, so the other person can check
  it.
- Save ImHex pattern files (`.hexpat`) in the repo so both of us can load them.

## Phases

1. **Research.** Document the engine and its data formats: archives, megatexture pages, maps,
   models and scripts. M1 to M3 are its first deliverables.
2. **Proof of concept.** Load and render a real game environment from the user's own install.
   The megatexture viewer from M4 is the first step.
3. **Rendering and streaming.** Profile megatexture streaming, find the bottlenecks, and
   improve them. The M3 baseline is the "before" for every fix.
4. **Gameplay compatibility.** Work toward the original game being playable.
5. **Modding tools.** Expand on the RAGE Tool Kit.
6. **Multiplayer.** Preserve the original modes and explore careful expansion.
7. **Unified application.** Explore a single 64-bit experience.

Phases 4 to 7 all need the archive reader from M1. Its file listing shows where scripts, maps
and multiplayer data live.

## Open questions

- How will the code be structured (engine, tools, launcher)?
- Which language and build system? C++ with CMake is the usual choice for projects like this.
  This waits for M4.
- Which license? See [Decisions](#decisions) and [legal.md](legal.md).
