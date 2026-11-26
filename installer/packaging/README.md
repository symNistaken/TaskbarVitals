# Packaging Manifest Templates (winget & Scoop)

This directory holds ready-to-stamp templates for publishing Taskbar Vitals releases to **winget** and **Scoop**. The template files use all-zero placeholder hashes until a release tag is built and uploaded.

## Release Workflow

1. Build release artifacts (`dist\TaskbarVitals-Setup-<Version>.exe`, `dist\TaskbarVitals-<Version>-portable.zip`, and `dist\SHA256SUMS.txt`):
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File installer\build-installer.ps1 -Version 2.1.2
   ```
2. Stamp real SHA-256 digests from `dist\SHA256SUMS.txt` into the Scoop and winget manifests:
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\update-packaging-manifests.ps1 -Version 2.1.2
   ```
3. Publish the GitHub release tag `v2.1.2` with `TaskbarVitals-Setup-2.1.2.exe`, `TaskbarVitals-2.1.2-portable.zip`, `SHA256SUMS.txt`, and `SHA256SUMS.txt.sig`.
4. Validate locally before submitting PRs:
   - **winget:** run `winget validate --manifest installer\packaging\winget\manifests\s\symNistaken\TaskbarVitals\2.1.2` (and test install with `winget install --manifest ...`), then open a PR against [`microsoft/winget-pkgs`](https://github.com/microsoft/winget-pkgs).
   - **Scoop:** test locally with `scoop install installer\packaging\scoop\taskbar-vitals.json`, then submit to [`ScoopInstaller/Extras`](https://github.com/ScoopInstaller/Extras) or your personal Scoop bucket.
