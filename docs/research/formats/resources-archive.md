# `.resources` / `.patch` archive format

- Game version / build: Steam build `1509967` (`Rage64.exe` 1.0.35.4669), with all DLC.
- Files examined: `base/gameresources.resources`, `base/gameresources_sp_01.patch`,
  `mp/base/gameresources.resources`, `mp/base/gameresources_mp_01.patch`,
  `dlc/dlc3/base/gameresources.resources`. Only headers and tables were read by hand.
  Entry data was unpacked in memory to check sizes, then thrown away.
- Tools used: hex dumps, then [tools/resources.py](../../../tools/resources.py), written from these notes.
- Confidence: **verified** for the header, the table layout and the compression (every entry in
  five archives); **partially verified** for how DLC and patch archives relate to the base archive;
  **unknown** for the last 20 bytes of each entry and for `streamed.resources` / `.streamed`.
- Date: 2026-10-30

## What we know

`.resources` and `.patch` files use the same format. Each file has three parts:

| Part | Position | Contents |
| --- | --- | --- |
| Header | bytes 0–11 | Magic `22 94 AB CD`, then the table's offset and the table's size |
| Data | from byte 12 or 16 to the table | Each entry's data, one after another |
| Table | end of file | Entry count, then one record per entry |

Table offset + table size equals the file size in every archive tested.

**Byte order.** All numbers are big-endian, except the length that comes before each string, which
is little-endian. Big-endian numbers suggest the format is shared with the console versions
(guess).

### Table

The table starts with a 4-byte entry count. Each entry then has:

| Field | Size | Notes |
| --- | --- | --- |
| ID | 4 | Usually 0 and up. Engine built-ins (for example `_globalshadowsimage`) count down from `0xFFFFFFFF` |
| Type | string | Resource type, such as `image`, `model`, `anim`, `material`, `sample`, `entityDef`. The DLC3 archive uses about 100 types |
| Source name | string | The asset the entry was built from, for example a `.tga` path. Empty for built-ins |
| Name | string | The built file, usually under `generated/` (`.bimage`, `.bmd6model`, `.bmvtr` and so on) |
| Offset | 4 | Where the data starts (see "Where the data lives" below) |
| Size | 4 | Size after unpacking |
| Stored size | 4 | Size in the archive. Equal to Size when stored uncompressed, 0 for empty entries |
| Language count | 4 | Usually 0 |
| Language records | 24 each | A 20-byte zero-padded language name, then a 4-byte size |
| Unknown | 20 | See open questions |

A string is a 4-byte length followed by that many bytes, with no terminator.

**Languages.** Voice-line `sample` entries carry 8 records: English, French, Italian, German,
Spanish, Russian, Polish and Japanese, each with a different size. These probably locate the
per-language audio (guess). Some non-voice samples have a single record with an empty name.

### Compression

Compressed entries use raw deflate (zlib with no header). Every compressed entry in all five
archives unpacked to exactly its recorded size (see "Check results"). Small text files, such as
some decls, tables and `renderParm` entries, are stored uncompressed.

### Where the data lives

- **Base archives** hold all of their own data.
- **Patch archives** hold their own data. 6,292 of the 6,328 entries in
  `gameresources_sp_01.patch` have the same name as an entry in the base archive, with different
  IDs. So a patch most likely replaces base entries by name (partially verified).
- **The DLC3 archive is a full index that reuses the base game's data.** It lists 87,629 entries.
  65,793 of them are exact copies of a base entry: same name, offset and sizes. Their data is in
  `base/gameresources.resources`, not in the DLC file. The remaining entries point into the DLC
  file itself. No field we have decoded says which file to read. The likeliest explanation is that
  the engine matches these entries against the base archive's table (guess).

### Check results

`tools/resources.py --check` reads every entry, confirms it unpacks to its recorded size, and
discards the data. Results for this build:

| Archive | Entries | Unpack correctly | Empty |
| --- | ---: | ---: | ---: |
| `base/gameresources.resources` | 74,696 | 74,330 | 366 |
| `base/gameresources_sp_01.patch` | 6,328 | 6,328 | 0 |
| `mp/base/gameresources.resources` | 33,445 | 33,295 | 150 |
| `mp/base/gameresources_mp_01.patch` | 2,723 | 2,723 | 0 |
| `dlc/dlc3/base/gameresources.resources` | 87,629 | 87,117 (21,328 from the DLC file, 65,789 from the base archive) | 512 |

No entry failed. The DLC3 split was measured by trying the DLC file first and the base archive
second. The two patches were also checked with their base archive as a fallback, and never needed
it.

## Open questions

- **The last 20 bytes of each entry.**
  - Word 1 is almost always `00 FF FF FF`. When it isn't, it may be an offset into
    `streamed.resources` (guess).
  - Word 2 is usually 0 or 1, but holds stray text in a few entries, so it may be uninitialized
    padding.
  - Word 3 is the same within a resource type (for example `models` and `samples` share one value),
    so it might be a loader version or type code.
  - Word 5 is 0, 1 or 8 and looks like flags.
- **How exactly the engine decides** that a DLC entry's data is in the base archive.
- **`streamed.resources` and `english.streamed`** have no header or table. They are probably found
  through the `sample` entries' language records and word 1 above.
- **How the game chooses between archives at runtime:** load order, `mp/` versus `base/`, and the
  `hashSeed` in `dlc3/info.txt`.
