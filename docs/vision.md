# Vision

## Why OpenRAGE exists

RAGE is a game its fans love. Its world, atmosphere and technology were ambitious, but parts
of it never got the attention they deserved, especially modding and multiplayer. OpenRAGE
brings the preservation-and-modernization spirit of openQ4 to RAGE. The aim is to **preserve
and improve the game, not remake it**.

## Goals

### Megatexture technology
Preserve and optimize RAGE's original megatexture (virtual texture) system. Investigate why it
pops in and stalls, and improve streaming reliability and performance.

### Modern performance and graphics
Improve performance and stability on modern hardware. Support high frame rates without
breaking physics or gameplay. Explore modern upscalers such as DLSS and FSR.

### Modding
Revitalize modding by building on the original RAGE Tool Kit and giving creators more useful
capabilities.

### Multiplayer
Preserve the original multiplayer modes and experience while exploring additions that could
help the community grow.

### Unified experience
Explore combining useful features from the 32-bit and 64-bit releases into one streamlined
64-bit build, including multiplayer and achievements where feasible. Keep a clear separation
between mods and multiplayer.

## Technical realities

- **No source code is available.** RAGE runs on id Tech 5, which unlike Quake 4's id Tech 4
  was never open-sourced. OpenRAGE can't follow openQ4's approach directly. It will need
  reverse engineering and/or independent reimplementation of the runtime, in the style of
  projects such as OpenMW or OpenRCT2.
- **The causes of megatexture problems are unknown.** They need profiling before anyone tries
  to fix them. Possible factors include streaming, scheduling, decompression, CPU/GPU
  synchronization, disk I/O and resource management.
- **Upscalers aren't a streaming fix.** DLSS and FSR reduce GPU load. They don't fix CPU-side
  streaming stalls.
- **Be honest about pop-in.** The goal is to *reduce* pop-in and improve streaming. Nobody
  should promise to eliminate it before the underlying causes are understood.
- **Game assets stay user-supplied.** See [legal.md](legal.md).

## Possible components (not decided)

- OpenRAGE Engine
- OpenRAGE Toolkit
- OpenRAGE Multiplayer
- OpenRAGE Launcher
