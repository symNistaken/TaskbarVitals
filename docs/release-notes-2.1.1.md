## What's New in Taskbar Vitals v2.1.1

Maintenance release: translations, one fix and some internal cleanup.

### Translations
- Phrases added in 2.0 and 2.1 are now translated in all 21 languages.
- Languages other than English and Turkish are machine-translated. Native speakers are welcome to review them through a pull request or issue.

### Fixes
- Two dashboard battery-health phrases were never translated in any language: "Estimated full-charge runtime" and the mWh capacity part.

### Internal
- `main.cpp` is split into `app_settings`, `notifications` and `app_commands` modules. No behaviour change.
- The UI hit test is more robust.

### Docs
- README, SECURITY and the website now list the only network connections: the update check to GitHub and the user-started speed test to Cloudflare.
- The website credits BatteryHub.

### Verification & SmartScreen Note
- Official releases are unsigned open-source builds; if Windows Defender SmartScreen appears on first run, pick **More info → Run anyway** and compare your download against `SHA256SUMS.txt`.
- Installed auto-updates verify `SHA256SUMS.txt.sig` offline against the maintainer's ECDSA P-256 key (`src/update_public_key.h`).
