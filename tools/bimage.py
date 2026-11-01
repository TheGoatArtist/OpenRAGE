#!/usr/bin/env python3
"""List the .bimage textures inside a RAGE .resources or .patch archive.

Reads only each texture's header: its size, format, mip levels and faces. It
never decodes, extracts or writes any image data. The --check option unpacks
each texture in memory, confirms its mip records fill the file exactly and
that each level is the size its format predicts, then throws it away.

The format is described in docs/research/formats/bimage.md.

Works on Windows and Linux (including Steam/Proton installs).

Usage:
    python tools/bimage.py "E:/SteamLibrary/steamapps/common/RAGE/base/gameresources.resources"
    python tools/bimage.py ARCHIVE --list --filter fonts
    python tools/bimage.py DLC_ARCHIVE --check --also BASE_ARCHIVE
"""

import argparse
import struct
import sys
import zlib
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

from resources import mb, read_entries

MAGIC = b"\x07MIB"  # "BIM" plus version 7, stored big-endian
NO_DATE = 0xFFFFFFFF  # engine-generated images such as constantcolor_*
# source file time, magic, texture type, width, height, unknown (always 0), mip levels
HEADER = struct.Struct(">I4s5I")
HEADER_SIZE = 42
UNKNOWN_BYTES = (0x1C, 0x24, 0x28)  # small values, meaning not known yet
FORMAT_BYTE = 0x20
# level, face, width, height, data size; the data follows each record
IMAGE = struct.Struct(">5I")
CUBE = 2
CUBE_FACES = 6
BLOCK = 4  # BC1/BC3 compress 4x4 pixel blocks

# format code: (name, bits per pixel, block-compressed)
FORMATS = {
    3: ("32-bit uncompressed", 32, False),
    5: ("8-bit (fonts)", 8, False),
    8: ("8-bit", 8, False),
    10: ("BC1 / DXT1", 4, True),
    11: ("BC3 / DXT5", 8, True),
}


def format_name(code):
    return FORMATS[code][0] if code in FORMATS else f"unknown ({code})"


def level_size(code, width, height):
    """Bytes one mip level should take, or None for an unknown format."""
    if code not in FORMATS:
        return None
    _, bits, blocks = FORMATS[code]
    if blocks:
        width = max(1, (width + BLOCK - 1) // BLOCK) * BLOCK
        height = max(1, (height + BLOCK - 1) // BLOCK) * BLOCK
    return width * height * bits // 8


def read_start(files, entry, length):
    """Unpack the first `length` bytes of an entry from whichever archive holds
    it (DLC entries can point into the base archive). None if none does."""
    for f in files:
        f.seek(entry["offset"])
        raw = f.read(entry["stored"])
        if len(raw) != entry["stored"]:
            continue
        if entry["stored"] == entry["size"]:
            data = raw[:length]
        else:
            try:
                data = zlib.decompressobj(-15).decompress(raw, length)
            except zlib.error:
                continue
        if data[4:8] == MAGIC:
            return data
    return None


def parse_header(data):
    when, magic, kind, width, height, _, levels = HEADER.unpack_from(data)
    return {
        "date": None if when == NO_DATE else datetime.fromtimestamp(when, timezone.utc),
        "cube": kind == CUBE,
        "width": width,
        "height": height,
        "levels": levels,
        "format": data[FORMAT_BYTE],
        "unknown": tuple(data[off] for off in UNKNOWN_BYTES),
    }


def check_image(data, header):
    """Return None if the mip records fill the file exactly, else a problem."""
    faces = CUBE_FACES if header["cube"] else 1
    pos = HEADER_SIZE
    for i in range(header["levels"] * faces):
        if pos + IMAGE.size > len(data):
            return f"record {i} runs past the end"
        level, face, width, height, size = IMAGE.unpack_from(data, pos)
        if level != i // faces or face != i % faces:
            return f"record {i} is level {level} face {face}"
        expected = level_size(header["format"], width, height)
        if expected is not None and size != expected:
            return f"level {level} is {size} bytes, format predicts {expected}"
        pos += IMAGE.size + size
    if pos != len(data):
        return f"records end at {pos} of {len(data)} bytes"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("archive", type=Path, help=".resources or .patch file")
    parser.add_argument("--list", action="store_true", help="print every texture")
    parser.add_argument("--filter", help="only textures whose name contains this text")
    parser.add_argument("--check", action="store_true",
                        help="confirm each texture's mip records fill it exactly")
    parser.add_argument("--also", type=Path, action="append", default=[],
                        help="other archive(s) that may hold the data (e.g. the base game's)")
    args = parser.parse_args()

    path = args.archive.expanduser().resolve()
    if not path.is_file():
        sys.exit(f"Not a file: {path}")

    _, entries = read_entries(path)
    entries = [e for e in entries if e["name"].endswith(".bimage") and e["stored"]]
    if args.filter:
        needle = args.filter.lower()
        entries = [e for e in entries if needle in e["name"].lower()]

    files = [open(p, "rb") for p in [path] + [p.expanduser().resolve() for p in args.also]]
    images, missing, problems = [], [], []
    try:
        for e in entries:
            data = read_start(files, e, e["size"] if args.check else HEADER_SIZE)
            if data is None or len(data) < HEADER_SIZE:
                missing.append(e)
                continue
            header = parse_header(data)
            images.append((e, header))
            if args.check:
                problem = check_image(data, header)
                if problem:
                    problems.append((e, problem))
    finally:
        for f in files:
            f.close()

    print(f"# {path.name}")
    print()
    print(f"{len(images)} textures, {mb(sum(e['size'] for e, _ in images))} MB unpacked.")
    if missing:
        print(f"{len(missing)} more whose data isn't in this archive (try --also).")
    print()
    print("## Formats")
    print()
    print("| Format | Code | 2D | Cube | Unpacked MB |")
    print("| --- | ---: | ---: | ---: | ---: |")
    by_format = {}
    for e, h in images:
        by_format.setdefault(h["format"], []).append((e, h))
    for code, group in sorted(by_format.items(), key=lambda kv: len(kv[1]), reverse=True):
        cubes = sum(1 for _, h in group if h["cube"])
        size = sum(e["size"] for e, _ in group)
        print(f"| {format_name(code)} | {code} | {len(group) - cubes} | {cubes} | {mb(size)} |")

    print()
    print("## Entry types")
    print()
    for kind, count in Counter(e["type"] for e, _ in images).most_common():
        print(f"- `{kind}`: {count}")

    if args.check:
        print()
        print("## Check")
        print()
        print(f"- Mip records fill the file exactly: {len(images) - len(problems)} of {len(images)}")
        for e, problem in problems:
            print(f"- `{e['name']}`: {problem}")

    if args.list:
        print()
        print("## Textures")
        print()
        print("| Name | Size | Format | Levels | Cube | Source date | Unknown 0x1C/0x24/0x28 |")
        print("| --- | --- | --- | ---: | --- | --- | --- |")
        for e, h in images:
            date = h["date"].strftime("%Y-%m-%d") if h["date"] else "generated"
            unknown = "/".join(str(v) for v in h["unknown"])
            print(f"| `{e['name']}` | {h['width']}x{h['height']} | {format_name(h['format'])} | "
                  f"{h['levels']} | {'yes' if h['cube'] else ''} | {date} | {unknown} |")


if __name__ == "__main__":
    main()
