## What's New in Taskbar Vitals v2.1.2

Device fixes: a Bluetooth mouse no longer shows up twice, and three devices stay listed when they go quiet.

### Fixes
- **Bluetooth mice (Logitech, Corsair):** a mouse read by its own driver and by Windows' Bluetooth battery sometimes showed two cards ("USB / Bluetooth" and "Bluetooth"). When the mouse's driver misses a reading, Windows' level now fills the same card, so you always see one.
- **MCHOSE V9 Pro:** some dongles stop answering a while after the headset goes to sleep. The headset now stays listed as asleep with its last level instead of showing "Disconnected" or disappearing.
- **8BitDo Ultimate 2.4G (first generation):** stays listed on the charging dock and in X-input mode, with its last known level. In X-input mode a note asks you to switch to D-input mode, where the battery can be read.

### New (untested)
- **Razer Nari Essential** (receiver 1532:051E) is tried with the same battery query as the BlackShark V2 Pro (2020) and Barracuda X (2021). It has not been confirmed on a real headset yet; reports are welcome.

### Docs
- Plainer README opening, a social preview card for link shares, and a link to the maintainer's X account.

### Verification & SmartScreen Note
- Official releases are unsigned open-source builds; if Windows Defender SmartScreen appears on first run, pick **More info → Run anyway** and compare your download against `SHA256SUMS.txt`.
- Installed auto-updates verify `SHA256SUMS.txt.sig` offline against the maintainer's ECDSA P-256 key (`src/update_public_key.h`).
