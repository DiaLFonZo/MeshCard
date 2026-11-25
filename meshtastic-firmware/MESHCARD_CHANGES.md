# MeshCard changes to Meshtastic firmware

This folder is a copy of [meshtastic/firmware](https://github.com/meshtastic/firmware)
(GPL-3.0, see `LICENSE`), modified for MeshCard. This notice is required by
GPL-3.0 section 5a.

- Base: commit `647831b251fb2349a8ce85d78490874ccb3bffac` (`develop`, 17 Nov 2026, firmware 2.8.3)
- Changed by: MeshCard (DiaLFonZo, with Claude), 18–20 Nov 2026 and later
- Purpose: drive an M5Stack U115 LoRa unit (through the MeshCard bridge firmware)
  on the Cardputer ADV and the Cardputer Zero (`meshtasticd`), and show the
  MeshCard screens on the Cardputer ADV.
- Not included: the submodules `protobufs` (`62b09edf`) and `meshtestic` (`dcac7e56`).

The exact changes are the difference between this folder and the base commit
above; the git history of this repository has them in detail.

## Files added

- `bin/config.d/lora-m5stack-u115-bridge.yaml`
- `src/meshcard/LocalUiAPI.cpp`
- `src/meshcard/LocalUiAPI.h`
- `src/platform/esp32/U115LockingArduinoHal.cpp`
- `src/platform/esp32/U115LockingArduinoHal.h`
- `src/platform/portduino/libpinedio_u115.c`
- `src/platform/portduino/u115/include/libpinedio-usb.h` (based on `src/platform/portduino/windows/include/libpinedio-usb.h`)
- `variants/esp32s3/m5stack_cardputer_adv/meshcard/lv_conf.h`

## Files modified

- `platformio.ini`
- `src/AudioThread.h`
- `src/graphics/TFTColorRegions.h`
- `src/graphics/TFTDisplay.cpp`
- `src/graphics/draw/MenuHandler.cpp`
- `src/graphics/tftSetup.cpp`
- `src/input/InputBroker.cpp`
- `src/main.cpp`
- `src/mesh/MeshService.cpp`
- `src/mesh/MeshService.h`
- `src/mesh/PhoneAPI.cpp`
- `src/mesh/PhoneAPI.h`
- `src/mesh/RadioInterface.cpp`
- `src/mesh/RadioLibInterface.cpp`
- `src/mesh/SX126xInterface.cpp`
- `src/mesh/SX126xInterface.h`
- `src/mesh/api/ServerAPI.cpp`
- `src/mesh/api/ServerAPI.h`
- `src/modules/AdminModule.cpp`
- `src/modules/ExternalNotificationModule.cpp`
- `src/modules/RangeTestModule.cpp`
- `variants/esp32s3/m5stack_cardputer_adv/platformio.ini`
- `variants/esp32s3/m5stack_cardputer_adv/variant.h`
- `variants/native/portduino/platformio.ini`

No upstream file was removed.
