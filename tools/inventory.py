#!/usr/bin/env python3
"""Summarize a RAGE install: folders, file types, counts and sizes.

Reads only file names and sizes (plus the version numbers stored inside the
.exe files). Nothing is copied out of the game folder, so the output is safe
to paste into research notes.

Works on Windows and Linux (including Steam/Proton installs).

Usage:
    python tools/inventory.py "E:/SteamLibrary/steamapps/common/RAGE"
    python3 tools/inventory.py ~/.local/share/Steam/steamapps/common/RAGE
"""

import argparse
import struct
import sys
from collections import defaultdict
from pathlib import Path

# Signature at the start of the VS_FIXEDFILEINFO block in a Windows .exe/.dll
FIXED_FILE_INFO_SIGNATURE = struct.pack("<I", 0xFEEF04BD)


def mb(size):
    return f"{size / (1024 * 1024):,.1f}"


def exe_versions(path):
    """Return (file_version, product_version) from an .exe's version resource, or None."""
    data = path.read_bytes()
    offset = data.find(FIXED_FILE_INFO_SIGNATURE)
    if offset < 0:
        return None
    # Layout: signature, struct version, file MS, file LS, product MS, product LS
    _, _, file_ms, file_ls, prod_ms, prod_ls = struct.unpack_from("<6I", data, offset)

    def fmt(ms, ls):
        return f"{ms >> 16}.{ms & 0xFFFF}.{ls >> 16}.{ls & 0xFFFF}"

    return fmt(file_ms, file_ls), fmt(prod_ms, prod_ls)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("game_path", type=Path, help="RAGE install folder")
    args = parser.parse_args()

    root = args.game_path.expanduser().resolve()
    if not root.is_dir():
        sys.exit(f"Not a folder: {root}")

    files = [p for p in root.rglob("*") if p.is_file()]
    sizes = {p: p.stat().st_size for p in files}

    by_folder = defaultdict(list)
    by_ext = defaultdict(list)
    for p in files:
        by_folder[p.parent.relative_to(root).as_posix()].append(p)
        by_ext[p.suffix or "(none)"].append(p)

    def total(paths):
        return sum(sizes[p] for p in paths)

    print(f"# Inventory of {root.name}")
    print()
    print(f"Total: {len(files)} files, {mb(total(files))} MB")
    print()
    print("## Folders")
    print()
    print("| Folder | Files | MB |")
    print("| --- | ---: | ---: |")
    for folder in sorted(by_folder, key=str.lower):
        paths = by_folder[folder]
        name = "/" if folder == "." else f"/{folder}"
        print(f"| `{name}` | {len(paths)} | {mb(total(paths))} |")
    print()
    print("## File types")
    print()
    print("| Extension | Files | MB |")
    print("| --- | ---: | ---: |")
    for ext, paths in sorted(by_ext.items(), key=lambda kv: total(kv[1]), reverse=True):
        print(f"| `{ext}` | {len(paths)} | {mb(total(paths))} |")
    print()
    print("## Executables")
    print()
    for exe in sorted(root.glob("*.exe"), key=lambda p: p.name.lower()):
        versions = exe_versions(exe)
        if versions:
            print(f"- `{exe.name}`: file version {versions[0]}, product version {versions[1]}")
        else:
            print(f"- `{exe.name}`: no version resource found")


if __name__ == "__main__":
    main()
