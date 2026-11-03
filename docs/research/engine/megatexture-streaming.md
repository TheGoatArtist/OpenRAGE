# Megatexture streaming: what the engine has and needs

- Game version / build: nothing new was read from the game for this note. What it says about RAGE
  comes from [../formats/megatexture-pages.md](../formats/megatexture-pages.md) (Steam build
  `1509967`).
- Engine examined: [engine/](../../../engine/) at RBDOOM-3-BFG `ea29c00` with OpenRAGE's changes,
  and its NVRHI submodule at `dafbd40`.
- Tools used: reading the source.
- Confidence: **verified** in the source for "What the engine has" and "What's missing";
  **inferred** where marked; the pipeline at the end is a **proposal** for discussion.
- Date: 2026-11-03

The roadmap makes megatexture streaming its own subsystem (see
[How the renderer fits in](../../roadmap.md#how-the-renderer-fits-in)). This note checks which parts
of it the engine already has and which still have to be written. id's own description of how id
Tech 5 streams pages, in J.M.P. van Waveren's papers, hasn't been read for this note yet: see
[Sources still to read](#sources-still-to-read).

## What we know

### What RAGE does at run time

From the format research in [megatexture-pages.md](../formats/megatexture-pages.md):

- **One virtual space.** The game maps every `.pages` file into one space 2,048 pages wide with
  12 levels. `vt_info` lists the page sources: `_vmtr` is 1, `_vmtr_dlc` is 2, and the current
  map comes after them.
- **Pages** are 128×128 texels: a 120×120 payload and a 4-texel border copied from the
  neighbours. The power layer is 32×32 with a 1-texel border.
- **Disk.** Files store pages coarsest level first. `.pageLines` gives the 64 KB block where each
  page starts and flags pages that run into the next block, so fetching a page takes one or two
  aligned reads and no tree walk.
- **Decoding is split.** The executable ships CUDA kernels for the inverse transforms and for
  re-encoding to YCoCg DXT5, normal-map DXT and specular. They take 16-bit coefficients, so the
  entropy decoding runs on the CPU.
- **Physical caches.** The engine log mentions 4096 × 4096 physical caches, which would hold
  32 × 32 = 1,024 pages of 128 texels (arithmetic, not checked). `vt_export` writes three physical
  textures: the diffuse as (Co, Cg, 0, Y), normal and power as (power, normal Y, 0, normal X), and
  specular and mask as (specular RGB, mask).

### What the engine has

| Needed for streaming | In `engine/` today | Where (under `engine/neo/`) |
| --- | --- | --- |
| Encoding pages into RAGE's cache formats | id's real-time DXT encoders from Doom 3 BFG, with SSE2 paths: DXT1, DXT5, YCoCg-DXT5, normal maps into DXT1 or DXT5, DXN1 (BC4) and CTX1, plus RBDOOM-3-BFG's BC6H | `renderer/DXT/` |
| RGB ↔ YCoCg | `idColorSpace::ConvertRGBToCoCg_Y` and `ConvertCoCg_YToRGB` | `renderer/Color/ColorSpace.cpp` |
| Worker threads | `idParallelJobList`: job lists with priorities and sync points. Its "max threads" mode is documented as helping "if there is IO to overlap" | `idlib/ParallelJobList.h` |
| Separate copy and compute queues | NVRHI supports both and both device managers can create them, but `enableCopyQueue` and `enableComputeQueue` default to false and nothing turns them on, so all uploads and compute go through the graphics queue | `sys/DeviceManager.h`, `sys/DeviceManager_VK.cpp`, `sys/DeviceManager_DX12.cpp` |
| Writing a page into part of a large texture | NVRHI staging textures (`createStagingTexture`, `mapStagingTexture`) and `copyTexture` between texture slices. `writeTexture` only writes whole mip levels | `extern/nvrhi/include/nvrhi/nvrhi.h` |
| Hardware virtual texturing | NVRHI's tiled (sparse) textures (`TextureDesc::isTiled`, `getTextureTiling`, `updateTextureTileMappings`), implemented for D3D11, D3D12 and Vulkan. Sampler feedback textures exist in the D3D12 back end only. The engine uses neither | `extern/nvrhi/` |

**The cache formats line up.** `ConvertRGBToCoCg_Y` writes (Co + 128, Cg + 128, 0, Y), the same
channel order as RAGE's physical diffuse texture, and that's the input the YCoCg-DXT5 encoder
expects. The DXT5 normal-map encoders take `_Ny_Nx` (Y in green, X in alpha), and RAGE's
normal/power texture has the same layout with power in the spare red channel. **Inferred:** RAGE's
caches are YCoCg-DXT5 and DXT5 normal maps made by this family of encoders (id wrote both), so
OpenRAGE can fill its caches in the game's own formats with code it already has.

**A leftover from id's tools.** `idBinaryImage` (`renderer/BinaryImage.h`), the `.bimage` reader
compared with RAGE's in [bimage.md](../formats/bimage.md), still says it is "used in a
memory-mapped form for imageCPU for offline megatexture generation". Only the comment remains; no
megatexture code is in the engine.

### What's missing

None of these exist in `engine/` yet:

- **Partial texture uploads.** `idImage::SubImageUpload` is an empty stub in the NVRHI back end
  (`renderer/NVRHI/Image_NVRHI.cpp`).
- **Streaming of any kind.** Images load whole, in batches (`idImageManager::LoadDeferredImages`).
- **The page decoder.** It exists as the `ragejxr` library in
  [tools/rage-jxr](../../../tools/rage-jxr/README.md), but `engine/` doesn't link it yet.
- **Virtual texturing itself:** a feedback pass that records which pages a frame needs, a page
  table (indirection texture) and the shader code that looks pages up in it, physical cache
  textures with an allocator, and a page reader that uses `.pageLines`.

### A possible pipeline (proposal)

This is how the parts above could fit together, following the roadmap's rules: streaming runs on
worker threads and a separate transfer queue, and GPU compute replaces the CUDA transcoder. Sizes
and budgets are left open until id's papers have been read.

1. **Feedback.** A low-resolution pass writes, for each pixel, the virtual page and level its
   material samples.
2. **Requests.** The CPU reads the feedback back a frame or two later and requests missing pages
   coarsest first, so a blurrier ancestor always shows while the sharp page loads. The files store
   pages in the same order.
3. **Reads.** Job-list workers fetch each page's 64 KB block or blocks through `.pageLines`.
4. **Decode.** `ragejxr` decodes on CPU jobs to begin with. Its transform stage can move to GPU
   compute later, the same split RAGE uses with CUDA.
5. **Encode.** `idDxtEncoder` turns the decoded layers into YCoCg-DXT5 and DXT5 normal maps. The
   cache could start uncompressed while the rest is built.
6. **Upload.** Staging texture, then a copy on the copy queue into the cache, then the page-table
   update.
7. **Draw.** Material shaders translate virtual texture coordinates through the page table.

## Open questions

- **id's own numbers:** the feedback resolution, the page-table format, cache sizes, per-frame page
  budgets, and how filtering works across page borders. id's papers cover these.
- **Software page table or hardware sparse textures.** NVRHI offers both on Vulkan and D3D12. Which
  works better on today's drivers needs testing.
- **Cache format.** Matching RAGE (YCoCg-DXT5 and DXT5 normal maps) keeps memory use and the look
  the same. BC7 or uncompressed caches could look better and cost more memory.
- **Where decoding runs.** RAGE entropy-decodes on the CPU and runs the transforms on the GPU.
  Whether OpenRAGE needs a GPU stage depends on how fast `ragejxr` is on a CPU: measure pages per
  second per core first (`rage-jxr level` prints its decode time).

## Sources still to read

J.M.P. van Waveren's papers and talks for id Software, from the archive of his site at
[quaddicted.com](https://www.quaddicted.com/webarchive/mrelusive.com_20170131/publications/pubs_bytype.html).
[megatexture-pages.md](../formats/megatexture-pages.md) already uses the 2006 and 2012 papers for
specific facts.

- [Software Virtual Textures](https://www.quaddicted.com/webarchive/mrelusive.com_20170131/publications/papers/Software-Virtual-Textures.pdf) (2012)
- [id Tech 5 Challenges: From Texture Virtualization to Massive Parallelization](https://www.quaddicted.com/webarchive/mrelusive.com_20170131/publications/presentations/2009_siggraph/05-JP_id_Tech_5_Challenges.pdf) (SIGGRAPH 2009)
- [Virtual Textures](https://www.quaddicted.com/webarchive/mrelusive.com_20170131/publications/presentations/2010_gtc/GTC_2010_Virtual_Textures.pdf) (GTC 2010)
- [Virtual Texturing in Software and Hardware](https://www.quaddicted.com/webarchive/mrelusive.com_20170131/publications/presentations/2012_siggraph/Virtual_Texturing_in_Software_and_Hardware_final.pdf) (SIGGRAPH 2012 course)
- [High Quality Software and Hardware Virtual Textures](https://www.quaddicted.com/webarchive/mrelusive.com_20170131/publications/presentations/2013_siggraph/hq_sw_hw_vts_12.pdf) (SIGGRAPH 2013)
- [Real-Time Texture Streaming & Decompression](https://www.quaddicted.com/webarchive/mrelusive.com_20170131/publications/papers/Real-Time-Texture-Streaming-&-Decompression.pdf) (2006)
- [Real-Time DXT Compression](https://www.quaddicted.com/webarchive/mrelusive.com_20170131/publications/papers/Real-Time-Dxt-Compression.pdf) (2006)
