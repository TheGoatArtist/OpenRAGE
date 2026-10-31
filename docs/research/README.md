# Research notes

This folder holds what we learn about how RAGE works: its file formats, engine systems and
behavior. Write notes in your own words. Never paste game data or decompiled code (see
[../legal.md](../legal.md)).

## Suggested layout

```
research/
  formats/     one file per file format (e.g. resources-archive.md, megatexture-pages.md)
  engine/      runtime systems (streaming, renderer, physics tick, scripting)
  versions.md  which game builds exist (32-bit, 64-bit, DLC, store versions) and how they differ
```

## Note template

```markdown
# <Format or system name>

- Game version / build:
- Files examined:
- Tools used:
- Confidence: guess / partially verified / verified

## What we know

## Open questions
```

## Notes so far

- [install-inventory.md](install-inventory.md): layout, file types and sizes of a Steam install with DLC
- [toolkit-inventory.md](toolkit-inventory.md): the RAGE Tool Kit's source assets, formats, docs and mod builds
- [formats/resources-archive.md](formats/resources-archive.md): the `.resources` / `.patch` archive format

## First targets

1. ~~**Game install inventory.**~~ Done. See above.
2. **Archive format.** Mostly done for `.resources` / `.patch`; see
   [formats/resources-archive.md](formats/resources-archive.md). `streamed.resources` and
   `.streamed` files are still unknown.
3. **Megatexture page data.** How the virtual texture is stored on disk.
