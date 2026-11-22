# Releasing

The app updates itself from GitHub releases, but it only installs a setup whose SHA-256 is listed in
a `SHA256SUMS.txt` signed with the release key. That key lives on the maintainer's PC, never on
GitHub: a hacked account, token or workflow can publish files, but not a signature the app accepts.

## Once: create the release key

```
powershell -ExecutionPolicy Bypass -File scripts\new-release-key.ps1
```

- Asks for a password (12+ characters) and writes the encrypted private key to
  `%USERPROFILE%\.wirelessbattery\release-key.wbk`.
- Puts the public key into `src\update_public_key.h`. Commit that file. Official builds refuse
  to compile without a key in it.
- **Back up the key file and keep the password** (a password manager is a good place for both).
  If either is lost, the installed apps can no longer update themselves: you would create a new
  key and every user would have to install one release by hand.

## Every release

1. Push a version tag (`git tag v1.2.3 && git push origin v1.2.3`). CI builds the setup, the
   portable zip and `SHA256SUMS.txt`, and publishes the release.
2. Sign the checksum list:

   ```
   powershell -ExecutionPolicy Bypass -File scripts\sign-release.ps1 -Version 1.2.3
   ```

   It downloads `SHA256SUMS.txt` from the release, shows it, asks you to confirm, asks for the key
   password and writes `SHA256SUMS.txt.sig`. If you have the same files in `dist\` (a local build of
   that version), it shows whether they match.
3. Upload `SHA256SUMS.txt.sig` to the release (the script opens the edit page). From then on the app
   offers **Download and install**. Before that it shows the update but only links to the release page.

Only sign a list for a release you published yourself. The signature says "these exact files are the
release", so signing is the step where a malicious upload would be caught.

## Changing the key

Run `new-release-key.ps1` with a new `-KeyFile`: it adds the new public key next to the old one in
`src\update_public_key.h`. Release a version signed with the old key that contains both, then sign
with the new key from the next release on and remove the old key later. A lost or leaked key
cannot be retired this way: remove it from the header, and users of older versions have to install
the next release by hand.

## What the app checks before running a setup

- The release is the repository's latest, not a draft or prerelease, and newer than the app.
- `SHA256SUMS.txt.sig` is a valid ECDSA P-256 signature of `SHA256SUMS.txt` by a key in
  `src\update_public_key.h` (Windows BCrypt, no third-party code).
- The signed list names `BatteryHub-Setup-<version>.exe` exactly once, with the same SHA-256
  that GitHub reports for the file.
- The downloaded setup has exactly that size and SHA-256. A copy of the app then checks it again
  right before starting it, and keeps the file locked so it cannot be swapped in between.
- Downloads only use HTTPS to `github.com`, `objects.githubusercontent.com` and
  `release-assets.githubusercontent.com`, with each redirect checked.
