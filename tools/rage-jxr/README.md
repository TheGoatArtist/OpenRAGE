# rage-jxr

Decodes RAGE's megatexture pages, in both of their codecs:

- **Map pages** hold four JPEG XR streams (diffuse, normal, specular, power) written by id
  Software's own encoder, which differs from standard JPEG XR in four places of its adaptive
  entropy coding.
- **Model pages** (`_vmtr.pages`) hold DCT streams coded like baseline JPEG, plus a 1-bit mask.

The formats are described in
[docs/research/formats/megatexture-pages.md](../../docs/research/formats/megatexture-pages.md).
The program is named after the JPEG XR part, which came first.

It reads only the files you give it and writes only the images you ask for. Decoded pages come
from your own copy of the game: keep them out of the repository.

## Build

C++20 and CMake 3.16 or newer; no other dependencies.

```bash
cmake -S . -B build
cmake --build build --config Release
```

The program is `build/rage-jxr` on Linux and `build/Release/rage-jxr.exe` with Visual Studio.

The decoder itself is the `ragejxr` library (everything in `src/` except `main.cpp` and
`image_out.*`), which OpenRAGE's engine links with `add_subdirectory` on this folder. Its public
headers are `megatexture.h` and `pages.h`: `decode_diffuse` decodes a page's diffuse layer and
`decode_model_layers` a model page's normal, specular, power and mask, in the layout of the textures
the game exports.

## Use

Decode one page to PNG. `<level>` is the level as stored in the file (0 = most detailed):

```bash
rage-jxr decode "/path/to/RAGE/virtualtextures/wasteland1.pages" 0 0 5 -o page.png
```

- `--layers` also writes the normal, specular and power layers, and for model pages the mask.
  For map pages their value scales aren't confirmed yet, and neither is the colour conversion of
  colour specular on model pages, so treat those images as previews.
- `--payload` crops to the 120×120 payload, dropping the 4-texel border pages share with their
  neighbours.

Decode the diffuse layer of a whole level into one image, 120 texels per page. Level 7 of
`wasteland1` is 960×960; each level down doubles that. Pages the file leaves out (often large
areas at detailed levels) are filled from the nearest coarser page:

```bash
rage-jxr level "/path/to/RAGE/virtualtextures/wasteland1.pages" 7 -o wasteland1_7.png
```

`_vmtr.pages` has 10 levels, so its level 6 is 960×960.

Add `<x> <y> <width> <height>` (in texels at that level) to decode only a rectangle, such as one
material's texture (see [docs/research/formats/bmodel.md](../../docs/research/formats/bmodel.md)
for how materials map into `_vmtr`):

```bash
rage-jxr level "/path/to/RAGE/virtualtextures/_vmtr.pages" 1 0 12960 240 240 -o keycard.png
```

Check that every stream of every page decodes, ending exactly where its stream ends:

```bash
rage-jxr check "/path/to/RAGE/virtualtextures/scientist.pages"
```

Decode a standard JPEG XR file with jxrlib's own colour output, to compare the decoder with the
reference implementation (`--rage` applies RAGE's rules, `--dump` prints coefficients):

```bash
rage-jxr jxr image.jxr image.ppm
```

The JPEG XR side supports spatial-mode, single-tile, 4:4:4 / greyscale / N-component images,
which covers every map page.

## License

GPL-3.0-or-later, like the rest of OpenRAGE. The decoder follows the structure of Microsoft's
jxrlib, so those parts also carry jxrlib's BSD-2-Clause notice: see
[LICENSE-jxrlib.txt](LICENSE-jxrlib.txt).
