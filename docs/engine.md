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
