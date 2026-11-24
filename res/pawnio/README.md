# PawnIO modules for the CPU temperature

`IntelMSR.bin` and `AMDFamily17.bin` are the official, signed **IntelMSR** and **AMDFamily17** modules of [PawnIO](https://pawnio.eu), unchanged.

| | |
|---|---|
| Source | https://github.com/namazso/PawnIO.Modules (release **0.2.12**, `release_0_2_12.zip`) |
| SHA-256 of `IntelMSR.bin` | `30a5a302283de1e080dfee99e686eca91d7e53e911095cb91a320fb961f1ce12` |
| SHA-256 of `AMDFamily17.bin` | `dae74615761b78bdf064dfb3e136252ddcc6fc727d88f14738d0e5800d427a91` |
| SHA-256 of the release zip | `09698897ce19b92f1043a2e322cfcf0b1fe823e0b576971b178a65244fb0af9d` (matches the digest GitHub publishes) |
| License | GNU LGPL 2.1 or later, copyright namazso (`COPYING` is the license text from the release) |

They are built into `TaskbarVitalsTemp.exe` (the helper that reads the CPU temperature, see `helper/`) as resources (`200` and `201`). To use different builds of these modules, replace the `.bin` files and rebuild.

The modules only run inside the PawnIO driver, which is **not** part of Taskbar Vitals: it is installed separately from
https://pawnio.eu (the author asks software to point users there):
- On Intel CPUs, `IntelMSR.bin` is used read-only on two MSRs: `IA32_PACKAGE_THERM_STATUS` (0x1B1) and `IA32_TEMPERATURE_TARGET` (0x1A2).
- On AMD Ryzen CPUs (Family 17h–1Ah), `AMDFamily17.bin` is used read-only via `ioctl_read_smn` on `THM_TCON_CUR_TMP` (0x00059800).
