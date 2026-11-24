# Security Policy

## Hardware & System Safety Guarantees

Taskbar Vitals is built to read telemetry safely without changing device settings, lighting, macros, or firmware.

- **Read-only peripheral queries:** Vendor drivers in `src/drivers/` only issue read-only battery and link-status HID/RFCOMM queries. Devices are matched strictly by vendor ID, product ID, or protocol-specific HID collections; unknown devices are never written to.
- **No unprompted elevation:** The main application (`TaskbarVitals.exe`) runs as a standard unprivileged user process for all wireless peripheral monitoring, CPU/RAM usage, and GPU telemetry.
- **Isolated CPU temperature helper (`TaskbarVitalsTemp.exe`):** Reading Intel CPU package temperature requires MSR access through the external [PawnIO](https://pawnio.eu) driver. When enabled by the user, `TaskbarVitalsTemp.exe` runs as an isolated elevated helper (under a Windows-protected `Program Files` path), embeds the unmodified PawnIO `IntelMSR` module, reads only `IA32_TEMPERATURE_TARGET` (`0x1A2`) and `IA32_PACKAGE_THERM_STATUS` (`0x1B1`), and shares the numeric result over read-only shared memory (`Local\TaskbarVitalsCpuTemp`). It never writes MSRs or accepts external commands beyond shutdown signaling.
- **100% local data:** Device readings, custom aliases, alert thresholds, and 30-day battery history stay on your PC. Nothing is transmitted externally.

### Network connections

Apart from the Bluetooth/HID device queries above, the only outbound connections are:

| What | Host | When | How to avoid |
|---|---|---|---|
| Update check (official stable builds only; `src/update_win.cpp`) | `api.github.com` | About 30 seconds after startup, then at most once every 24 hours; also on **Check for updates**. An update is downloaded from `github.com` hosts only after you click **Download and install**. | Turn off **Automatically check for updates**; development builds never check. |
| Speed test (`src/netmon.cpp`) | `speed.cloudflare.com` | Only when you click **Run speed test**. Downloads about 10 MB and uploads 2 MB of filler data. | Don't click it. |

Both are plain HTTPS requests with no account or token, so the host sees your IP address (and, for the update check, the app version in the user agent). No device readings, battery history, or usage data are sent.

## Cryptographically Signed Windows Updates

Official stable Windows builds verify release integrity before offering **Download and install**:

1. Update checks query the public GitHub Releases API over HTTPS without an account or token and send no hardware or battery information.
2. Downloads are restricted to HTTPS connections on `github.com`, `objects.githubusercontent.com`, and `release-assets.githubusercontent.com`, with redirect host validation.
3. Before launching an installer, Taskbar Vitals verifies an ECDSA P-256 signature (`SHA256SUMS.txt.sig`) using Windows BCrypt against the maintainer's public key embedded in `src/update_public_key.h`. The private signing key never lives on GitHub.
4. Both the downloaded installer size and SHA-256 hash are verified against the signed `SHA256SUMS.txt` manifest and re-verified under an exclusive file lock immediately before execution.

See [docs/releasing.md](docs/releasing.md) for the complete release and key-rotation specification.

## Reporting a Vulnerability

If you discover a security issue (for example in the updater verification logic, the elevated CPU temperature helper, IPC permissions, or HID parsing):

- **Do not** publish exploit details in a public GitHub issue before a fix is ready.
- Open a private security advisory on GitHub (`Security` → `Advisories` → `Report a vulnerability`) at [symNistaken/TaskbarVitals/security/advisories](https://github.com/symNistaken/TaskbarVitals/security/advisories).
- Include the affected component, Windows or Linux version, reproduction steps, and potential impact.

Reports are reviewed as quickly as possible so fixes and signed updates can be issued promptly.
