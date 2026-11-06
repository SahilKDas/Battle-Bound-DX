# Battle Bound DX — attribution and provenance

Battle Bound DX is planned as an adaptation of **Battle Bound** by
[@KIKOKO_](https://scratch.mit.edu/users/KIKOKO_/).
The supplied project embeds a TurboWarp link identifying
[Scratch project 517764131](https://scratch.mit.edu/projects/517764131/).
That project page could not be independently retrieved during this audit;
the author and source link above are supported by the embedded project.

## Preserved source and changes

`Battle Bound.sb3` was deleted at the user's request after byte-for-byte
verification of every extracted file. `scratch/original/` contains its extracted
contents; `scratch/inventory.json` records the source SHA-256 and original
comments and asset references. Current changes add extraction tooling,
searchable script listings, an asset catalog, and architecture documentation.
The C++26 research port adds a native compatibility runtime, content compilation,
rasterized asset cache and offline entry point. It clears stale cloud snapshots,
seeds the local version marker and mutes audio by default. Runtime rendering and
scheduling may differ from Scratch; see `docs/NATIVE_PORT.md`. Future DX releases must describe their
changes and retain the original attribution and applicable license notices.

## Credits supplied inside the original project

The `[UI]` costume `about_credits` attributes:

- @KIKOKO_: the remaining original work, after the credits below.
- @XShrunk: multiplayer engine, testing and ideas.
- @griffpatch: platform collision code.
- Epic Games: shotgun sound.
- @Will_Wam's Ninja 5: shuriken sound.
- YouTube library: gun sounds (specific recordings/licenses not identified).
- Scratch library: some sound effects (specific files not identified).
- Among Us: inspiration for some hats.
- Disney's Mandalorian: helmet design; the vector is credited to the author.
- Beta testers: @TimMcCool, @XShrunk, @iDuoof, @Carot3, @I8D, @INorth,
  @missyq, @spazestudios.

Stage comments also identify music as Phantom Power's Overpowered, Skyros,
and Battle Ground, and a fanmade Battlefield 2042 theme. This records the
original claims; it does not establish permission for any third-party asset.

## Licensing policy for the adaptation

The existing repository `LICENSE` contains CC BY-SA 4.0 and is preserved.
Its presence alone does not prove the original uploader licensed every
included asset under that version. Confirm the original project's publication
license/history or obtain appropriate permission before public distribution
outside Scratch. Do not silently relabel inherited content with a new license.

For content verified as CC BY-SA, retain creator/title/source/license notices,
mark modifications, and distribute adaptations under the applicable ShareAlike
terms. Do not add restrictions that conflict with those terms. Preserve credits
in repository documentation and carry them into the eventual game's credits.

Scratch's current [Terms of Service](https://mitscratch.freshdesk.com/en/support/solutions/articles/4000219182-scratch-terms-of-service),
last updated January 22, 2026, section 8.2(a), describes personal remix rights
within the service. It is not evidence of a universal CC license for an external
port. Scratch's [remixing help](https://mitscratch.freshdesk.com/en/support/solutions/articles/4000156850-remixing-what-is-it-and-why-can-every-project-on-scratch-be-remixed-)
still describes shared projects as Creative Commons Share Alike. Record this
difference rather than assuming today's terms identify this project's historic
license. A valid existing CC grant is not revocable while its terms are followed.

References: [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/),
[CC BY-SA 2.0](https://creativecommons.org/licenses/by-sa/2.0/).

Third-party music, recordings and recognizable franchise designs require
asset-specific provenance/permissions or replacement before a public DX release.
They remain preserved here for inspection, not marked as cleared DX assets.
Scratch's own software/support materials and trademarks have separate rules
under sections 7.1–7.3; do not imply Scratch endorses DX. Reusing Scratch engine
code would require checking that component's actual software license separately.

## Native port dependencies

The native runtime uses raylib 5.5 by Ramon Santamaria and contributors;
the installed distribution's license is retained in `licenses/raylib.txt`.
Raylib and GLFW are statically linked from the existing toolchain. GLFW's
license is retained in `licenses/glfw.txt`. Their license notices and this
credits document are embedded in the standalone executable. They are not
relicensed as project content. Build-only tools include Python and PyMuPDF; their packages
are not included in the game executable or copied into this repository.

The native port now defaults to a windowed 1280×720 viewport, uses 2× SVG-derived
textures and packages assets inside the executable without runtime extraction.
The original asset files remain unchanged.

The Vulkan renderer uses the Khronos Vulkan API declarations from the installed
Vulkan-Headers SDK (Copyright 2015–2026 The Khronos Group Inc., Apache-2.0).
The Vulkan SDK and loader are not redistributed; the graphics driver supplies
the runtime loader. SPIR-V shaders in the executable are compiled from this
repository's shader sources. Raylib remains for CPU image decoding and audio;
the game no longer creates an OpenGL rendering context.

The replay/content milestone renames imported media to descriptive sprite/asset
paths under `assets/source/`, without changing their bytes. Original identities,
checksums and shared aliases remain in `assets/manifest.json`. New typed item
definitions and deterministic gameplay trace tools are local DX additions.
