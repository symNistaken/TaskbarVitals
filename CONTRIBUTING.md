# Contributing

## Reporting whether a device works

Most drivers were written from protocol documentation and could not be tested on every model.
Please open an issue with:

- the device name, and whether it is connected by receiver, cable or Bluetooth
- the output of `BatteryHub.exe --list`
- the output of `BatteryHub.exe --dump`
- what the vendor's own software shows as the battery level

## Adding a device

1. Run `BatteryHub.exe --dump` and find your device's vendor and product IDs, and the
   vendor-defined collections (usage page `FFxx`) with their report sizes.
2. Find the battery protocol. Good places to look: HeadsetControl, OpenRazer, Solaar, rivalcfg,
   OpenRGB, OpenLinkHub, or a USB capture of the vendor software (Wireshark + USBPcap).
3. If the vendor already has a driver in `src/drivers/`, add the product ID (and any differences) there.
   Otherwise create `src/drivers/<vendor>.cpp`:

   ```cpp
   #include "drivers.h"

   void PollAcme(PollContext& ctx) {
       for (auto [vid, pid] : ctx.Products(0x1234)) {           // unclaimed products of this vendor
           for (const HidColl* c : ctx.Find(vid, pid, [](const HidColl& c) { return c.page() == 0xFF00; })) {
               HidDev d;
               if (!d.Open(*c)) continue;
               std::vector<BYTE> r;
               if (!d.Write({ 0x01, 0x02 }) || d.Read(r, 500) < 3) continue;   // hidapi-style buffers
               DeviceStatus s;
               s.key = L"acme:" + Hex4(pid);
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

4. Declare the function in `src/drivers/drivers.h`, call it from `PollAllDevices()` in
   `src/devices.cpp` (before the generic fallbacks). `build.bat` and the Makefile pick up new files automatically.
5. Update the device table in `README.md`.

### Rules for drivers

- **Read-only:** only send requests that read state. Never change settings, lighting or modes.
- **Exact matching:** only talk to devices you recognise by vendor and product ID, or by a
  vendor-specific collection that is unique to the protocol.
- **Always time out:** a missing device must not stall the poll for more than about a second.
  Passive protocols may need longer to cover a documented push interval (for example, Delux and
  Rapoo). Keep a single absolute deadline of at most 4 seconds per matching collection, do not
  restart it for unrelated reports, and document why the exception is needed.
- **Credit sources:** cite where the protocol comes from in a comment at the top of the file.

## Translations

A language is one UTF-8 text file in `lang/` (copy `lang/_template.txt`, fill in `@code`, `@name` and the phrases). It is built into the app automatically. New UI text in the code goes through `T(L"...")`, or `TF(L"... {name} ...", { { L"name", value } })` when it contains values; never build translated sentences from pieces. After adding phrases, add them to `lang/_template.txt` too.

## Code style

C++17, Win32 only, no third-party dependencies. Keep the binary small. Format roughly as the
existing code does (4-space indent, 120-column lines).

## Windows updater development

The updater is Windows-only and uses WinHTTP, BCrypt and the existing Inno Setup installer.
Keep update status exclusively in the menu. Do not add update icons, badges, battery tooltip
changes, balloons, toasts or automatic dialogs. Device polling runs separately from network work.

Build version and channel are shared by C++ and the Windows resource through generated
`build/version.h`. Use `make VERSION=1.2.3 CHANNEL=test` or `build.bat 1.2.3 test` for local
testing; `dev` is the default and `stable` enables official release checks. Version components
must fit 0..65535. Only the official repository's release-tag workflow selects stable
automatically. Do not turn a fork build into an official update channel accidentally.

Run `make test VERSION=1.2.3 CHANNEL=test` for existing device tests plus strict release parsing,
version ordering, check scheduling, real SHA-256 file checks, argument quoting and helper guards.
Updater regressions also cover cancellation at download completion, dialog guards and failure
to configure HTTP timeouts/redirect restrictions. The Windows test includes the implementations,
following the existing Linux backend test pattern, and substitutes HTTP sending so these tests
cannot contact GitHub. No test-only controller entry points are added to production.
Individual targets are `make test-update-core` and `make test-update-win`. Native updater tests
link the application resource to activate Common Controls v6. Tests use local data and do not
download, elevate, install or change the user's startup settings. The application's production
endpoint cannot be overridden for tests.

Before packaging, run
`powershell -NoProfile -File scripts/verify-windows-version.ps1 -ExpectedVersion 1.2.3`.
`installer/build-installer.ps1 -Version 1.2.3` also runs this check before downloading dependencies.
The `/WBUPDATE=1` installer path preserves existing startup/settings and leaves relaunch to the
temporary helper. That helper remains under the original user, elevating only a global setup.
WinHTTP handles belong to the worker; cancellation is cooperative between finite-timeout calls.
Final app shutdown joins that worker and can therefore wait for an in-flight network timeout.

For a release review, use a disposable Windows account or VM to exercise two real installer
versions, per-user/global scope, UAC cancellation, startup on/off, custom paths, setup failure
and relaunch. Unit tests and compiling a package do not verify those installer effects. Visual
menu/theme review and real installer/UAC end-to-end checks remain manual release gates; do not
mark them passed unless actually observed. Never test by overwriting the daily user's install.

## Automatic safety check

Every pull request is scanned by the "PR safety check" for things this app should never do (network
access, launching programs, reading keyboard/clipboard/screen, startup entries, binaries, build or
release changes). A flag is not a rejection: just explain the flagged lines in the PR description.

Using AI tools is fine, but please remove `Co-authored-by` lines that credit them (for example
`Co-authored-by: Claude ...`) from your commit messages. The check fails while they are present.
