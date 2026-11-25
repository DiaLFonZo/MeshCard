# MeshCard changes to Meshtastic device-ui

This folder is a copy of [meshtastic/device-ui](https://github.com/meshtastic/device-ui)
(GPL-3.0, see `LICENSE`), modified for MeshCard. This notice is required by
GPL-3.0 section 5a.

- Base: commit `916595f3fb3fbcd2ae5023efd26b207397e2a2b9` (`master`, 13 Nov 2026)
- Changed by: MeshCard (DiaLFonZo, with Claude), November 2026
- Purpose: fit the Meshtastic UI on the Cardputer Zero screen.

## Files modified

- `source/graphics/driver/FBDriver.cpp` — adds a block under `#ifdef FB_SHRINK`
  that scales the UI down to the Cardputer Zero framebuffer.

No file was added or removed.
