# The engine

[engine/](../engine/) holds the OpenRAGE engine. It's [RBDOOM-3-BFG](https://github.com/RobertBeckebans/RBDOOM-3-BFG)
1.6.0 (upstream commit `ea29c00`), which is based on id Software's Doom 3 BFG Edition source
code. See [CREDITS.md](../CREDITS.md) for who wrote it and [COPYING.txt](../COPYING.txt) for the
license. Every file is byte-for-byte upstream's, except for the OpenRAGE changes listed below.

## Building

Building works the same way as RBDOOM-3-BFG. The full instructions for Windows, Linux and macOS
are in [engine/README.md](../engine/README.md#compile_windows). On Linux you need CMake, a C++17
compiler, SDL2, OpenAL Soft, the Vulkan loader and headers, Intel ISPC, and DXC (the shader
compiler from the Vulkan SDK). Then:

```bash
git submodule update --init --recursive
```

```bash
cmake -S engine/neo -B build/engine -G Ninja -DCMAKE_BUILD_TYPE=Release -DFFMPEG=OFF -DBINKDEC=ON
```

```bash
cmake --build build/engine
```

The executable is `OpenRAGE` (`OpenRAGE.exe` on Windows, `OpenRAGE.app` on macOS). The build
writes the compiled shaders to `engine/base/renderprogs2/`.

The repository's `.gitignore` skips `base/` folders and file types such as `.dll` and `.lib` to keep
RAGE's game files out. Files under `engine/` that need them were added with `git add -f`, and new
ones need it too.

## What OpenRAGE changed

- **Name.** The window title, console banner (`OpenRAGE 0.1.0 (RBDOOM 3 BFG 1.6.0)`), executable,
  macOS bundle and Windows version info all say OpenRAGE. id Software's copyright stays in the
  Windows version info.
- **Saves and config.** Saves and config live in their own OpenRAGE folders, so they never mix with
  Doom 3 BFG or RBDOOM-3-BFG. That's `~/.local/share/openrage` on Linux,
  `~/Library/Application Support/OpenRAGE` on macOS and `Saved Games\OpenRAGE` on Windows. The
  config file is `OpenRAGEConfig.cfg`.
- **No Doom artwork.** The Doom 3 loading screen and the DOOM 3 BFG app icons aren't included. The
  Doom 3 BFG legal splash screens and intro video aren't shown at startup. A black screen shows
  while the engine starts instead.
- **Classic Doom 1 and 2 are off by default.** `-DDOOM_CLASSIC=ON` turns them back on.
- **A missing default font isn't fatal.** Doom 3 BFG's default font used to be required to start.
- **Megatexture viewer.** The `megatextureViewer` console command opens a window that shows RAGE's
  megatextures from your own install, decoded with the `ragejxr` library from
  [tools/rage-jxr](../tools/rage-jxr/README.md). See [Megatexture viewer](#megatexture-viewer).

## Megatexture viewer

The first part of RAGE the engine can show. Start the engine with the viewer open on a
megatexture:

```bash
./OpenRAGE +set fs_basepath <path to engine> +megatextureViewer "<RAGE folder>/virtualtextures/wasteland1.pages"
```

Or run `megatextureViewer` in the console, with or without a file.

- The left side shows the whole megatexture at low detail. Click a spot to look at it.
- **Level** picks the level to decode (0 is the most detailed), and **View** picks the size of the
  region: 960, 1920 or 3840 texels square. Pages the file leaves out are filled from the nearest
  coarser page, the same way `rage-jxr level` does it.
- **Actual size** shows the region texel for texel. Otherwise it's scaled to fit the window.
- It shows the diffuse layer, for map megatextures and `_vmtr.pages` alike.
- Decoding runs on a worker thread, so the window stays responsive. Decoded pages stay in memory;
  nothing is written to disk.
- The last file opened is kept in the `mtv_lastFile` setting.

The code is in [engine/neo/tools/imgui/megatexture/](../engine/neo/tools/imgui/megatexture/).

## Artwork

OpenRAGE's artwork has to be original, so nothing from RAGE or Doom can be used. The build picks
these files up automatically once they exist:

| Asset | Format | Path |
| --- | --- | --- |
| Windows app icon | `.ico` with 16, 24, 32, 48, 64, 128 and 256 px images | `engine/neo/sys/win32/rc/res/openrage.ico` |
| macOS app icon | `.icns` | `engine/neo/sys/posix/res/OpenRAGE.icns` |
| Fallback loading screen | 1920×1080 `.jpg` | `engine/base/guis/assets/loadscreens/default.jpg` |

Until they exist, the executable uses the system's default icon.

## Step 2: starting without Doom 3 BFG's files

The goal is for the engine to start as OpenRAGE using only files from this repository, with no Doom
3 BFG install. This table tracks what it still needs from Doom 3 BFG at startup:

| Needed at startup | Came from | Status |
| --- | --- | --- |
| `default.cfg` | `engine/base/` | Done: in this repository. |
| Shaders | Built from `engine/neo/shaders/` | Done: the build makes them. |
| Legal splash screens and intro video | Doom 3 BFG | Done: no longer shown. |
| Default font (`newfonts/Arial_Narrow`) | Doom 3 BFG | Done: no longer fatal. Text in that font doesn't draw; the console and ImGui use their own fonts. |
| Language strings (`strings/*.lang`) | Doom 3 BFG | Not fatal: text shows string IDs instead. |
| Main menu, HUD and loading screen (Flash `.bswf` UI) | Doom 3 BFG | To do: an OpenRAGE main menu built with Dear ImGui, which the engine already includes. |
| Fallback lighting probe (`engine/neo/renderer/Image_env_UAC_lobby_*.h`) | Baked from Doom 3's UAC lobby | To do: replace with a neutral probe and check the lighting on screen. |
| Game definitions, scripts and sounds for maps | Doom 3 BFG | Later: RAGE's own data, as the format research turns into loaders. |

Each change needs a check on a machine with the Vulkan SDK: start `OpenRAGE` with
`+set fs_basepath <path to engine>` and no Doom 3 BFG install, then note where startup stops.

## Loading RAGE geometry

The engine reads RAGE's archives straight from your own install and can load a RAGE map's
world geometry. Textures come later (they're megatextures), so surfaces don't have their real
look yet.

1. Point the engine at your RAGE install (the folder that holds `base/`), on the command line or
   in the console. It's saved in your config.

   ```bash
   ./OpenRAGE +set fs_ragePath "/path/to/steamapps/common/RAGE"
   ```

2. In the console, `rageMaps` lists the maps and `rageList <filter>` lists archive entries.
3. Load a map with `map <name>`, for example `map game/tracks/dusty8`, then use `noclip` to fly
   around. There's no collision with RAGE's geometry yet.

### What happens

| Step | Where |
| --- | --- |
| `base/gameresources_sp_01.patch` and `base/gameresources.resources` are mounted, patch first, after every normal search path, so loose files and mods override them | `engine/neo/framework/FileSystem.cpp` |
| An entry is unpacked into memory when it's opened (raw deflate without a final block) | `engine/neo/rage/RageArchive.cpp` |
| `maps/<map>/_combo/<map>.proc` (`mapProcFile005`) gives the BSP nodes and the number of areas | `engine/neo/renderer/RenderWorld_load.cpp` |
| Each area's `_area<N>.bmodel` becomes that area's model: all six vertex formats, strips and lists, material ranges | `engine/neo/rage/RageBModel.cpp`, `engine/neo/renderer/Model_bmodel.cpp` |
| The geometry is uploaded once, at level load, to the engine's static GPU vertex and index buffers. The console prints how much of them the map uses | `idRenderModelManagerLocal::EndLevelLoad` |
| The game gets a stand-in `.map`: a worldspawn with a floor slab under the map, and a player start at the first `idPlayerStart` in `<map>.entities` | `engine/neo/d3xp/RageMap.cpp` |

Any `.bmodel` also loads as an ordinary model, so other code can use RAGE models by name.

Nothing converted from the game is written to disk: RAGE models skip the engine's binary model
cache, and RAGE maps don't get a `.bproc` file.

### Not done yet

- **Textures.** Surfaces use RAGE's material names, which don't exist as engine materials yet, so
  they draw with the default texture. `r_rageMaterial <material>` draws everything with one
  material instead.
- **The rest of the map:** `megamodel_*.bmodel` (movers and doors), `sky.bmodel`, the occlusion
  model, every entity except the player start, and collision (`.bcm`).
- **Very large maps.** Doom 3 limits coordinates to ±131,072 units. wasteland1 (up to 152,232)
  and wasteland2 (up to 196,816) go past that.
- **Static GPU buffer size.** About 124 MB each for vertexes and indexes. The biggest maps may
  not fit; watch the numbers the console prints when a map loads.
- **Texture coordinate precision.** The engine stores texture coordinates as half floats, which is
  too coarse for positions in a whole map's megatexture. Megatexture drawing will need full
  precision.

### Testing

`rage_selftest` checks the archive, `.bmodel` and `.entities` readers against small files it
builds itself from the layouts in [research/formats/](research/formats/). It needs no game data
and runs with `ctest` from the build folder. The readers haven't been run against real RAGE files
in this repository's test setup yet, so the first run on a real install is the real check: if a
map doesn't load, the console says which file and why.

## Updating from upstream RBDOOM-3-BFG

`engine/` keeps upstream's bytes and line endings (the root `.gitattributes` marks it `-text`), so
upstream patches apply directly. For example, with an RBDOOM-3-BFG clone next to this repository:

```bash
git -C ../RBDOOM-3-BFG diff ea29c00 <new-commit> > upstream.patch
```

```bash
git apply -3 --directory=engine upstream.patch
```

Then update the commit in this file and in [CREDITS.md](../CREDITS.md), along with the contributor
list. Changes to the Doom artwork OpenRAGE left out will fail to apply; skip them.
