# NuPhy WH80 (USB cable and Bluetooth)

Hardware tested with `19F5:A011`, firmware release `0116`, interface 1,
usage `0001:0000`, unnumbered 64-byte input/output payloads (65 bytes in Windows HID caps).

Protocol source: [NuPhyIO](https://drive.nuphy.io/), bundle `main.b3fb29e0.js`,
SDK `getBatteryState(0)`, `getDataPackage`, and `createCommand`.
Only `GetFunc` (`05`), offset `0020`, length `01` is requested. No initialization,
configuration, lighting or firmware commands are needed.

Request payload, zero-padded to 64 bytes:

```text
55 05 00 21 01 20 00 00
```

Actual reply payload:

```text
AA 05 00 05 01 20 00 00 E4 00 ...
```

Byte 3 is the low byte of the sum of bytes 4..63. Battery is byte 8:
`value & 7F` is percentage, `value & 80` is the SDK charging flag.
The native C++ driver returned **100%, charging** on the attached keyboard.
Charging transitions and low/full boundaries have not been hardware tested;
the official firmware notes describe WH80 readings in 10% increments.
Receiver operation is outside this driver's scope.

## Bluetooth

On Bluetooth the same keyboard uses HID ID `19F5:324A` (release `0129`) and
advertises a user-visible name such as `NuPhy WH80 - 1`. It exposes two keyboard
collections and a mouse collection. The generic HID heuristic prioritizes mice,
so add this exact Bluetooth ID to the existing keyboard overrides.

Battery reporting continues to use the Windows Bluetooth battery property.
After unplugging USB, Windows reported **95%** on the attached keyboard. An
uncached read of standard GATT Battery Level (`180F` / `2A19`) also returned
**95%** directly from the keyboard. This
is a separate device/transport reading from the wired 100% observation; do not
replace it with an assumed full value or rescale it. The reason for the differing
firmware readings is not established.

The driver checks the exact product/interface/report sizes, response header,
offset, length, checksum and percentage range. It uses a 400 ms response deadline
and ignores unrelated replies within that same deadline.

Regression test:

```text
g++ -std=c++17 tests/nuphy_driver_test.cpp src/drivers/nuphy.cpp -o build/nuphy_driver_test.exe
build/nuphy_driver_test.exe
```
