# How to build and install MeshCard

MeshCard runs on two devices, both using an **M5Stack U115** LoRa unit as the radio:

| Device | What runs | Screen |
|---|---|---|
| **Cardputer ADV** (ESP32-S3) | Meshtastic firmware with MeshCard built in | 240 × 135 |
| **Cardputer Zero** (Linux) | MeshZero radio service + MeshCard app | 320 × 170 |

The U115 does not speak Meshtastic by itself. It gets a small **bridge firmware** that lets the Cardputer drive its radio chip over the Grove cable.

---

## 1. Prepare the U115 (once per unit)

The U115's chip (ASR6501) is flashed over SWD with a Raspberry Pi Pico running *debugprobe*.

**Wiring (Pico → U115):**

| Pico | U115 |
|---|---|
| GP2 | SWC |
| GP3 | SWD |
| GP1 | RST |
| GND | GND |
| 3V3 | 3V pad |

Do **not** power the U115 from the Grove red 5 V wire while flashing; the chip restart-loops.
For the serial test only, also connect the Grove yellow/white wires to GP4/GP5 (chip RX → GP4).

**Flash:**

```
python tools/psoc4_acquire.py
python tools/flash_robust.py saved/2026-11-18-working/bridge-direct.hex
```

Run `flash_robust.py` right after `ACQUIRED` is printed. It checks every block and can simply be run again if it stops.
Check it with `python tools/bridge_test.py COM6` (needs the Grove serial wires).

The bridge source is in `firmware/u115_bridge/`. It uses direct register access, not the Heltec CubeCell library's pin functions.

---

## 2. Build tools (PC, once)

Everything builds in **WSL Debian** on Windows:

```
wsl --install -d Debian
# inside Debian, as root:
apt install python3-venv rsync cmake g++ g++-aarch64-linux-gnu
python3 -m venv /root/pio-venv && /root/pio-venv/bin/pip install platformio
```

Builds run in a Linux-side copy (`/root/fw-adv`, `/root/meshcard`) because building straight from `C:\` is very slow. Output goes to `out/` in this repo (not saved in git).

---

## 3. Cardputer ADV

**Plug in:** U115 on the ADV's Grove port. The firmware finds which wire is TX/RX by itself.

**Build** (first time ~20 min, then a few minutes):

```
wsl -d Debian -u root -- bash "/mnt/c/00 - GitHub/MeshCard/tools/adv_build.sh"
```

**Install** (USB, ADV on COM8):

```
.\tools\adv_flash.ps1            # update: keeps settings, nodes and messages
.\tools\adv_flash.ps1 -Factory   # first install or full wipe
```

If the screen stays black after flashing, switch the ADV off and on.
The web flasher (flasher.meshtastic.org → custom firmware) also works with the `.factory.bin`, but the USB tool above is more reliable.

**First start:** set the region and preset from the phone app (e.g. US + Long Range - Fast). Picking the region on the device itself can choose a different preset.

**Logs and crashes:**

```
python tools/adv_listen.py COM8 60      # listen without restarting the ADV
python tools/adv_log.py COM8 20         # may restart the ADV when opened
bash tools/adv_crash_where.sh 0x4202b426 ...   # (WSL) crash address → file:line
```

After a crash, the restart note shows `crash at 0x...`. Paste those addresses into `adv_crash_where.sh`.

**Self-test build:** `adv_build.sh selftest` walks every screen by itself and logs free memory.

---

## 4. Cardputer Zero

**Plug in:** U115 on the Zero's Grove port (the Zero switches Grove 5 V on when the radio starts).

**Radio service:** MeshZero's `meshzerod` with a `meshtasticd` build that drives the U115.
Install files are in `zero/meshzero/` (`install.sh`, `radio.yaml`); the U115 `meshtasticd` binary is in `saved/2026-11-18-working/`.

**Build the MeshCard app** (~2 min):

```
wsl -d Debian -u root -- bash "/mnt/c/00 - GitHub/MeshCard/tools/zero_build.sh"
```

**Copy to the Zero** (over Wi-Fi/SSH; set `ZERO_HOST`, `ZERO_USER`, `ZERO_PASS`):

```
python tools/zero_put.py out/zero/meshcard-arm64 /home/pi/meshcard/meshcard.new
python tools/zero_ssh.py "mv -f /home/pi/meshcard/meshcard.new /home/pi/meshcard/meshcard"
```

Launcher entry, start script and alert service: `zero/meshcard/`.
The phone app connects to the Zero over the network (Linux meshtasticd has no Bluetooth).

---

## 5. PC preview (screenshots without a device)

```
wsl -d Debian -u root -- bash "/mnt/c/00 - GitHub/MeshCard/tools/preview_build.sh" <shots list> adv
```

Takes screenshots of MeshCard at the ADV size (`adv`) or the Zero size (`zero`) into `out/shots-<size>/`.
