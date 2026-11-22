# Razer HyperSpeed Multi-Device battery readings

One receiver can expose the mouse and keyboard as a single USB product. BatteryHub asks the receiver which devices are paired and reads each active device separately. The mouse still updates while the keyboard sleeps, and vice versa.

## Receiver protocol

The Razer driver checks mouse and keyboard devices with a 91-byte HID feature report, including report ID zero. The read-only slot query uses class `00`, command `BF`, transaction `1F`, and a payload size of `31` hex. The first argument is `10` hex and the remaining arguments are zero.

A successful two-slot reply begins with `02`, followed by two three-byte entries. Each entry contains an active flag (zero or one) and a big-endian USB product ID. The first entry is the mouse, addressed by transaction `1F`. The second is the keyboard, addressed by transaction `9F`. An inactive entry can retain its product ID. [OpenRazer capture and discussion](https://github.com/openrazer/openrazer/issues/1892)

| Query | Class | Command | Result |
| --- | --- | --- | --- |
| Battery | `07` | `80` | Argument 1 on a 0–255 scale |
| Charging | `07` | `84` | Argument 1 equals one while charging |

BatteryHub keys each reading by the device's reported product ID. If the device also answers on its own receiver, the two readings share a single entry. The displayed name comes from any available USB product string or a known model name. Other devices are shown as a Razer mouse or keyboard with the product ID.

The driver falls back to normal single-device polling when the slot list is unavailable, invalid, or has a vacant slot. These notes cover receivers with the two-slot HyperSpeed report above. The [OpenRazer HyperFlux V2 work](https://github.com/openrazer/openrazer/pull/2800) documents a receiver that uses separate HID interfaces.

## Verification

- Hardware tested on Windows with a BlackWidow V4 Tenkeyless HyperSpeed receiver (`1532:02D5`) and a Viper V3 Pro SE (`1532:00DF`) paired to it. The mouse's own receiver was unplugged.
- With the keyboard asleep, the receiver returned `02 01 00 DF 00 02 D5` at the start of the slot reply. BatteryHub reported the mouse at 37% on 15 consecutive polls.
- With both devices awake, the 1.1.2 installer payload reported the mouse at 37% and the keyboard at 94% in all ten polls. An earlier 15-poll run returned the mouse each time and missed one keyboard reply.
- The public capture shows the same slot format with a Naga V2 Pro receiver (`1532:00A8`) paired with a DeathStalker V2 Pro Tenkeyless (`1532:0296`). [OpenRazer hardware capture](https://github.com/openrazer/openrazer/issues/1892)
