#!/usr/bin/env python3
"""List the page tree of a RAGE megatexture (.pages) file.

Reads only the 40-byte header in front of each page: its position, its level
and where its child pages are. It never decodes, extracts or writes any image
data. The --check option confirms the tree covers the whole file and compares
the companion .pageLines file with what the tree says it should contain.

The format is described in docs/research/formats/megatexture-pages.md.

Works on Windows and Linux (including Steam/Proton installs).

Usage:
    python tools/pages.py "E:/SteamLibrary/steamapps/common/RAGE/virtualtextures/scientist.pages"
    python tools/pages.py PAGES_FILE --check --list
    python tools/pages.py "E:/SteamLibrary/steamapps/common/RAGE" --check
"""

import argparse
import heapq
import struct
import sys
from collections import Counter
from pathlib import Path

FILE_MAGIC = 0x77339904
PAGE_MAGIC = 0xCABFED04
# magic, width in pages, unknown, levels, possible pages, unknown (always 2), file size
FILE_HEADER = struct.Struct("<6IQ")
# magic, version, 4 child offsets, 4 child sizes (16 bits each), x | y << 16, level
PAGE_HEADER = struct.Struct("<10I")
ROOT_OFFSET = 0x20  # a page header whose only child is the top page
FIRST_PAGE = 0x48
UNIT = 2  # child offsets and sizes count 2-byte units
BLOCK_BITS = 16  # .pageLines counts 64 KB blocks
CROSSES_BLOCK = 0x8000
ABSENT = 0xFFFF


def mb(size):
    return f"{size / (1024 * 1024):,.1f}"


def read_header(f, path):
    f.seek(0)
    magic, width, unknown, levels, possible, _, size = FILE_HEADER.unpack(f.read(FILE_HEADER.size))
    if magic != FILE_MAGIC:
        sys.exit(f"{path}: not a .pages file (magic {magic:08x})")
    return {"width": width, "unknown": unknown, "levels": levels, "possible": possible, "size": size}


def children(words):
    """(offset, size, quadrant) for each child named in a page header."""
    sizes = (words[6] & 0xFFFF, words[6] >> 16, words[7] & 0xFFFF, words[7] >> 16)
    return [(words[2 + q] * UNIT, sizes[q] * UNIT, q) for q in range(4) if sizes[q]]


def read_pages(f, header, problems):
    """Walk the tree from the root. Returns {offset: (x, y, level, size)}.

    Pages are visited in file order so the reads move forward through the
    file, which matters on hard drives."""
    f.seek(ROOT_OFFSET)
    root = PAGE_HEADER.unpack(f.read(PAGE_HEADER.size))
    pending = [(offset, size, None) for offset, size, _ in children(root)]
    pages = {}
    while pending:
        offset, size, expected = heapq.heappop(pending)
        if offset + PAGE_HEADER.size > header["size"]:
            problems.append(f"page at byte {offset} is past the end of the file")
            continue
        f.seek(offset)
        words = PAGE_HEADER.unpack(f.read(PAGE_HEADER.size))
        if words[0] != PAGE_MAGIC:
            problems.append(f"no page header at byte {offset}")
            continue
        x, y, level = words[8] & 0xFFFF, words[8] >> 16, words[9]
        if expected and (x, y, level) != expected:
            problems.append(f"page at byte {offset} is {(x, y, level)}, expected {expected}")
        pages[offset] = (x, y, level, size)
        for child, child_size, q in children(words):
            place = (x * 2 + (q & 1), y * 2 + (q >> 1), level - 1)
            heapq.heappush(pending, (child, child_size, place))
    return pages


def check_tree(pages, header, problems):
    """The pages should follow each other with no gaps up to the end of the file."""
    end = FIRST_PAGE
    for offset in sorted(pages):
        if offset != end:
            problems.append(f"gap or overlap at byte {end}")
            return
        end = offset + pages[offset][3]
    if pages and end != header["size"]:
        problems.append(f"pages end at byte {end}, file ends at {header['size']}")


def expected_page_lines(pages, header):
    """What .pageLines should hold: one value per possible page, top level first."""
    width, levels = header["width"], header["levels"]
    values = [ABSENT] * header["possible"]
    start, base = 0, {}
    for level in range(levels - 1, -1, -1):
        base[level] = start
        start += (width >> level) ** 2
    for offset, (x, y, level, size) in pages.items():
        value = offset >> BLOCK_BITS
        if (offset + size - 1) >> BLOCK_BITS != value:
            value |= CROSSES_BLOCK
        values[base[level] + y * (width >> level) + x] = value
    return values


def check_page_lines(path, pages, header):
    page_lines = path.with_suffix(".pageLines")
    if not page_lines.is_file():
        return "none"
    data = page_lines.read_bytes()
    expected = expected_page_lines(pages, header)
    if len(data) != len(expected) * 2:
        return "wrong size"
    actual = struct.unpack(f"<{len(expected)}H", data)
    differ = sum(1 for a, b in zip(actual, expected) if a != b)
    return "matches" if differ == 0 else f"stale ({differ:,} of {len(pages):,} differ)"


def analyze(path, check):
    with open(path, "rb") as f:
        header = read_header(f, path)
        problems = []
        pages = read_pages(f, header, problems)
    result = {"path": path, "header": header, "pages": pages, "problems": problems}
    if check:
        check_tree(pages, header, problems)
        result["page_lines"] = check_page_lines(path, pages, header)
    return result


def print_file(result, check, list_pages):
    header, pages = result["header"], result["pages"]
    width, levels = header["width"], header["levels"]
    print(f"# {result['path'].name}")
    print()
    print(f"{width} x {width} pages at the most detailed level, {levels} levels. "
          f"{len(pages):,} of {header['possible']:,} possible pages stored. {mb(header['size'])} MB.")
    print(f"Header field 0x08 (meaning unknown): {header['unknown']}.")
    print()
    print("## Levels")
    print()
    print("Level 0 is the most detailed.")
    print()
    print("| Level | Grid | Pages | Of grid | MB |")
    print("| ---: | ---: | ---: | ---: | ---: |")
    count, size = Counter(), Counter()
    for _, _, level, page_size in pages.values():
        count[level] += 1
        size[level] += page_size
    for level in range(levels - 1, -1, -1):
        grid = width >> level
        share = count[level] / (grid * grid)
        print(f"| {level} | {grid} x {grid} | {count[level]:,} | {share:.1%} | {mb(size[level])} |")

    if check:
        print()
        print("## Check")
        print()
        print(f"- Structure: {'OK' if not result['problems'] else 'problems found'}")
        for problem in result["problems"][:10]:
            print(f"  - {problem}")
        print(f"- .pageLines: {result['page_lines']}")

    if list_pages:
        print()
        print("## Pages")
        print()
        print("| Level | X | Y | Offset | Size |")
        print("| ---: | ---: | ---: | ---: | ---: |")
        for offset in sorted(pages):
            x, y, level, page_size = pages[offset]
            print(f"| {level} | {x} | {y} | {offset} | {page_size} |")


def print_folder(folder, files, check):
    print(f"# Megatextures in {folder}")
    print()
    columns = "| File | Levels | Grid | Pages | Most detailed level stored | MB |"
    rule = "| --- | ---: | ---: | ---: | ---: | ---: |"
    if check:
        columns += " Structure | .pageLines |"
        rule += " --- | --- |"
    print(columns)
    print(rule)
    for path in files:
        result = analyze(path, check)
        header, pages = result["header"], result["pages"]
        finest = min((p[2] for p in pages.values()), default="-")
        row = (f"| `{path.relative_to(folder).as_posix()}` | {header['levels']} | "
               f"{header['width']} | {len(pages):,} | {finest} | {mb(header['size'])} |")
        if check:
            row += f" {'OK' if not result['problems'] else 'problems'} | {result['page_lines']} |"
        print(row, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("target", type=Path, help=".pages file, or a folder to search for them")
    parser.add_argument("--check", action="store_true",
                        help="confirm the page tree covers the file and compare .pageLines")
    parser.add_argument("--list", action="store_true", help="print every page (single file only)")
    args = parser.parse_args()

    target = args.target.expanduser().resolve()
    if target.is_dir():
        files = sorted(target.rglob("*.pages"))
        if not files:
            sys.exit(f"No .pages files under {target}")
        print_folder(target, files, args.check)
    elif target.is_file():
        print_file(analyze(target, args.check), args.check, args.list)
    else:
        sys.exit(f"Not found: {target}")


if __name__ == "__main__":
    main()
