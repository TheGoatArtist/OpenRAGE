# Roadmap (draft)

> These are **possible phases under discussion**. They're not a finalized roadmap, and they
> don't mean implementation has started.

1. **Research.** Document the engine and its data formats: archives, megatexture pages, maps,
   models and scripts.
2. **Proof of concept.** Load and render a real game environment from the user's own install.
3. **Rendering and streaming.** Profile megatexture streaming, find the bottlenecks, and
   improve them.
4. **Gameplay compatibility.** Work toward the original game being playable.
5. **Modding tools.** Expand on the RAGE Tool Kit.
6. **Multiplayer.** Preserve the original modes and explore careful expansion.
7. **Unified application.** Explore a single 64-bit experience.

## Proposed first milestone: a viewer (draft)

A small OpenRAGE program that opens the user's own RAGE install and shows real game data. It's
the first piece of phase 2, and it starts the engine's structure without needing any gameplay.
Each step is small, works on Windows and Linux, and ends with something visible.

1. **Decide the basics.** License (written from scratch, or starting from Doom 3 BFG under
   GPL-3.0), language and build system, and dependencies. Suggested: C++20, CMake, SDL3,
   Vulkan with volk and VMA, and Dear ImGui.
2. **A window.** SDL3 opens a window and Vulkan clears it every frame. This proves the toolchain
   on both platforms. Optionally, GitHub Actions builds it on every push.
3. **Archive browser.** Port the `.resources` reader from [tools/resources.py](../tools/resources.py)
   to C++ and show each archive's entries in a searchable list.
4. **Textures.** Finish documenting the `.bimage` header, then upload BC1/BC3 images straight to
   the GPU and show them when picked in the browser.
5. **Megatexture explorer.** Open a `.pages` file and draw its page tree: which pages exist at
   each level, their sizes, and where `.pageLines` is stale. Uses
   [research/formats/megatexture-pages.md](research/formats/megatexture-pages.md).
6. **Megatexture pixels.** Once the page codec is decoded, show the real terrain. Streaming and
   caching grow from here.

Steps 2–5 don't wait on unsolved research. Step 6 needs the page codec, which can be researched
in parallel.

### How the renderer fits in

These are the design rules proposed for the engine, so the Vulkan renderer stays a separate
piece from the start:

- **Game code never touches Vulkan.** It describes the scene, and the renderer decides how to draw
  it. Only the Vulkan back end calls Vulkan, which also leaves room for other back ends later.
- **Fixed game tick, interpolated rendering.** Game rules and physics run at a steady rate, and
  the renderer blends between ticks, so high frame rates don't change gameplay.
- **Megatexture streaming is its own subsystem.** Feedback, disk reads, decoding and upload run on
  worker threads and a separate transfer queue, so a slow read never stalls a frame. GPU compute
  replaces the original CUDA transcoder and runs on any vendor.
- **Upscaler-ready output.** The renderer produces depth, motion vectors and camera jitter from
  the start, so FSR, DLSS and XeSS can be added later.

## Open questions

- What is the first concrete milestone? A viewer is proposed above.
- How will the code be structured (engine, tools, launcher)?
- Which language and build system? C++ with CMake is the usual choice for projects like this.
- Which license? See [legal.md](legal.md).
