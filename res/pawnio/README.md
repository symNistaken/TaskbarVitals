# PawnIO module for the CPU temperature

`IntelMSR.bin` is the official, signed **IntelMSR** module of [PawnIO](https://pawnio.eu), unchanged.

| | |
|---|---|
| Source | https://github.com/namazso/PawnIO.Modules (`IntelMSR.p`, release **0.2.12**, `release_0_2_12.zip`) |
| SHA-256 of `IntelMSR.bin` | `30a5a302283de1e080dfee99e686eca91d7e53e911095cb91a320fb961f1ce12` |
| SHA-256 of the release zip | `09698897ce19b92f1043a2e322cfcf0b1fe823e0b576971b178a65244fb0af9d` (matches the digest GitHub publishes) |
| License | GNU LGPL 2.1 or later, copyright namazso (`COPYING` is the license text from the release) |

It is built into `TaskbarVitalsTemp.exe` (the helper that reads the CPU temperature, see `helper/`) as a resource. To use a
different build of the module, replace this file and rebuild.

The module only runs inside the PawnIO driver, which is **not** part of Taskbar Vitals: it is installed separately from
https://pawnio.eu (the author asks software to point users there). The module itself allows reading only a list of Intel MSRs;
the helper uses two of them, `IA32_PACKAGE_THERM_STATUS` (0x1B1) and `IA32_TEMPERATURE_TARGET` (0x1A2), and never writes one.
