# AirPods / Beats 1% Battery Support via AAP

## Why BLE broadcasts are limited to 10% steps

Without a Windows Bluetooth L2CAP bridge driver, AirPods / Beats battery readings come from passive BLE "proximity pairing" advertisements (company `0x004C`, type `0x07`). In the unencrypted portion of that payload, Apple encodes each battery level as a 4-bit nibble (`0..10`), which only represents `0%`, `10%`, `20%`, ..., `100%`. The trailing encrypted payload cannot be decrypted without the iCloud key material, and paired AirPods advertise as non-connectable (`ADV_SCAN_IND`), so GATT Battery Service queries are unavailable.

By contrast, when connected over Bluetooth Classic, AirPods expose Apple's **AAP (Apple Accessory Protocol)** service (`{74EC2172-0BAD-4D01-8F77-997B2BE0722A}`) on **L2CAP PSM `0x1001`**, which reports **true 1%-granular** battery percentages (`0..100`) and charging states (`0x01` charging, `0x02` discharging, `0x05` charging in case) for single-battery headsets (AirPods Max, Beats) as well as left, right, and charging case components.

## Dual-transport behavior in BatteryHub

1. **AAP (1% precision)**: When an AirPods / Beats device is connected over Bluetooth Classic and a compatible Windows AAP kernel bridge driver is installed, BatteryHub opens an AAP session, completes the read-only notification handshake (`HANDSHAKE` -> `HOST_CAPABILITIES` -> `REQUEST_NOTIFICATIONS`), and consumes `0x04` battery notifications at 1% granularity.
2. **BLE fallback (10% steps)**: When no AAP driver is installed, the device is not connected over Classic Bluetooth (for example charging on the desk), or another app holds exclusive access to the driver handle, BatteryHub automatically falls back to BLE proximity broadcasts using a 10-second active RSSI competition window.

## Supported Windows AAP drivers

BatteryHub automatically detects and supports three Windows AAP driver interfaces:

1. **`NTPodsAAP`** (recommended open-source driver from `arctumn/ntpods`, device interface `{C0FFEE00-1337-4A5B-9E6F-A1B2C3D4E5F6}`, binds to `BTHENUM\{74ec2172-0bad-4d01-8f77-997b2be0722a}`).
2. **`PodPilotAap`** (open-source driver from `kylianxdev/Earlo`, device interface `{1E9FA4AC-DD4C-4D83-A2A0-A5767809CCBA}`).
3. **`MagicAAP`** (MagicPods / `aap-head-tracker` driver interface `{9EEC98BB-3C54-45D4-A843-7900C4635E08}`).

> **Note on exclusive access:** `NTPodsAAP` uses an exclusive device object (`WdfDeviceInitSetExclusive`). Do not run the NTPods daemon and BatteryHub's AAP reader simultaneously against the same device; if another client already holds the handle, BatteryHub backs off and falls back to BLE.

## Installing `NTPodsAAP` on Windows

Because Windows does not allow user-mode apps to open Classic Bluetooth L2CAP channels (`WSAENETDOWN`), an unsigned/test-signed KMDF driver is required. Currently `NTPodsAAP` is not WHQL-signed by Microsoft, so choose one of two boot options:

### Option A: One-time test boot (keeps Secure Boot enabled)

1. Hold **Shift** and click **Restart** in Windows -> **Troubleshoot** -> **Advanced options** -> **Startup Settings** -> **Restart**.
2. Press **F7** ("Disable driver signature enforcement") on the Startup Settings screen.
3. Download the `NTPodsAAP` prebuilt driver package (`NTPodsAAP.sys`, `NTPodsAAP.inf`, `ntpodsaap.cat`) from `arctumn/ntpods` (`windows/drivers/aap/prebuilt`) and install it from an Administrator PowerShell using its `install.ps1` (or sign with a trusted local test certificate and run `pnputil /add-driver NTPodsAAP.inf /install`).
4. Note that after a normal reboot without F7, Windows will stop loading the test-signed driver until the next F7 boot.

### Option B: Permanent Test Signing mode

> **Warning:** Enabling `testsigning` requires disabling **Secure Boot** in UEFI/BIOS, lowers OS driver-signature enforcement, and conflicts with kernel-level game anti-cheat systems (such as Riot Vanguard or FACEIT). Back up your BitLocker recovery key before changing Secure Boot or BCD settings.

1. Disable **Secure Boot** in UEFI/BIOS.
2. In an Administrator Command Prompt / PowerShell, run:
   ```cmd
   bcdedit /set testsigning on
   ```
   Reboot Windows.
3. Install `NTPodsAAP` with `install.ps1 -PackageDir <path-to-prebuilt>` as Administrator (requires `signtool.exe` and `inf2cat.exe` from the Windows SDK).

## Rollback / Uninstall

1. Find and delete the driver package in an Administrator PowerShell:
   ```powershell
   pnputil /enum-drivers
   # Locate the published oemXX.inf whose Original Name is NTPodsAAP.inf, then:
   pnputil /delete-driver oemXX.inf /uninstall /force
   ```
2. If you enabled permanent test signing, turn it off and re-enable Secure Boot in UEFI:
   ```cmd
   bcdedit /set testsigning off
   ```

## Verification

Run the built binary from PowerShell or Command Prompt:

- `BatteryHub.exe --list` — prints current battery percentage (1% when AAP is active; `detail` includes `AAP` or `L ... · AAP`) and falls back to BLE when AAP is unavailable.
- `BatteryHub.exe --dump` — prints paired Apple audio devices, detected AAP bridge driver interfaces, active AAP session status, and cached BLE `0x07` adverts.
