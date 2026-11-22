# GameSir Tarantula Pro / NovaPro — SAPJoy device info frame

GameSir ships two independent vendor libraries: an older `sapjoyuniapis_x86` build that hard-checks
usage page **`0xFFA0`** and serves the Tarantula Pro / NovaPro (`T4NPro`, sold as Super Nova), and a
newer `sapjoyuniapis_t4n2` build that hard-checks **`0xFF7A`** and serves Nova 2 Lite, NOVA VISION,
T3XS, Cyclone 3 and N3S. The `0xFF7A` path is already covered by `gamesir.cpp`; this document and
`gamesir_sapjoy.cpp` cover the `0xFFA0` one.

## Collection

| Direction | Report id | Length |
|---|---|---|
| Output (host → pad) | `0x07` | 32 bytes |
| Input (pad → host) | `0x06` | 64 bytes |
| Feature | — | length 0: no feature channel on this collection |

## Frame

```
[len] [cmd] [sub] [payload ...]        no checksum
```

`len` sits first and **counts itself**, excluding the report id. The frame is zero padded to the
collection's report length.

### GetDeviceInfo — the only frame this driver sends

```
07 03 0A 01 00 00 ... 00          32 bytes
│  │  │  └─ sub  = 0x01
│  │  └──── cmd  = 0x0A
│  └─────── len  = 3
└────────── output report id
```

Reply (64 bytes):

| Offset | Field |
|---|---|
| `[0]` | input report id `0x06` |
| `[1]` | len (`0x0E` = 14, counts itself) |
| `[2]` `[3]` | cmd `0x0A`, sub `0x01` |
| `[4]` | device id |
| `[5..6]` | firmware |
| `[7..12]` | MAC |
| **`[13]`** | **battery** |
| `[14]` | device type |
| `[15..63]` | zero padding |

Filter on all three header fields (`[0] == 0x06 && [2] == 0x0A && [3] == 0x01`): the same collection
also carries gamepad state frames.

## Battery semantics

The vendor app (its `main.js`) applies exactly two rules:

```js
chargeState: 255 == parseInt(battery)                    // charging / unknown, no percentage
battery:     Math.round(100 < battery ? 100 : battery)   // clamp above 100
```

So `0..100` is the percentage, `255` means charging (keep the last known value), anything above 100
is clamped to 100. There is no interpolation, filtering or voltage fitting — the granularity is the
firmware's.

## Status

- Verified on a **NovaPro (Super Nova / T4NPro)** on its receiver (`3537:103E`): reported 96 % while
  the vendor UI showed 96 %, and 255 while charging.
- `3537:100E` (NovaPro on a cable) and `3537:1050` (Tarantula Pro) answer the same frame in the
  vendor client; not tested on those models.
- `3537:103E` is shared by the Tarantula Pro and the NovaPro, so the product string is used for the
  display name.

## Link identity

Matching nonzero, non-FF MAC bytes identify the same controller across cable and receiver;
a valid cable reading takes precedence. Different addresses remain separate. Missing addresses
fall back to the product ID, so links with no concrete address are not combined across IDs.
The input frame must contain its declared length and at least all device-info fields.
