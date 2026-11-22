# GameSir Cyclone Pro battery support

## Why the controller showed as a full Xbox controller

The Cyclone Pro (model T4cp) plays through a 2.4 GHz dongle, `3537:1015`. The dongle presents itself
to Windows as a wired Xbox 360 controller (XUSB, `Xbox 360 Controller for Windows`). XInput answers
its battery query with "Full", so the XInput fallback listed it as `Xbox controller 1` at 100%
whatever the real charge was.

The dongle carries no battery information:
- its only HID collection is the XInput game controller (`HID\VID_3537&PID_1015&IG_00`)
- it has no vendor-defined collection like the Nova (`FF7A`) or the Cyclone 2 (`FFF0`)

## Where the level is

While it plays through the dongle, the pad also advertises over Bluetooth LE as
`GameSir-Cyclone Pro`, which is the link GameSir's phone app uses. Its "get base info" request
returns the battery:

| Step | Detail |
|---|---|
| Service | `0x8650` (`00008650-0000-1000-8000-00805f9b34fb`) |
| Characteristic | `0x865F` (notify, write without response) |
| Request | enable notifications, write `01 01` |
| Reply | 19-byte notification (some firmwares prefix it with `C9`, 20 bytes) |

Reply layout:

```text
[0..5] MAC  [6..7] hardware ver  [8..9] BT ver  [10..11] firmware ver  [12..17] build date  [18] battery
```

Byte 18 is the battery in percent (0-100), or `FF` while charging. No level is reported while it
charges, so the last known percentage is kept and shown with the charging bolt.

The pad also has service `0xFF10`, which is GameSir's firmware-update channel. The driver never opens
it. The pad has no standard Battery Service (`0x180F`), which is why Windows' Bluetooth page and
the Bluetooth fallback show no level for it.

Source: [GameSirBattery](https://github.com/bpbp-boop/GameSirBattery), where the protocol was
worked out from GameSir's Android app (`BluetoothLeService`, "get base info").

## How the driver works

`src/drivers/gamesir_ble.cpp`, called before the XInput fallback:

1. **Find the pad.** It looks up device interfaces of class `{00008650-0000-1000-8000-00805F9B34FB}`.
   Windows creates one for each GATT service of a paired BLE device. It only uses those whose parent
   `BTHLE\DEV_…` node is named `GameSir-Cyclone…`.
2. **Read the battery.** It opens that service with the Win32 GATT API (`bluetoothleapis.h`,
   `-lbluetoothapis`), subscribes to `0x865F` and writes `01 01`. It waits up to 3 seconds for the
   reply, then switches notifications back off.
3. **Keep the poll fast.** The BLE connection can take seconds, or time out while the pad is
   asleep, so the query runs on its own thread:
   - at most every 2 minutes after a good read, or every minute after a failed one
   - the poll reports the cached reading, and stops reporting it once it is 6 minutes old
   - `graceSec` then keeps it visible, dimmed, as asleep
   - only the first poll of a run waits for the thread, once, with an absolute 4-second deadline,
     so the level shows on start and in `--list`
4. **Hide the fake Xbox entry.** When the dongle is present, the driver claims `3537:1015`:
   - the Windows.Gaming.Input path now skips claimed vendor/product ids
   - the XInput path gets each slot's vendor/product id from `XInputGetCapabilitiesEx` (xinput1_4
     ordinal 108) and skips claimed ones
   - when that export is missing, XInput behaves as before

## Tested hardware

Windows 11, Cyclone Pro on its dongle (`3537:1015`), paired once over Bluetooth, on 2026-11-18:

```text
GameSir Cyclone Pro  [2.4 GHz receiver (battery over Bluetooth LE)]  100%   (gamesir-ble:A05A598D2224)
```

While the pad was asleep, the "Xbox controller 1 / Full" entry no longer appeared and nothing was
shown for the pad.

## Limitations

- **Pairing:** the pad must be paired with Windows once (Settings > Bluetooth & devices > Add device >
  Bluetooth > `GameSir-Cyclone Pro`). Unpaired pads have no GATT device interface. Pairing adds
  Bluetooth gamepad entries, but these send no input while the pad plays through the dongle.
- **Bluetooth required:** the PC needs Bluetooth LE.
- **One pad:** only one Cyclone Pro per PC is read.
- **Charging:** there is no percentage while charging.
