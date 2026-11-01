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

## License

Decided by both maintainers on 2026-10-31: **GPL-3.0-or-later** ([LICENSE](../LICENSE)), for
everything in the repository, documentation included.

- **Why GPL.** Anyone can use and change OpenRAGE, but anything released that builds on it must
  stay open source too. That keeps the work with the community, matches the non-commercial
  spirit of the Tool Kit terms, and is the norm for projects like openQ4, OpenMW and OpenRCT2. It
  also lets the project reuse GPL code from Doom 3 BFG or RBDOOM-3-BFG if it ever decides to.
- **SDK exception.** Plain GPL doesn't allow shipping the code combined with closed libraries
  that aren't part of the operating system. [LICENSE-EXCEPTION.md](../LICENSE-EXCEPTION.md)
  grants an additional permission (GPL section 7) for proprietary vendor SDKs such as DLSS, XeSS
  and Steamworks. It only covers code the OpenRAGE contributors own, not code taken from other
  GPL projects.
- **Compatible dependencies.** The planned open libraries (Vulkan headers and loader, volk, VMA,
  SDL3, Dear ImGui, FidelityFX/FSR, FFmpeg, Jolt) are all under licenses that work with
  GPL-3.0. GPU drivers count as system libraries.
- **Steam policy is separate.** Whether an unofficial program may unlock RAGE's Steam
  achievements is a question for Valve's and Bethesda's rules, not the license. The core build
  should work without Steam.
- **Contributions.** By contributing, you license your work under the same terms, including the
  SDK exception.

## Name and trademarks

"RAGE" is a trademark of its owners. "OpenRAGE" is used descriptively for an unofficial fan
project. If the rights holders object, the project may need to rename.
