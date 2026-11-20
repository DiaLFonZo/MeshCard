# U115 long-TX CRC investigation

Date: 2026-11-20 ET. Scope: source inspection only; no device access, flashing, commits, pushes, or OS changes.

## Executive finding

The leading software defect is the U115-specific regulator selection. Both U115 hosts compile the same `SX126xInterface.cpp` branch, which passes `useRegulatorLDO=true` to RadioLib. Its comment says the Heltec CubeCell driver never enables DC-DC. The installed CubeCell 1.4.0 `LoraWan102/src/radio/radio.c` contradicts that: `RadioInit()` calls `SX126xSetRegulatorMode(USE_DCDC)` at line 501. RadioLib's pinned `SX126x::modSetup()` applies the requested LDO/DC-DC mode. This is a confirmed configuration difference and a plausible cause of a failure that develops during a long 22 dBm transmission.

**Causality is not yet proved.** The provided TX_DONE, receiver CRC, and SNR observations establish that a full-length transmission occurs and is not decoded correctly. They do not directly measure the U115 output envelope, supply voltage, carrier drift, or on-air payload bytes. The proposed A/B test below is needed before calling the regulator mismatch the demonstrated root cause. No hardware failure is asserted.

## Evidence and paths

- Both builds reach the U115 radio branch: `meshtastic-firmware/src/mesh/SX126xInterface.cpp:19-22` defines `U115_RADIO` for `PINEDIO_U115` or `U115_BRIDGE_ESP32`. The ADV environment defines the latter in `variants/esp32s3/m5stack_cardputer_adv/platformio.ini:25-40`; the Zero environment defines the former in `variants/native/portduino/platformio.ini:558-563`.
- `SX126xInterface.cpp:140-148` selects LDO and calls `lora.begin(..., useRegulatorLDO)`. Pinned RadioLib `SX126x.cpp:1393-1398` maps that choice to `setRegulatorLDO()` or `setRegulatorDCDC()`.
- CubeCell 1.4.0 `libraries/LoraWan102/src/radio/radio.c:491-505` calls `SX126xInit`, then `SX126xSetRegulatorMode(USE_DCDC)`. Its `cores/asr650x/device/sx126x/sx126x.h:246-248` defines LDO as 0 and DC-DC as 1. This is the actual reference function inspected; the source comparison does not establish what any particular stock U115 firmware image programmed.
- The bridge implements checked raw UART framing and one NSS-low SPI transfer per XFER: `firmware/u115_bridge/u115_bridge.ino:68-79,103-110,141-159`. ADV relays it at `meshtastic-firmware/src/platform/esp32/U115LockingArduinoHal.cpp:281-298`; Zero relays it at `src/platform/portduino/libpinedio_u115.c:432-445`. The bridge drives P6.1 high at startup (`bridge_board.c:120-127`).
- The common TX path calls `beginSending()` and `launchTransmit()` at `RadioLibInterface.cpp:1548-1588`. The Zero-specific prestaging path is in `SX126xInterface.cpp:718-729,805-873,897-927,962-1011`. It is guarded by `ARCH_PORTDUINO` and thus cannot by itself explain ADV failure.
- The reported Zero trace shows `SetTx(0x83, timeout 0)`, full airtime and TX_DONE, correct explicit packet parameters and errata writes, with only about two GetIrqStatus commands during TX. This disfavors early stop and mid-TX register mutation. The receiver's readable header plus CRC error shows synchronization and at least initial data were recoverable, but does not identify whether the later error is in stored bytes or the RF waveform.

## Ranked differences against the reference

| Rank | Difference and source locations | Assessment |
| --- | --- | --- |
| 1 | **Regulator:** U115 forces LDO (`SX126xInterface.cpp:140-148`); CubeCell selects DC-DC (`LoraWan102/src/radio/radio.c:500-504`). | Best common software hypothesis for both hosts and duration-dependent TX errors. The source comment justifying LDO is false for this reference. Requires A/B confirmation. |
| 2 | **Zero buffer placement and CAD handoff:** the fork can write TX bytes while RX runs, select a nonzero base, and omit RadioLib's full TX staging (`SX126xInterface.cpp:897-927,821-873`). CubeCell uses base 0/0 and writes TX at offset 0 (`radio.c:503`; `cores/asr650x/device/sx126x/sx126x.c:107`). | A possible additional Zero-specific long-frame fault, especially around a 256-byte buffer wrap or RX overlap. It cannot explain ADV's identical failure because the path is Portduino-only. No observed base/byte mismatch was supplied. |
| 3 | **Reference initialization and calibration sequence:** CubeCell initializes the chip, selects DC-DC, base 0/0 and 200 us ramp (`radio.c:491-505`). RadioLib configures the chip through its own `begin()`, applies its selected regulator mode and calibrates image for 902-928 MHz. The fork additionally has a later full-calibration recovery path (`SX126xInterface.cpp:1074-1108`). | No specific calibration mismatch is tied to the failures. RX of long stock packets works; image calibration chiefly concerns RX rejection. The U115 crystal selection is correct; DIO3 TCXO mode is known to fail. |
| 4 | **Bridge mechanics:** CubeCell accesses SPI and DIO1 directly; U115 sends complete SPI transactions over checked 115200-baud UART frames (`u115_bridge.ino:103-110,141-159`) with host-specific HALs. V4 rests the PSoC between edges (`bridge_board.c:77-103`); V3 polled continuously. | The same failure with sleeping V4 and busy-polling V3 weakens CPU activity as a cause. A corrupted bridge WriteBuffer would remain possible in principle, but no UART checksum or SPI write evidence points to it; a bridge fault would need byte-for-byte capture. |
| 5 | **RF switch power and IRQ handling:** the bridge drives P6.1 high (`bridge_board.c:120-124`); both builds configure DIO2 as RF switch (`SX126xInterface.cpp:204-216`, `bin/config.d/lora-m5stack-u115-bridge.yaml:17`). ADV polls software DIO1, Zero uses a bridge event. | Both are U115 integration differences, but full TX_DONE and intact short packets make an IRQ-only explanation poor. An antenna path problem is not established by these observations. |

### TX settings that do *not* substantiate a cause

- **PA configuration at 22 dBm:** the pinned RadioLib SX1262 optimized table's 22 dBm entry is `paDutyCycle=4, hpMax=7, paVal=22` (`SX1262.cpp:6-39,181-190`). CubeCell's default `gPaOptSetting=0` selects `SetPaConfig(04,07,00,01)` (`asr_board.c:52`; `sx126x.c:495-515`). Thus the high-power PA setting matches.
- **TX power and ramp:** the U115 clamp is 22 dBm (`SX126xInterface.cpp:133-148,323-332`); CubeCell caps at 22 (`sx126x.c:517-524`). CubeCell sets 200 us ramp (`asr_board.c:240-243`); the pinned RadioLib API documents 200 us as its default. No U115 ramp override is defined in the ADV variant or Zero configuration. TX power must remain 22 dBm for the test.
- **OCP:** the fork sets `currentLimit=140` mA (`SX126xInterface.h:42`; `SX126xInterface.cpp:200,305`). At 2.5 mA per unit this writes `0x38`, which CubeCell writes explicitly (`sx126x.c:525`). CubeCell's adjacent comment calls that 160 mA, but the register value is what matters. OCP is therefore not an identified difference.
- **PA clamp and packet errata:** pinned RadioLib `SX1262::begin()` calls `fixPaClamping()` and its implementation sets the documented clamp bits (`SX1262.cpp:44-63`; `SX126x.cpp:1241-1255`). The Zero trace already verified `0x0889=0x04` and `0x0736=0x0D`. No evidence supports changing them.
- **XTAL trims `0x0911/0x0912`:** CubeCell's ASR650x header defines `REG_XTA_TRIM=0x0911` (`sx126x.h:142`), but the inspected ASR650x driver and `LoraWan102` radio source contain no write to either trim address. The inspected U115 source contains none. There is no reference trim value to transplant, and no frequency-drift measurement proving trim is needed.
- **Image calibration:** CubeCell selects `E1 E9` above 900 MHz (`sx126x.c:352-381`); pinned RadioLib selects its 902-928 MHz image constants (`SX126x.cpp:1183-1206`). The fork calls `calibrateImage(getFreq())` again after its AGC recovery calibration (`SX126xInterface.cpp:1083-1108`). This is not presently an evidenced long-TX cause.
- **TX activity:** the bridge's `PINS` and SPI XFER commands do not autonomously write radio registers during transmission (`u115_bridge.ino:103-121`). The supplied trace's sparse GetIrqStatus traffic is read-only. Complete TX_DONE rules out a simple timeout or premature stop.

Pinned RadioLib sources inspected:
- `https://raw.githubusercontent.com/jgromes/RadioLib/5878f990bdb4c95c75494a21562c0c5a49659c6f/src/modules/SX126x/SX126x.cpp`
- `https://raw.githubusercontent.com/jgromes/RadioLib/5878f990bdb4c95c75494a21562c0c5a49659c6f/src/modules/SX126x/SX1262.cpp`
The build pins that revision in `meshtastic-firmware/platformio.ini:136-137`.

## Proposed fix and one discriminating test

Change only the U115 branch at `SX126xInterface.cpp:141-145` to select DC-DC (`useRegulatorLDO=false`), matching CubeCell. Keep 22 dBm, LongFast, frequency, packet settings, bridge firmware and receiver unchanged.

Run one controlled A/B on the ADV: with the existing firmware, send a fixed long packet (for example the same 93-byte on-air length) repeatedly to the same stock receiver and record the CRC-valid count and TX_DONE; then flash only the ADV build with this one regulator change and repeat the identical transmissions. Use enough repeats to separate deterministic failure from an occasional success (for example 20 each). The stock receiver is useful because the Zero-only prestaging code cannot influence its decoding. A transition from consistently bad to consistently CRC-valid with only this change would establish the regulator setting as the proximate software cause on ADV; it would strongly implicate the same common setting on Zero. If there is no change, do not call the regulator mismatch the cause. Next inspect an on-air capture or a byte-for-byte TX FIFO readback, especially Zero's staged buffer, before making a hardware claim.

No test was run by Codex; device flashing is reserved for Eric.

