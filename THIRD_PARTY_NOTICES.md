# Third-party notices and provenance

This file lists every outside project that MeshCard copies, modifies, links
into its binaries, or uses as a tool, with its source, authors and license.
It also states which parts are original MeshCard work.

The README has the short version: [Credits and Acknowledgments](README.md#credits-and-acknowledgments).

Each copied project keeps its own `LICENSE` and copyright headers unchanged.
Where MeshCard changed a copied project, a `MESHCARD_CHANGES.md` file in that
folder lists the changes (GPL-3.0, section 5a).

## 1. Upstream projects copied and modified in this repository

### Meshtastic firmware — `meshtastic-firmware/`

- Source: https://github.com/meshtastic/firmware
- Authors: the Meshtastic project and its contributors
- License: GPL-3.0 (`meshtastic-firmware/LICENSE`, unchanged)
- Base: commit `647831b251fb2349a8ce85d78490874ccb3bffac` (`develop`, 17 Nov 2026, firmware 2.8.3)
- Status: **modified** — 8 files added, 24 files modified, none removed.
  See [meshtastic-firmware/MESHCARD_CHANGES.md](meshtastic-firmware/MESHCARD_CHANGES.md).
- Not included: the git submodules `protobufs` (`62b09edf`) and `meshtestic`
  (`dcac7e56`). Get them from the upstream repository at those commits.
- `src/platform/portduino/u115/include/libpinedio-usb.h` is based on
  Meshtastic's own `src/platform/portduino/windows/include/libpinedio-usb.h`,
  which follows the API of Pine64's
  [libch341-spi-userspace](https://github.com/pine64/libch341-spi-userspace).

### Meshtastic device-ui — `device-ui/`

- Source: https://github.com/meshtastic/device-ui
- Authors: mverch67 and the Meshtastic contributors
- License: GPL-3.0 (`device-ui/LICENSE`, unchanged). Its `library.json`
  declares `GPL-3.0 AND BSD-3-Clause AND Apache-2.0 AND MIT` for the parts it bundles.
- Base: commit `916595f3fb3fbcd2ae5023efd26b207397e2a2b9` (`master`, 13 Nov 2026)
- Status: **modified** — 1 file. See [device-ui/MESHCARD_CHANGES.md](device-ui/MESHCARD_CHANGES.md).

### MeshZero — `meshcard/`

- Source: https://github.com/s0liton/MeshZero
- Author: Pedro Santos ([s0liton](https://github.com/s0liton))
- License: GPL-3.0-only (`meshcard/LICENSE`, unchanged; SPDX headers kept in every file)
- Base: commit `22ec6f9` (17 Nov 2026)
- Status: **modified** — the MeshCard screen app is MeshZero's app, extended.
  21 files modified, 26 added, none removed.
  See [meshcard/MESHCARD_CHANGES.md](meshcard/MESHCARD_CHANGES.md).
- MeshZero's own third-party notices (nanopb, quirc, Meshtastic protobufs,
  LVGL, fonts) are kept in [meshcard/THIRD_PARTY_NOTICES.md](meshcard/THIRD_PARTY_NOTICES.md).
  In short:
  - nanopb — https://github.com/nanopb/nanopb — zlib license, Copyright (c) 2011 Petteri Aimonen (`meshcard/third_party/nanopb/`, unchanged)
  - quirc 1.2 — https://github.com/dlbeer/quirc — ISC license, Copyright (C) 2010-2012 Daniel Beer (`meshcard/third_party/quirc/`, unchanged)
  - Meshtastic protobufs v2.7.25 — https://github.com/meshtastic/protobufs — GPL-3.0 (generated code in `meshcard/src/proto/gen/`, unchanged)
  - LVGL v9.3.0 — https://github.com/lvgl/lvgl — MIT (downloaded at build time, not stored here)
  - Noto Emoji — https://github.com/googlefonts/noto-emoji — SIL Open Font License 1.1, Copyright 2013 Google LLC

## 2. Added by MeshCard to the copied projects

- **Montserrat** fonts (`meshcard/assets/fonts/Montserrat-Bold.ttf`,
  `Montserrat-Medium.ttf`, `Montserrat-SemiBold.ttf`, version 9.000) —
  https://github.com/JulietaUla/Montserrat — SIL Open Font License 1.1
  (`meshcard/assets/fonts/Montserrat-OFL.txt`). Copyright 2011 The Montserrat
  Project Authors.
- **Pre-rendered ADV fonts** (`meshcard/src/ui/fonts/adv/*.c`) — generated from
  the Montserrat files above with [lv_font_conv](https://github.com/lvgl/lv_font_conv)
  1.5.2 (MIT). The font data stays under the SIL Open Font License 1.1.

## 3. Original MeshCard work

Written for MeshCard by DiaLFonZo, with help from Claude (see section 7):

- `firmware/u115_bridge/` — the U115 bridge firmware (turns the U115's
  ASR6501 into a radio the Cardputer drives over the Grove UART), and `firmware/build.ps1`.
- `tools/` — build, flash, test and diagnostic scripts.
- `zero/` — Cardputer Zero setup, launcher and test scripts.
- `docs/` — how-to pages and screen mockups.
- `meshcard/assets/meshcard-logo.png`, `meshcard/assets/meshcard-icon-80.png` — MeshCard logo and icon.
- The changes and additions listed in each `MESHCARD_CHANGES.md`.

Unless a file says otherwise, original MeshCard work is released under GPL-3.0
(root [LICENSE](LICENSE)), the license of the projects it extends. The one
exception is the U115 bridge code (`firmware/u115_bridge/`), which is under the
MIT license ([firmware/u115_bridge/LICENSE](firmware/u115_bridge/LICENSE)): it
runs on its own chip, is not built with any GPL code, and is compiled with the
Heltec core listed in section 4.

## 4. Code built into the U115 bridge firmware (not stored here)

The bridge (`firmware/u115_bridge/`) is compiled with the Heltec CubeCell
Arduino core, so the bridge `.hex` files (in `saved/` and in releases) contain
parts of it:

- **Heltec CubeCell-Arduino** 1.4.0 — https://github.com/HelTecAutomation/CubeCell-Arduino —
  the repository declares no license.
- Inside that core:
  - Cypress PSoC Creator generated code — "Copyright (c) 2007-2018 Cypress
    Semiconductor", under the Cypress end-user license agreement.
  - Semtech LoRaMac radio and board drivers — Copyright (c) 2013 SEMTECH S.A.,
    Revised BSD license (binaries must reproduce the copyright notice and license).
  - Arduino core files (`Print.h`, `Stream.h`, `WString.h`, `HardwareSerial.h`,
    `SPI.h`, ...) — GNU Lesser General Public License 2.1 or later.

## 5. Tools used but not included

- [arduino-cli](https://github.com/arduino/arduino-cli) and [PlatformIO](https://platformio.org/) — build tools
- [pyOCD](https://github.com/pyocd/pyOCD) (Apache-2.0) — flashing the U115
- [Raspberry Pi debugprobe](https://github.com/raspberrypi/debugprobe) — SWD probe firmware on a Pico
- [intelhex](https://github.com/python-intelhex/intelhex) (BSD-3-Clause), [pyusb](https://github.com/pyusb/pyusb) (BSD-3-Clause), [libusb-package](https://github.com/pyocd/libusb-package) (Apache-2.0), [paramiko](https://github.com/paramiko/paramiko) (LGPL-2.1) — Python libraries used by `tools/`
- [Contact](https://github.com/pdxlocations/contact) by pdxlocations (GPL-3.0) — terminal Meshtastic client, started by `zero/contact_zero.py` when it is installed on the Zero

## 6. Trademarks

Meshtastic® is a registered trademark of Meshtastic LLC. `zero/meshtastic_100.png`
(drawn by `zero/make_icon.py`) shows the Meshtastic logo as the launcher icon
for the Meshtastic apps on the Zero. M5Stack, Cardputer, Heltec, CubeCell and
other names are trademarks of their owners.

MeshCard is an independent project. It is not affiliated with or endorsed by
Meshtastic LLC, the MeshZero author, M5Stack or Heltec Automation.

## 7. AI assistance

MeshCard was developed by DiaLFonZo with substantial help from **Claude**
(Anthropic), used through Claude Code: much of the MeshCard code and
documentation was written by Claude under DiaLFonZo's direction and testing.
Commits made this way carry a `Co-Authored-By: Claude` line.

The investigation reports `docs/codex-*.md` were written with OpenAI Codex.
