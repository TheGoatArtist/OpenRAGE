# OpenRAGE

> *Preserving the wasteland. Rebuilding the technology. Expanding the experience.*

OpenRAGE is an open-source project to preserve and modernize **RAGE (2011)**. It's inspired by
openQ4's work on Quake 4. The goal is to keep RAGE's
identity, atmosphere, gameplay and artistic intent intact while making it run better on modern
hardware. We also want to expand the areas that never got enough attention, mainly modding and
multiplayer.

OpenRAGE is **not a remake**. The original game stays at the center of everything.

## Status

**Pre-implementation.** The project is in the research and planning stage. No engine code
exists yet. Everything under [docs/](docs/) describes ideas and possible phases. None of it is
a committed roadmap.

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

## License

*To be decided.* See the open questions in [docs/legal.md](docs/legal.md).
