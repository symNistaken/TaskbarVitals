# Contributing to Taskbar Vitals

Thank you for helping improve **Taskbar Vitals**! Whether you are confirming hardware support, writing a read-only HID driver, improving system vitals telemetry, updating translations, or refining the experimental Linux backend, this guide covers how the project is structured and verified.

---

## Project Architecture

Taskbar Vitals is written in native **C++17 / Win32** (with an experimental **GTK4 / Cairo** Linux port), keeping runtime overhead and binary size small:

| Path | Purpose |
|---|---|
| `src/main.cpp`, `src/app_settings.cpp`, `src/notifications.cpp`, `src/app_commands.cpp`, `src/panel.cpp` | Win32 taskbar strip(s), tray lifecycle, settings registry/JSON port, alert dispatch, menu/panel commands, DPI-aware settings panel |
| `src/drivers/` | Vendor-specific, strictly read-only peripheral battery drivers (`PollContext`) |
| `src/sysmon.cpp`, `src/gpumon.cpp` | Live CPU, RAM, Energy Meter wattage, DXGI/PDH/WDDM/NVML GPU metrics |
| `src/cputemp.h`, `src/cputemp_client.*`, `helper/cputemp_helper.cpp` | Elevated CPU package temperature helper (`TaskbarVitalsTemp.exe`) embedding PawnIO `IntelMSR.bin` and `AMDFamily17.bin` |
| `src/update_core.cpp`, `src/update_win.cpp`, `src/update_signature.cpp` | Windows GitHub updater with ECDSA P-256 `SHA256SUMS.txt.sig` verification |
| `lang/` | UTF-8 localization files (compiled into application resources) |
| `linux/`, `src/linux/` | Experimental Linux `hidraw` + StatusNotifierItem backend |
| `tests/` | Standalone unit and driver protocol regression tests |

**Third-party components:**
- **HeadsetControl 4.3.0** (`headsetcontrol.exe`): Bundled by the Windows packaging script (`installer/build-installer.ps1`) with SHA-256 verification and GPL-3.0 corresponding source tarball.
- **PawnIO modules** (`res/pawnio/IntelMSR.bin`, `res/pawnio/AMDFamily17.bin`, LGPL-2.1+): Embedded unchanged into `TaskbarVitalsTemp.exe` for read-only Intel/AMD CPU package temperature reads via the separately installed PawnIO driver.

---

## Build & Test Environment

### Windows (MSYS2 MINGW64 / Command Prompt)

> **Important:** The [Makefile](Makefile) uses POSIX shell commands (`mkdir -p`, `printf`, `rm -rf`). Run `make` from an **MSYS2 MINGW64** (or UCRT64) bash shell, or use `build.bat` in plain Windows Command Prompt when `g++` and `windres` are on `PATH`. PowerShell's `make` without MSYS2 will fail.

Install build tools in MSYS2 MINGW64:

```sh
pacman -S mingw-w64-x86_64-gcc make
```

Common build and test commands:

| Goal | MSYS2 MINGW64 | Command Prompt |
|---|---|---|
| Development build (`TaskbarVitals.exe` + `TaskbarVitalsTemp.exe`) | `make -j12` | `build.bat` |
| Versioned test-channel build | `make -j12 VERSION=2.0.0 CHANNEL=test` | `build.bat 2.0.0 test` |
| Full regression suite + updater + signature tests | `make test VERSION=2.0.0 CHANNEL=test` | — |
| Individual updater suites | `make test-update-core` / `make test-update-win` / `make test-update-signature` | — |
| Clean build outputs | `make clean` | — |
| Package installer & portable ZIP (requires Inno Setup 6/7) | `powershell -NoProfile -File installer\build-installer.ps1 -Version 2.0.0` | same |

*Note:* When linking `TaskbarVitals.exe` or `test-update-win`, `ld` may print a harmless `Warning: .rsrc merge failure: multiple non-default manifests` message; this is expected with the current resource setup.

### Experimental Linux Build

Requires `g++`, `make`, `gtk4`, and `cairo` development headers (`libgtk-4-dev libcairo2-dev` on Debian/Ubuntu; `gtk4-devel cairo-devel` on Fedora):

```sh
./build-linux.sh          # builds ./taskbar-vitals
./build-linux.sh test     # runs Linux HID parser tests
./build-linux.sh clean
```

---

## Reporting Hardware Status

Most peripheral drivers are built from protocol research and cannot all be verified on physical hardware by the maintainer. Use the [Device Support template](.github/ISSUE_TEMPLATE/device-request.yml) or run `TaskbarVitals.exe --report` and include:

- Exact model name and connection mode (2.4 GHz receiver, USB cable, or Bluetooth)
- Output of `TaskbarVitals.exe --list`
- Output of `TaskbarVitals.exe --dump`
- Battery level and charging state shown by the manufacturer's software

---

## Writing a Peripheral Battery Driver

1. Run `TaskbarVitals.exe --dump` to identify the device's `VID:PID`, vendor usage pages (`FFxx`), and report lengths.
2. Research the read-only battery query (e.g., open-source drivers like HeadsetControl, OpenRazer, Solaar, rivalcfg, vendor web configurators, or Wireshark + USBPcap captures).
3. Add the model/PID to an existing vendor driver in `src/drivers/`, or create `src/drivers/<vendor>.cpp`:

   ```cpp
   #include "drivers.h"

   void PollAcme(PollContext& ctx) {
       for (auto [vid, pid] : ctx.Products(0x1234)) {  // unclaimed products of this vendor
           for (const HidColl* c : ctx.Find(vid, pid, [](const HidColl& c) { return c.page() == 0xFF00; })) {
               HidDev d;
               if (!d.Open(*c)) continue;
               std::vector<BYTE> r;
               if (!d.Write({ 0x01, 0x02 }) || d.Read(r, 500) < 3) continue;  // hidapi-style buffers
               DeviceStatus s;
               s.key = ctx.StableKey(L"acme:" + Hex4(pid));
               s.name = HidProduct(*c);
               s.kind = DeviceKind::Mouse;
               s.via = L"2.4 GHz receiver";
               s.percent = Clamp100(r[2]);
               ctx.Add(s, vid, pid);
               break;
           }
       }
   }
   ```

4. Declare the function in `src/drivers/drivers.h` and invoke it in `PollAllDevices()` in `src/devices.cpp` before the generic fallbacks. (`Makefile` and `build.bat` automatically pick up new `src/*.cpp` and `src/drivers/*.cpp` files.)
5. Add a unit test under `tests/` and **register its compile/run step in `Makefile` under the `test:` target** (tests are not auto-discovered).
6. Update the alphabetical vendor table in `README.md` under **Supported Hardware**.

### Strict Driver Rules

- **Strictly read-only:** Never write configuration, DPI, polling rate, lighting, or firmware commands.
- **Exact device matching:** Match by `VID:PID` or a protocol-unique vendor HID collection before talking to a device.
- **Bounded timeouts:** Missing or sleeping devices must never stall polling beyond ~1 second. Passive push protocols (e.g., Rapoo, Delux) may use a single absolute deadline of at most 4 seconds per matching collection—do not reset the timer on unrelated reports, and comment why the window is required.
- **Cite protocol sources:** Document the origin of the protocol in a header comment at the top of the driver file.

---

## System Monitoring & Linux Contributions

- **System Vitals (`src/sysmon.cpp`, `src/gpumon.cpp`, `src/cputemp_client.cpp`):**
  - Keep sampling cheap and non-blocking; delay-load optional DLLs (`pdh.dll`, `dxgi.dll`, `nvml.dll`, `amdadlx64.dll`, `ControlLib.dll`) only when the user enables those indicators.
  - Never add driver-write paths or unprompted elevation to `TaskbarVitals.exe`. Changes to `helper/cputemp_helper.cpp` or `src/cputemp.h` must preserve strict read-only register bounds (Intel MSRs `0x1A2`, `0x1B1`; AMD SMN `0x00059800`) and session ACL restrictions.
- **Linux (`linux/`, `src/linux/`):**
  - Keep Linux-only code isolated in `linux/` and `src/linux/` with `LF` line endings (enforced by `.gitattributes`), and verify `./build-linux.sh test` passes.

---

## Translations

All 20 built-in UI languages live as UTF-8 text files in `lang/` (`Key = Translation`).

- Copy `lang/_template.txt` to `lang/<code>.txt`, set `@code` and `@name` (plus `@rtl = 1` for RTL scripts), and translate phrases.
- New UI strings in C++ must use `T(L"...")` or `TF(L"... {name} ...", { { L"name", value } })`—never concatenate translated fragments. Add every new phrase to `lang/_template.txt`.
- Keep `--list` and `--dump` diagnostic output in English so support issues remain readable.

---

## Code Style & Updater Policy

- **Formatting:** C++17, 4-space indentation, 120-column line target, small binary footprint.
- **Windows Updater:** Update status belongs strictly in the Settings panel—never add tray badges, balloons, toasts, or unprompted dialogs. Maintain ECDSA P-256 signature verification (`src/update_signature.cpp`), exact HTTPS host/redirect checks, and the local-only test suite (`make test-update-core`, `make test-update-win`, `make test-update-signature`). Before packaging, verify version headers via `scripts/verify-windows-version.ps1`.
- **Release reviews:** Installer/UAC scope changes, upgrades, and per-user/all-users paths must be tested in a clean disposable Windows environment rather than relying solely on unit tests.

---

## PR Safety & Commit Checks

Every pull request runs `.github/workflows/pr-safety.yml` (`scripts/pr-safety.sh`), which scans the diff for network calls, process creation, registry/startup writes, input hooks, binaries, or pipeline edits, and rejects commit messages containing AI `Co-authored-by` trailers.

Before opening a PR, test your diff locally in Git Bash / MSYS2:

```sh
git diff main > pr.diff && bash scripts/pr-safety.sh pr.diff && rm pr.diff
```

- Any flagged line is not an automatic rejection—just explain why it is needed in the PR description.
- Keep commits clean and remove any `Co-authored-by` trailers referencing AI tools before pushing.
