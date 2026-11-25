# Saved binaries

Known-good builds kept for testing, covered by GPL-3.0 (see the root
[LICENSE](../LICENSE)); the MeshCard part of the bridge `.hex` files is MIT
([firmware/u115_bridge/LICENSE](../firmware/u115_bridge/LICENSE)). Their
sources are listed below.

## 2026-11-18-working

| File | What it is | Source |
|---|---|---|
| `meshtasticd-arm64` | Meshtastic `meshtasticd` for the Cardputer Zero (Linux, arm64), with U115 support | [meshtastic/firmware](https://github.com/meshtastic/firmware) commit `647831b251fb2349a8ce85d78490874ccb3bffac` with `meshtastic-u115.patch` (in this folder) applied. GPL-3.0. |
| `meshtastic-u115.patch` | The MeshCard changes used for that build | — |
| `bridge.hex`, `bridge-direct.hex` | U115 bridge firmware (ASR6501) | Bridge version 3: [`firmware/u115_bridge/`](../firmware/u115_bridge/) as of commit `8055c37` (this repository's first commit), built with Heltec CubeCell-Arduino 1.4.0. Rebuilding that commit gives a byte-identical `bridge.hex` (checked 25 Nov 2026); `bridge-direct.hex` is `bridge.hex` run through `tools/patch_direct_boot.py`. The current folder holds version 4 (a CPU-rest test), which is not in these files. |

The bridge `.hex` files also contain code from the Heltec CubeCell core, which
has its own terms (Cypress end-user license, Semtech Revised BSD, Arduino LGPL);
see [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md#4-code-built-into-the-u115-bridge-firmware-not-stored-here).
The Semtech notice that must come with these binaries (from
`cores/asr650x/lora/LICENSE.txt` in CubeCell-Arduino 1.4.0):

    --- Revised BSD License ---
    Copyright (c) 2013, SEMTECH S.A.
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:
        * Redistributions of source code must retain the above copyright
          notice, this list of conditions and the following disclaimer.
        * Redistributions in binary form must reproduce the above copyright
          notice, this list of conditions and the following disclaimer in the
          documentation and/or other materials provided with the distribution.
        * Neither the name of the Semtech corporation nor the
          names of its contributors may be used to endorse or promote products
          derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL SEMTECH S.A. BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
