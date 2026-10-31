#!/usr/bin/env python3
"""List the contents of a RAGE .resources or .patch archive.

Reads only the archive's table of contents: names, types, offsets and sizes.
It never extracts or writes any game data. The --check option reads each
entry's data to confirm it unpacks to the recorded size, then throws it away.

The format is described in docs/research/formats/resources-archive.md.

Works on Windows and Linux (including Steam/Proton installs).

Usage:
    python tools/resources.py "E:/SteamLibrary/steamapps/common/RAGE/base/gameresources_sp_01.patch"
    python tools/resources.py ARCHIVE --list --filter wellspring
    python tools/resources.py DLC_ARCHIVE --check --also BASE_ARCHIVE
"""

import argparse
import struct
import sys
import zlib
from collections import Counter
from pathlib import Path

MAGIC = bytes.fromhex("2294abcd")
HEADER_SIZE = 12
LANGUAGE_RECORD_SIZE = 24  # 20-byte name + 4-byte size
TAIL_SIZE = 20  # not yet understood, see the research note


def mb(size):
    return f"{size / (1024 * 1024):,.1f}"


class Table:
    """Reader for the table at the end of the archive. Numbers are big-endian
    except string lengths, which are little-endian."""

    def __init__(self, data):
        self.data = data
        self.pos = 0

    def u32(self):
        value = struct.unpack_from(">I", self.data, self.pos)[0]
        self.pos += 4
        return value

    def string(self):
        length = struct.unpack_from("<I", self.data, self.pos)[0]
        self.pos += 4
        value = self.data[self.pos:self.pos + length].decode("latin-1")
        self.pos += length
        return value


def read_entries(path):
    """Return (table_offset, entries) for one archive."""
    with open(path, "rb") as f:
        header = f.read(HEADER_SIZE)
        if header[:4] != MAGIC:
            sys.exit(f"{path}: not a .resources archive (magic {header[:4].hex(' ')})")
        table_offset, table_size = struct.unpack(">II", header[4:])
        f.seek(table_offset)
        table = Table(f.read(table_size))

    entries = []
    for _ in range(table.u32()):
        entry = {
            "id": table.u32(),
            "type": table.string(),
            "source": table.string(),
            "name": table.string(),
            "offset": table.u32(),
            "size": table.u32(),
            "stored": table.u32(),
            "languages": [],
        }
        for _ in range(table.u32()):
            record = table.data[table.pos:table.pos + LANGUAGE_RECORD_SIZE]
            language = record[:20].split(b"\0")[0].decode("latin-1")
            entry["languages"].append((language, struct.unpack(">I", record[20:])[0]))
            table.pos += LANGUAGE_RECORD_SIZE
        entry["tail"] = table.data[table.pos:table.pos + TAIL_SIZE]
        table.pos += TAIL_SIZE
        entries.append(entry)

    if table.pos != len(table.data):
        print(f"Warning: table parsing stopped at byte {table.pos} of {len(table.data)}", file=sys.stderr)
    return table_offset, entries


def unpacks(f, entry):
    """True if the entry's data unpacks to its recorded size from this file."""
    f.seek(entry["offset"])
    raw = f.read(entry["stored"])
    if len(raw) != entry["stored"]:
        return False
    if entry["stored"] == entry["size"]:
        return True  # stored uncompressed
    try:
        out = zlib.decompressobj(-15).decompress(raw, entry["size"] + 1)
    except zlib.error:
        return False
    return len(out) == entry["size"]


def check(path, entries, also):
    paths = [path] + [p.expanduser().resolve() for p in also]
    files = [open(p, "rb") for p in paths]
    # Show the parent folders too, since several archives share a file name
    labels = ["/".join(p.parts[-3:]) for p in paths]
    found = Counter()
    try:
        for entry in entries:
            if entry["stored"] == 0:
                found["empty"] += 1
                continue
            for f, label in zip(files, labels):
                if unpacks(f, entry):
                    found[label] += 1
                    break
            else:
                found["not found"] += 1
    finally:
        for f in files:
            f.close()
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("archive", type=Path, help=".resources or .patch file")
    parser.add_argument("--list", action="store_true", help="print every entry")
    parser.add_argument("--filter", help="only entries whose type or name contains this text")
    parser.add_argument("--check", action="store_true",
                        help="confirm each entry's data unpacks to its recorded size")
    parser.add_argument("--also", type=Path, action="append", default=[],
                        help="other archive(s) to try during --check (e.g. the base game's)")
    args = parser.parse_args()

    path = args.archive.expanduser().resolve()
    if not path.is_file():
        sys.exit(f"Not a file: {path}")

    table_offset, entries = read_entries(path)
    if args.filter:
        needle = args.filter.lower()
        entries = [e for e in entries if needle in e["type"].lower() or needle in e["name"].lower()]

    print(f"# {path.name}")
    print()
    print(f"{len(entries)} entries. Data region: {mb(table_offset)} MB.")
    print()
    print("## Types")
    print()
    print("| Type | Entries | Stored MB | Unpacked MB |")
    print("| --- | ---: | ---: | ---: |")
    by_type = {}
    for e in entries:
        by_type.setdefault(e["type"], []).append(e)
    for kind, group in sorted(by_type.items(), key=lambda kv: len(kv[1]), reverse=True):
        stored = sum(e["stored"] for e in group)
        size = sum(e["size"] for e in group)
        print(f"| `{kind}` | {len(group)} | {mb(stored)} | {mb(size)} |")

    if args.check:
        print()
        print("## Check")
        print()
        for label, count in check(path, entries, args.also).most_common():
            print(f"- {label}: {count}")

    if args.list:
        print()
        print("## Entries")
        print()
        print("| ID | Type | Name | Offset | Stored | Size | Languages |")
        print("| ---: | --- | --- | ---: | ---: | ---: | --- |")
        for e in entries:
            languages = ", ".join(lang or "(unnamed)" for lang, _ in e["languages"])
            print(f"| {e['id']} | `{e['type']}` | `{e['name']}` | {e['offset']} | "
                  f"{e['stored']} | {e['size']} | {languages} |")


if __name__ == "__main__":
    main()
