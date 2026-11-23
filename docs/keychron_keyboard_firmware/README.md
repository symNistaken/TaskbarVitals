# Keychron keyboard battery over the 2.4 GHz receiver

Keychron's stock firmware only reports the keyboard's battery over Bluetooth. With the small QMK change in
[`k5_max_battery.patch`](k5_max_battery.patch), the keyboard also answers a battery query (`A4`) through the
Keychron Link receiver and over the USB cable, and Taskbar Vitals shows its level, voltage and charging state.

Tested on the **Keychron K5 Max ANSI RGB** (`3434:0A50`) with the Keychron Link receiver (`3434:D030`).

## What the patch changes

Based on [Keychron/qmk_firmware](https://github.com/Keychron/qmk_firmware) branch `2025q3` (commit `57e682c`):

- adds the `KC_GET_BATTERY` (`0xA4`) raw HID command from [ldkv/qmk_firmware#1](https://github.com/ldkv/qmk_firmware/pull/1):
  reply `[A4][percent][mV lo][mV hi][charging: 0 battery / 1 charging / 2 full][transport][model id]`;
- passes raw HID requests that arrive over the wireless link to the same handler;
- in `keyboards/keychron/k5_max/config.h`: `#define WILRESS_RAW_ENABLE` (spelled this way in Keychron's code) and
  `#define KC_BATTERY_MODEL_ID 0`.

Nothing else changes: keymaps, lighting and Keychron Launcher keep working.

## Option 1: flash the ready-made K5 Max firmware

A build for the **K5 Max ANSI RGB only** is attached to
[this release](https://github.com/jtentis/Wireless-Device-Battery-Monitor/releases/tag/v1.0.8-k5.1)
(`k5_max_ansi_rgb_battery.bin`, SHA256 `5971a3429ca38805e1dc40b97a9fc9faf0ae1be59e673dc03acbb74eeb277abd`).
Do not flash it on another model or layout.

## Option 2: build it yourself

Needs Git and Docker (or a working [QMK setup](https://docs.qmk.fm/newbs_getting_started)).

```sh
git clone --recurse-submodules -b 2025q3 https://github.com/Keychron/qmk_firmware.git
cd qmk_firmware
git checkout 57e682c && git submodule update --init --recursive
git apply /path/to/k5_max_battery.patch
docker run --rm -v "$PWD":/qmk_firmware -w /qmk_firmware ghcr.io/qmk/qmk_cli \
    make keychron/k5_max/ansi/rgb:keychron
```

The result is `keychron_k5_max_ansi_rgb_keychron.bin` in the repository root. On Windows, a bind-mounted
checkout is very slow in Docker; cloning inside the container (or WSL) is much faster.

**Other Keychron wireless keyboards** (Q/V/K Max and other models on the `2025q3` tree with `LK_WIRELESS_ENABLE`):
the changes under `keyboards/keychron/common/` apply as they are. Add the two `#define` lines to your keyboard's
`config.h` instead of the K5 Max one, and build your own target, e.g. `make keychron/<model>/<layout>/<variant>:keychron`.
The app currently names a K5 Max (`0A50`–`0A58`) by model and any other one "Keychron keyboard".

## Flashing

1. Set the mode switch to **Cable** and unplug the USB cable.
2. Hold **Esc** while plugging the cable in. The keyboard enters the STM32 bootloader.
3. Flash the `.bin` with [QMK Toolbox](https://github.com/qmk/qmk_toolbox/releases) (or `dfu-util`).
4. Switch back to **2.4 GHz** and check with `TaskbarVitals.exe --list`:
   `Keychron K5 Max  [2.4 GHz receiver]  52%, 3743 mV`.

Updating the keyboard from Keychron Launcher replaces this firmware and removes battery reporting. To undo it,
flash Keychron's official firmware the same way.

## License

QMK is GPL-2.0-or-later; the patch is under the same license. Anyone who receives a binary built from it must
also be able to get this source change.
