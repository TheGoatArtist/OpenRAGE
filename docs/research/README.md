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

## First targets

1. **Game install inventory.** List the folders and file types in a stock install with the
   DLC, plus their sizes. No game files leave your machine. Just record the names, counts and
   sizes.
2. **Archive format.** How the game packages its assets.
3. **Megatexture page data.** How the virtual texture is stored on disk.
