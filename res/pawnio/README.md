# PawnIO modules for the CPU temperature

`IntelMSR.bin` and `AMDFamily17.bin` are the official, signed **IntelMSR** and **AMDFamily17** modules of [PawnIO](https://pawnio.eu), unchanged.

| | |
|---|---|
| Source | https://github.com/namazso/PawnIO.Modules (`IntelMSR.p`, `AMDFamily17.p`, release **0.2.12**, `release_0_2_12.zip`) |
| SHA-256 of `IntelMSR.bin` | `30a5a302283de1e080dfee99e686eca91d7e53e911095cb91a320fb961f1ce12` |
| SHA-256 of `AMDFamily17.bin` | `dae74615761b78bdf064dfb3e136252ddcc6fc727d88f14738d0e5800d427a91` |
| SHA-256 of the release zip | `09698897ce19b92f1043a2e322cfcf0b1fe823e0b576971b178a65244fb0af9d` (matches the digest GitHub publishes) |
| License | GNU LGPL 2.1 or later, copyright namazso (`COPYING` is the license text from the release) |

Both are built into `TaskbarVitalsTemp.exe` (the helper that reads the CPU temperature, see `helper/`) as resources. To use different builds of the modules, replace these files and rebuild.

The modules only run inside the PawnIO driver, which is **not** part of Taskbar Vitals: it is installed separately from
https://pawnio.eu (the author asks software to point users there).
- On Intel CPUs, the helper uses `IntelMSR.bin` and reads only `IA32_PACKAGE_THERM_STATUS` (`0x1B1`) and `IA32_TEMPERATURE_TARGET` (`0x1A2`), and never writes an MSR.
- On AMD Ryzen CPUs (Family 17h–1Ah), the helper uses `AMDFamily17.bin` and reads only SMN register `THM_TCON_CUR_TMP` (`0x00059800`) via `ioctl_read_smn` while holding `Global\Access_PCI`.
