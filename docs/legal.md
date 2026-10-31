# Legal and asset policy

> This isn't legal advice. It records the project's current intentions and open questions.

## Game assets

- OpenRAGE **never redistributes** RAGE's or its DLC's game files. That includes archives,
  textures, models, audio, maps and executables.
- Players supply their own legitimate copy of the game. OpenRAGE reads from that install.
- The [.gitignore](../.gitignore) blocks common game-data file types as a safety net.

## RAGE Tool Kit terms

The RAGE Tool Kit (Steam app 205860) comes with its own terms (`docs\pages\TermsAndConditions.html`
in the Tool Kit install). In summary:

- The Tool Kit stays Bethesda's copyrighted property.
- Anything made with it ("New Materials") is for **personal, non-commercial use with your own
  copy of RAGE**.
- New Materials may be shared **only free of charge, and only with people who own RAGE**.
  Selling them, or including them in compilations, needs Bethesda's written permission.
- Sharing New Materials gives Bethesda a permanent, royalty-free right to use them.
- The terms also bring in RAGE's own license and the Steam Subscriber Agreement.

What this means for OpenRAGE:

- A public repository isn't limited to people who own RAGE. So **nothing from the Tool Kit
  and nothing it builds is committed**: no source assets (`.tga`, `.md6mesh`, `.map`, `.decl`
  and so on), no built mod packages and no test archives, however small.
- Research notes written in our own words, ImHex pattern files and our own tools are fine.
- OpenRAGE's code isn't made with the Tool Kit, so these terms don't decide the project's own
  license. They do limit what test data we can share. Each contributor builds their own test
  files locally.

## Reverse engineering approach (open decision)

Because id Tech 5 is proprietary, the project has to decide how contributors may study the
original binaries. The usual options are:

- **Clean-room.** One group studies the binaries and writes *specifications* in plain
  language. A separate group writes code only from those specifications. This is the safest
  option and the most work.
- **Documented-understanding.** Contributors may study the binaries in tools such as Ghidra.
  They write original code from their understanding and never copy decompiled output into the
  repository.

Whichever option is chosen, **decompiled or disassembled code is never committed.**

## License (open decision)

OpenRAGE inherits no license from id Software's code, so the project can choose freely. The
common options are:

- **GPL-3.0.** Copyleft: forks and derived works must stay open source. openQ4 and the other
  id Tech 4 source ports use GPL because id's released code required it.
- **MIT or zlib.** Permissive: anyone can reuse the code, including in closed-source projects.

## Name and trademarks

"RAGE" is a trademark of its owners. "OpenRAGE" is used descriptively for an unofficial fan
project. If the rights holders object, the project may need to rename.
