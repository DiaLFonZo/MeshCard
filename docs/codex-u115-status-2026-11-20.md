# U115 radio on MeshCard: status and hand-off (2026-11-20, 15:30)

## Setup
- Repo: `C:\00 - GitHub\MeshCard` (branch main, uncommitted work in `meshtastic-firmware/`).
- Two hosts, same Meshtastic fork (2.8.3):
  - **Cardputer ADV**: ESP32-S3 firmware, env `m5stack-cardputer-adv-meshcard` (`-D U115_BRIDGE_ESP32`). Build `tools/adv_build.sh` (WSL), flash `tools/adv_flash.ps1` (COM8, app at 0x10000).
  - **Cardputer Zero**: Raspberry Pi CM0, Linux `meshtasticd` (Portduino env `native-u115`, `-D PINEDIO_U115`), plus `meshzerod` and the MeshCard app. Build `tools/wsl_rebuild.sh <changed files>` (arm64 chroot under qemu, about 6 min for 1–2 .cpp files; touching a widely included header such as `RadioLibInterface.h` rebuilds about 200 files, about 1 h). Install: strip, `tools/zero_put.py` to `/home/pi/meshtasticd.new`, move to `/usr/local/bin/meshtasticd`, restart service `meshzero`. SSH helper: `tools/zero_ssh.py`.
- Radio on both: **M5Stack U115** = Ai-Thinker **Ra-07H** (ASR6501 SiP = PSoC4 MCU + SX1262). The PSoC runs our own **UART bridge firmware v3** (frames `A5 cmd len16 data xor`; 0x01 PING, 0x02 SPI XFER, 0x03 RESET, 0x04 PINS, 0x05 ANTSW P6.1; replies `5A`, cmd|0x80). The host drives the SX1262 through RadioLib over this bridge (`U115_RADIO` in `SX126xInterface.cpp`).
- Region US, LongFast, 902.375 MHz, SF11 / BW250 / CR4/5, sync 0x2B. TX power: config 30, clamped to 22 dBm.

## Symptoms (user)
1. Long messages arrive with CRC errors (`error=-7`), both directions.
2. App shows "failed to deliver to mesh", short messages too, mostly from the Zero.

## Findings (measured)
### A. Long-TX corruption is at chip level: frequency drift while the module heats up during TX
- Raw TX rig (`tools/u115_tx_test.py`, run on the Zero with meshzero stopped; packets hop 0, channel hash 0xFF; Heltec MN-Vert on COM7 logs RX/CRC) gives the same failures with no Meshtastic code involved:
  - Unit A: 30 B 5/5; 60 B 3/4; 90–237 B 0/23 at preamble 16; CR4/8 0/4; 144 B right after a 144 B "heat" send 4/4; preamble 100 3/4; preamble 160 (1.3 s) 4/4.
- Receiving is fine: long frames from a Heltec (TCXO) are received by both U115s.
- **The problem follows the unit.** After swapping the two U115s between ADV and Zero:
  - ADV→Zero 7/9, 0 CRC errors.
  - Zero→ADV fails.
- Weak unit B (now on the Zero), 144 B, Heltec receiving:
  - preamble 16 / 160 / 370 (3 s): 0/4 each
  - 490 (4 s): 3/7
  - 610 (5 s): 7/7
  - 237 B at 5 s: 6/7 (the 7th probably after capture end)
  - heat + preamble 16: 3/4
  - 30 B: 4/4
- No effect:
  - XTA/XTB trim 0x0911/0x0912. Writes only stick in STDBY_XOSC and reload to 0x12 at each XOSC start. Effective 0x12 = +11.8 ppm vs Heltec; 0x0A = +18.7; 0x2E = +69.9. Static offsets are received fine.
  - LDO vs DC-DC
  - ramp 3.4 ms
  - STDBY_XOSC before TX
  - bridge v3 vs v4 (CPU sleeps)
  - a cable-quiet host during TX
- **No TCXO on the Ra-07H (proved):**
  - `SetDio3AsTcxoCtrl` at 1.8 V and 3.3 V, with 5 ms and 100 ms waits: the chip accepts the command (status bytes 0xA2, no command error). The following Calibrate then fails with **XOSC_START (0x020)**, the chip refuses STDBY_XOSC, and TX never completes.
  - In crystal mode everything works.
  - Datasheet p12: U6 is a 2-pin crystal with C32/C33 to ground. Pin 5 DIO3 is "external TCXO supply", for an optional external part.
  - Heltec CubeCell's ASR650x core always enables a TCXO (`-DCONFIG_LORA_USE_TCXO`, 1.8 V, 2 ms). That's Heltec's own board, not this module.
  - The official M5 Cap LoRa-1262 has a TCXO (Meshtastic config: 1.8 V on the ADV, 3.0 V on the Zero). That's why it works.
- Datasheet: Ra-07H TX power is 20 dBm ±1, **max 21 dBm**, 107 mA. Meshtastic drives it at 22 dBm (same as the Cap; no per-board limit). The datasheet asks for a clean 3.3 V (LDO, or DC-DC ripple < 30 mV).
- **User constraint: TX power must not be lowered, not even for a test**, unless the user explicitly says so.

### B. "Failed to deliver" from the Zero: false channel-busy
- On the Zero's U115, CAD says "LoRa detected" with nobody on air. At detPeak 24 / detMin 10 / 4 symbols: 36/40.
  - detPeak 26 → 35, 28 → 15, 30 → 24, 32 → 5 out of 40.
  - 8 symbols: 35 (peak 24) and 20 (peak 26) out of 40.
  - 16 symbols: 28/40.
  - Tool: `tools/u115_cad_peak.py`.
- Earlier: the rate correlates with the Pi's CPU at 1000 MHz vs ≤ 900 MHz (93% vs 0–1%).
  - Not from the air: the Heltec saw no change.
  - Not through the antenna: still busy with antenna-switch power P6.1 off.
  - Not RX gain, DIO2 or bridge version.
  - Suspected entry: the Grove supply or UART link.
  - **The user rejected any CPU-frequency cap or Zero OS change.**
- Effect: `canSendImmediately()` holds on false PREAMBLE_DETECTED, and the pre-TX CAD keeps saying busy (each busy also hands off to an RX window of max-packet length). The Zero logged "Can not send yet, busyRx (4 deferred in 200212 ms)", and its own packets waited about 5 min in the TX queue.

## Code changes (uncommitted)
- `src/mesh/SX126xInterface.{h,cpp}`: **warm-up preamble** for U115.
  - `txPreambleFor(numbytes)`: if the data airtime is > `U115_WARM_TX_MS` (500 ms), the preamble becomes `U115_WARMUP_MS / symbolMs + 1` symbols. **`U115_WARMUP_MS` = 6000** (5 s proven on the weak unit, plus margin).
  - `launchTransmit()` (declared on all builds now) programs it via `setLoraPreamble()` (only sends the command when the value changes). `startRxCommand()` restores the normal 16.
  - `getPacketTime(pl, received=false)` adds `txPreambleExtraMs()`.
  - Portduino prestaged packetParams use the warm-up value.
  - Cost: about 7 s on air per long packet. The user finds this unacceptable as a final design.
- Same files: `useRegulatorLDO = false` for U115 (DC-DC, as CubeCell). In `handleSoftwareLoraIrqPoll` (ADV only), the full IRQ poll runs every 1000 ms during TX instead of 20 ms.
- `src/mesh/RadioLibInterface.cpp`: **false-busy override** (U115 only, file-static `u115CadBusySince`).
  - In `TRANSMIT_DELAY_COMPLETED`: when `isChannelActive()` says busy and no frame has been received since the first busy for longer than `maxRxFrameMsec()`, log `U115: channel check said busy ... sending`, set `cadHandedToRx=false`, call `setStandby()`, and treat the channel as clear.
  - Reset on any received frame (in `handleReceiveInterrupt`, after the frameless-exit cases) and whenever a check says clear.
  - **Not tested yet.**
- Tools added or changed:
  - `tools/u115_tx_test.py` (configs, gap arg, 10 s timeout)
  - `tools/u115_rssi_scan_lib.py` (env `U115_TCXO=<code>`)
  - `tools/u115_cad_peak.py`
  - scratchpad `tcxo_ack.py`

## Device state now
- ADV: flashed 14:53 with both fixes (warm-up 6000 + false-busy override); U115 unit A.
- Zero: running the old binary (warm-up 1300 ms, no busy override); debug logging on (restore `/etc/meshzero/radio.yaml.bak-info` later); U115 unit B.
  - New build in progress: `wsl_rebuild.sh` with `RadioLibInterface.{h,cpp}` and `SX126xInterface.{h,cpp}`.
- Heltec MN-Vert on COM7 as reference receiver. It must not relay user traffic; test packets use hop 0 and channel hash 0xFF, or portnum 256 for mesh tests (no phone notification).

## Open questions for Codex
1. **A better fix than a 6 s preamble for crystal drift on long TX**, within the constraints: keep 22 dBm, no Zero OS changes, no hardware mods unless the user agrees. Ideas worth checking:
   - Can the SX1262 or the receivers tolerate drift better on the receive side? LDRO can't be changed unilaterally, since it must match on both ends.
   - Is a short pre-heat (e.g. TX of a throwaway frame or CW right before) better than a long preamble? The rig showed heat + 16 at 3/4.
   - Is anything in our host or bridge sequence making the drift worse than the module's maker intended? For example the PA config `0x04, 0x07, 0x00, 0x01`, OCP 140 mA, the `0x08D8` clamp fix, or the `0x0889` fix.
2. **Review the false-busy override** in `RadioLibInterface.cpp` for correctness: CAD→RX handoff state, Portduino prestage paths (`stageTxInRx`, `prestagedLen`), the readout task's captured-frame path, and whether `maxRxFrameMsec()` is the right window.
3. **Root cause of the Zero's false CAD/preamble detections** that a host or bridge change could fix. Example: does UART traffic on the Grove link during CAD inject into the SX1262? A "cable silent during CAD" test didn't help.
4. **Bridge hardening** (from the earlier review, `docs/codex-u115-tx-report-2.md`): report SPI timeout as an error; 40 ms reset pulse and release RESET to hi-z as CubeCell does.

## Rules from the user (don't break them)
- Never lower TX power.
- Never write chip-protection byte 0x90600000.
- No Zero OS or CPU changes.
- No distance or placement explanations.
- U115 only; don't suggest the Cap.
- Commit or push only when asked.
- Suspect host or bridge code before hardware; hardware claims need measurements.
