# OpenRAGE

> *Preserving the wasteland. Rebuilding the technology. Expanding the experience.*

OpenRAGE is an open-source project to preserve and modernize **RAGE (2011)**. It's inspired by
openQ4's work on Quake 4. The goal is to keep RAGE's
identity, atmosphere, gameplay and artistic intent intact while making it run better on modern
hardware. We also want to expand the areas that never got enough attention, mainly modding and
multiplayer.

OpenRAGE is **not a remake**. The original game stays at the center of everything.

## Status

**Early engine work.** The file-format research is under [docs/research/](docs/research/). The
engine lives in [engine/](engine/): it's built from [RBDOOM-3-BFG](https://github.com/RobertBeckebans/RBDOOM-3-BFG),
which is based on id Software's Doom 3 BFG Edition source code. It starts as OpenRAGE but doesn't
run RAGE yet. See [docs/engine.md](docs/engine.md) for how to build it and what's being worked on.
The phases under [docs/](docs/) are ideas, not a committed roadmap.

## Core philosophy

- Preserve the original game's identity, atmosphere, gameplay and artistic intent.
- Preserve and fix the original **megatexture** system instead of replacing it with a different
  texture technology.
- Improve performance, stability and compatibility on modern hardware.
- Expand what felt underdeveloped, especially modding tools and multiplayer.
- Keep the original game at the center. Don't turn it into a different game.

## Goals at a glance

| Area | Goal |
| --- | --- |
| Megatexture technology | Preserve and optimize the original system, find its weaknesses, and improve streaming reliability and performance. |
| Modern performance | Stable high frame rates without breaking physics or gameplay. Explore upscalers such as DLSS and FSR. |
| Modding | Build on the original RAGE Tool Kit and give creators more useful capabilities. |
| Multiplayer | Preserve the original modes and explore additions that help the community grow. |
| Unified experience | Explore merging the best of the 32-bit and 64-bit releases into one 64-bit build, with mods and multiplayer kept clearly separate. |

See [docs/vision.md](docs/vision.md) for details and [docs/roadmap.md](docs/roadmap.md) for the
phases under discussion.

## You need your own copy of RAGE

OpenRAGE will **never** distribute RAGE's game files. To use it, you'll need a legitimate copy
of the game. See [docs/legal.md](docs/legal.md).

## Get involved

OpenRAGE is the flagship project of a wider **RAGE fan community**. Everyone who loves the
games is welcome, whether or not they write code. See [CONTRIBUTING.md](CONTRIBUTING.md) and
[docs/community.md](docs/community.md). To set up your machine (Windows or Linux), see
[docs/getting-started.md](docs/getting-started.md).

## Disclaimer

OpenRAGE is an unofficial fan project. It is not affiliated with or endorsed by id Software,
Bethesda Softworks, ZeniMax Media or Microsoft. RAGE and id Tech are trademarks of their
respective owners.

## Credits

OpenRAGE is built on other people's work, and they're credited alongside each other:

- **id Software** for the Doom 3 BFG Edition GPL source code.
- **Robert Beckebans and the RBDOOM-3-BFG contributors** for RBDOOM-3-BFG, the engine OpenRAGE is
  built from.

See [CREDITS.md](CREDITS.md) for the full list.

## License

OpenRAGE uses the same license as Doom 3 BFG Edition: the **GNU General Public License v3** with
**id Software's additional terms**. [COPYING.txt](COPYING.txt) is byte-for-byte the license file id
Software shipped with the Doom 3 BFG Edition source code. Third-party code in the engine that isn't
covered by the GPL is listed in [engine/LICENSE_EXCEPTIONS.md](engine/LICENSE_EXCEPTIONS.md).

[LICENSE-EXCEPTION.md](LICENSE-EXCEPTION.md) adds a permission for proprietary vendor SDKs such as
DLSS, XeSS and Steamworks. It only covers code the OpenRAGE contributors wrote, not code from Doom 3
BFG or RBDOOM-3-BFG.

None of this covers RAGE itself: the game's files stay the property of their owners and are never
included. See [docs/legal.md](docs/legal.md).
