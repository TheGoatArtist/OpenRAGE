# Credits

OpenRAGE builds directly on other people's work. The engine in [engine/](engine/) is a modified
RBDOOM-3-BFG, and RBDOOM-3-BFG is a modified Doom 3 BFG Edition. Both are credited here in the same
way, because OpenRAGE uses both of their code.

Being credited here doesn't mean anyone below endorses OpenRAGE. As id Software's additional
terms (in [COPYING.txt](COPYING.txt)) require, their names aren't used to promote the project, and
OpenRAGE doesn't claim to be the original program.

## id Software: Doom 3 BFG Edition

The Doom 3 BFG Edition GPL source code, released in 2012, is the foundation of the engine.

- Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
- Source: <https://github.com/id-Software/DOOM-3-BFG>
- License: GNU GPL v3 with id Software's additional terms, shipped unchanged as
  [COPYING.txt](COPYING.txt).

## RBDOOM-3-BFG: Robert Beckebans and contributors

RBDOOM-3-BFG modernized Doom 3 BFG Edition with a new renderer (NVRHI with Vulkan and DirectX 12,
physically based rendering, global illumination, HDR and post-processing), 64-bit and
cross-platform support, TrenchBroom mapping support and much more. OpenRAGE's engine is built
from it.

- Copyright (C) 2012 and later, Robert Beckebans and the RBDOOM-3-BFG contributors (each source
  file's header names its exact holders and years).
- Source: <https://github.com/RobertBeckebans/RBDOOM-3-BFG>
- Imported at upstream commit `ea29c00` (version 1.6.0). See
  [engine/README.md](engine/README.md) and [engine/RELEASE-NOTES.md](engine/RELEASE-NOTES.md) for
  the project's own credits and history.

Contributors, as they appear in RBDOOM-3-BFG's git history up to that commit, ordered by number of
commits (some people appear under more than one name):

Robert Beckebans, Stephen Saunders, SRSaunders, Daniel Gibson, HarrievG, RobertBeckebans, Tobias
Frost, y2keeth, r-a-sattarov, Jonathan Young, Stephen Pridham, Eric Womer, Felix Rueegg,
BielBdeLuna, Admer, Radegast, Andreas Bergmeier, CodeLikeCXK, Trung Lê, Wintermute0110,
raynorpat, Brian Harris, Codes4Fun, Yamagi Burmeister, Darren Salt, Pat Raynor, dekadenZ,
Alejandro Piñeiro, Biel Bestué de Luna, Dan McGoo, Karin, LWSS, Shane Utt, vloup, Caffeine Code,
Christoph Korn, Davide Beatrici, Ensiform, Gianfranco Costamagna, Joe, Jonathan Gray, Klaus
Silveira, Pete Lewis, Richard Tollerton, Thomas Green, Trung Le, palmalcheg, Aleksey Medvedev,
Carlos Estrague, DEft, Dmitry Shapovalov, ErickMx, EricxDu, George Kalampokis, GoGo de Sica,
Harrie van Ginneken, Hugo Locurcio, IISergII, Ivan Vashchaev, James Dunne, Jeffrey N. Johnson,
Krzysztof Otręba, LeadHead, Lightwave, LinAGKar, Malcolm Mallardi, Matteo Pacini, Michael Limb,
Mike Swanson, Ophelia Beatrice de Sica, Philip Woods, Reto Schneider, SpiritQuaddicted,
SubjectName, Syed Ahkam, Tim Howard, akacross, darealshinji, diegorodriguezv, ignacio, numas13,
shmerl.

If your name is missing or wrong, please open an issue.

## Third-party libraries

The engine includes many third-party libraries, among them NVIDIA's NVRHI and ShaderMake, Dear
ImGui, Vulkan Memory Allocator, OpenAL Soft, zlib, stb, RapidJSON, Optick, mikktspace, tinyexr,
Ogg Vorbis, libbinkdec, Intel's ISPC Texture Compressor and Masked Occlusion Culling. Each keeps
its own license. [engine/LICENSE_EXCEPTIONS.md](engine/LICENSE_EXCEPTIONS.md) lists the code that isn't
covered by the GPL and the license that applies to it.

OpenRAGE's own tools also include code under other licenses. For example,
[tools/rage-jxr/](tools/rage-jxr/) is based on Microsoft's jxrlib (see its
[LICENSE-jxrlib.txt](tools/rage-jxr/LICENSE-jxrlib.txt)).

## OpenRAGE

Everything else is by the OpenRAGE contributors. See the repository's git history.

RAGE, DOOM and id Tech are trademarks of their respective owners. OpenRAGE is an unofficial fan
project and isn't affiliated with or endorsed by id Software, Bethesda Softworks, ZeniMax Media or
Microsoft.
