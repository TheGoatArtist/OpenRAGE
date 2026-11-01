# rage-jxr

Decodes RAGE's megatexture **map pages**. Each page holds four JPEG XR streams (diffuse, normal,
specular, power) written by id Software's own encoder, which differs from standard JPEG XR in
four places of its adaptive entropy coding. The format and those differences are described in
[docs/research/formats/megatexture-pages.md](../../docs/research/formats/megatexture-pages.md).

It reads only the files you give it and writes only the images you ask for. Decoded pages come
from your own copy of the game: keep them out of the repository.

## Build

C++20 and CMake 3.16 or newer; no other dependencies.

```bash
cmake -S . -B build
cmake --build build --config Release
```

The program is `build/rage-jxr` on Linux and `build/Release/rage-jxr.exe` with Visual Studio.

## Use

Decode one page to PNG. `<level>` is the level as stored in the file (0 = most detailed):

```bash
rage-jxr decode "/path/to/RAGE/virtualtextures/wasteland1.pages" 0 0 5 -o page.png
```

- `--layers` also writes the normal, specular and power layers. Their value scales aren't
  confirmed yet, so treat those images as previews.
- `--payload` crops to the 120×120 payload, dropping the 4-texel border pages share with their
  neighbours.

Check that every stream of every page decodes, ending exactly where its stream ends:

```bash
rage-jxr check "/path/to/RAGE/virtualtextures/scientist.pages"
```

Decode a standard JPEG XR file with jxrlib's own colour output, to compare the decoder with the
reference implementation (`--rage` applies RAGE's rules, `--dump` prints coefficients):

```bash
rage-jxr jxr image.jxr image.ppm
```

Supported: spatial-mode, single-tile, 4:4:4 / greyscale / N-component images, which covers every
map page. Model pages (`_vmtr.pages`) use a different, DCT-based codec and aren't handled here.

## License

GPL-3.0-or-later, like the rest of OpenRAGE. The decoder follows the structure of Microsoft's
jxrlib, so those parts also carry jxrlib's BSD-2-Clause notice: see
[LICENSE-jxrlib.txt](LICENSE-jxrlib.txt).
