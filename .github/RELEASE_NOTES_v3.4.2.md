# v3.4.2 — Security hardening

This release remediates the actionable findings from a full defensive security
audit of both repos (`SECURITY_AUDIT_2026-08.md`). **The audit found no
unprivileged→root escalation on Linux** — the sudoers vector-locking, root-owned
systemd units, the `espops` privilege-drop and `SUDO_USER` validation, the
config-injection sanitizers, the EFI-load-option parser, and the NVRAM
fail-closed logic were all verified sound. The one genuinely exploitable issue
was a **Windows local privilege escalation** in SteamDeck_rEFInd's legacy
dual-boot-fix tooling, now fixed.

The GUI and helper are released together, as always (version handshake).

## Fixed

### Windows privilege escalation — SteamDeck_rEFInd only (High)
The legacy `C:\rEFInd_Scripts` dual-boot-fix wrappers created that folder and
tried to lock it with `icacls /inheritance:r /grant:r` — which resets neither
ownership nor an attacker's pre-planted ACE. A non-admin could pre-create the
folder (becoming its owner, keeping implicit `WRITE_DAC`) and rewrite the script
that a `-RunLevel Highest` logon task runs elevated at every logon — a persistent
LPE. Fixed in all four `.bat` wrappers: refuse a pre-existing reparse
point/junction, delete-and-recreate the folder so the admin is the creator/owner,
`icacls /setowner` to Administrators, and fail closed if the create races.
`Remove_rEFInd_Windows.ps1` now refuses to recurse into a junction (elevated
arbitrary-delete guard).

### PATH-hijack hardening — Windows (both repos)
The legacy `.ps1` scripts and the standalone `uninstall_rEFInd.ps1` now resolve
`bcdedit`/`findstr`/`mountvol`/`reg` by absolute `%SystemRoot%\System32` path
rather than by name, matching `install_rEFInd.ps1`. An elevated script can no
longer be redirected by a same-named binary planted earlier on the machine PATH.

### Backgrounds-randomizer symlink — both repos (espops, parity-locked)
`randomize.cpp` now `lstat`s the backgrounds directory and refuses a symlink or
non-directory before dropping privileges. Previously a `stat()` followed a
symlink, so on a multi-user system a user could point the final path component at
another user's directory and have the randomizer drop to that user and copy their
PNGs onto the shared ESP. Identical, byte-for-byte, in both repos.

### Rich-text result dialogs — both repos
Install Config / Install Themes result dialogs now render captured helper output
as `Qt::PlainText`. The output can contain ESP-derived strings (the
`refind.conf.origin` sidecar another product/user wrote onto the shared ESP), and
the default `Qt::AutoText` would render an embedded `<a href>` as a live,
clickable link inside a trusted success dialog.

### Download & install hardening — both repos
- The Sourceforge rEFInd installers verify the downloaded archive against a pinned
  SHA-256 (rEFInd 0.14.2 = `f0f90fcc…`) before extracting an EFI binary that boots
  pre-OS. In rEFInd_GUI the pin is version-gated so a future `REFIND_VER` bump
  fails loudly instead of checking the wrong hash.
- The installers validate the username before splicing it into the sudoers rule
  (`sed` metacharacters / rule widening); `visudo -cf` remains the backstop.
- `scan_esp.sh` now probe-mounts ESPs `ro,nosuid,nodev,noexec`.

### CI / supply chain — both repos
- `windows-release.yml` requires a valid Authenticode signature on `ISCC.exe`
  before building the installer that SignPath then signs.
- **rEFInd_GUI only:** `rpm-release.yml` now stages `Source1` (the theme
  randomizer unit) so `rpmbuild`'s source pre-check passes — the RPM build was
  previously missing it.

## Known follow-ups (not in this release)
Documented with rationale in `SECURITY_AUDIT_2026-08.md`:
- UEFI driver downloads still use `releases/latest` — the URL deliberately tracks
  the in-development jlobue10 driver fork; pinning waits until it's upstreamed.
- Release-artifact signing (`SHA256SUMS`) and commit-pinned package sources.
- Arch bootstrap-tarball checksum + restoring pacman signature verification in the
  pinned SteamOS build (deferred rather than hardcode a hash that couldn't be
  verified from the audit environment).

## Version sync
`VERSION`, `mainwindow.cpp` (`APP_VERSION`), `CMakeLists.txt`, the manifest, the
Inno script, and `PKGBUILD` — plus `rEFInd_GUI.spec` (`%changelog`) and
`debian/changelog` in rEFInd_GUI — all bumped to 3.4.2.
