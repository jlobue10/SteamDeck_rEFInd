# Security Audit — SteamDeck_rEFInd & rEFInd_GUI

**Date:** 2026-08-31
**Version audited:** 3.4.1 (both repos)
**Scope:** Full defensive security review of both sibling repositories — privileged C++
(`espops` + helper binary), all bash installers/scripts, systemd units and the sudoers
template, the Windows PowerShell/Inno tooling, the unprivileged Qt GUI, and the CI /
supply chain. Both repos were reviewed together because `espops/`, `helper/main.cpp`,
the `osdetect_*` files and `previewdialog`'s `PreviewTheme` are parity-locked
byte-identical between them; findings in shared code apply to **both** and any fix must
be made in both to preserve the parity lock.

## Remediation status (v3.4.2)

Addressed in the v3.4.2 release:

- **#1/#2 Windows LPE + junction delete** — the four `.bat` wrappers now refuse a
  pre-existing reparse point, delete-and-recreate `C:\rEFInd_Scripts` so the admin is
  its owner, reset owner to Administrators, and fail closed if the create races; the
  uninstaller refuses to recurse into a junction.
- **#8 bare-name calls** — the legacy Windows `.ps1`s and the standalone uninstaller now
  resolve `bcdedit`/`findstr`/`mountvol`/`reg` by absolute System32 path.
- **#3 rEFInd download integrity** — the Sourceforge installers verify the archive
  against a pinned SHA-256 (`f0f90fcc…`) before extracting.
- **#9 randomizer symlink** — `randomize.cpp` now `lstat`s the backgrounds dir and refuses
  a symlink/non-directory before dropping privileges (byte-identical in both repos).
- **#10 rich-text dialogs** — Install Config/Themes result dialogs render captured output
  as `Qt::PlainText`.
- **#11 sudoers username**, **#13 `scan_esp.sh` mount flags**, **#12 Inno Setup
  Authenticode gate**, and the RPM `Source1` staging note — all fixed.

Tracked as follow-ups (not in v3.4.2), with rationale in each finding:

- **#4 UEFI driver pinning** — the driver URL deliberately tracks `releases/latest` of the
  jlobue10 fork under active development (documented in CLAUDE.md); pinning waits until the
  fork is upstreamed.
- **#5 release-package signing** and **#7 commit-pinned sources** — need a `SHA256SUMS`
  release asset / signed tags, a release-process change beyond this code bump.
- **#6 Arch bootstrap checksum + pacman signing** — deferred rather than hardcode a hash
  that could not be verified from the audit environment; a wrong pin would break the
  official Deck build.

## Bottom line

The security architecture is fundamentally sound. **No unprivileged→root escalation
was found on Linux**, and the highest-value target — untrusted boot-entry
titles/labels flowing into a `refind.conf` that root installs — is correctly sanitized
at every sink. The parity lock between the repos is intact (verified byte-identical
apart from the documented `espconstants.h` and `/esp` differences).

The one genuinely exploitable issue is a **High-severity local privilege escalation on
Windows**, and it lives entirely in the *legacy* `C:\rEFInd_Scripts` dual-boot-fix
tooling under `SteamDeck_rEFInd/Windows/` — **not** in the GUI installer chain, which is
well-hardened. Everything else is download-integrity hardening and defense-in-depth.

### Severity summary

| # | Severity | Area | Issue |
|---|----------|------|-------|
| 1 | **High** | Windows legacy tooling | `C:\rEFInd_Scripts` LPE — ACL fix resets neither ownership nor pre-planted ACEs; folder is a `-RunLevel Highest` logon-task target |
| 2 | Medium | Windows legacy tooling | Junction/`Remove-Item -Recurse -Force` arbitrary elevated delete on the same attacker-createable path |
| 3 | Medium | Both — installers | rEFInd zip from Sourceforge installed with no checksum/signature (runs pre-OS, Secure Boot off on Deck) |
| 4 | Medium | Both — installers | UEFI driver `.efi` files fetched from `releases/latest` with only an `MZ`/`PK` magic-byte check |
| 5 | Medium | SteamDeck — install-GUI | Unsigned GitHub release installed via `pacman -U` (root install scriptlet, no local verification) |
| 6 | Medium | CI / build | Pinned Arch bootstrap tarball not checksum-verified; snapshot pacman runs `SigLevel = Never` |
| 7 | Medium | Packaging | PKGBUILD/spec/deb sources pin mutable git *tags* with `SKIP` checksums; dispatch-rebuild can repackage moved tags |
| 8 | Low | Windows legacy tooling | Native tools (`bcdedit`/`mountvol`/`reg`/`findstr`) called by bare name in elevated scripts (PATH-hijack) |
| 9 | Low | espops (shared) | Background randomizer drops to the **resolved** dir owner after a `stat()` that follows a symlink (multi-user cross-user PNG read) |
| 10 | Low | GUI (shared) | Result dialogs use `Qt::AutoText`; ESP-derived strings (`refind.conf.origin`) can render as clickable rich-text links |
| 11 | Low | Both — scripts | `$USER`/`$HOME` spliced into the sudoers/desktop files via `sed` without validation (fails safe via `visudo -cf`) |
| 12 | Low | CI | Chocolatey `innosetup` on the signing builder is unpinned/unverified (sits inside the SignPath signing path) |
| 13 | Low | Various | Assorted hardening (see full list): `scan_esp.sh` mount flags, staged-script trust, `%LOCALAPPDATA%` junctions, unbounded reads |
| — | Info | rEFInd_GUI CI | `rpm-release.yml` stages only `Source0`; spec declares `Source1` (theme unit) — verify the RPM build (correctness, not security) |

---

## Detailed findings

### 1. [High] Windows LPE via `C:\rEFInd_Scripts` (legacy dual-boot-fix tooling)

**Files:** `Windows/Install rEFInd/Setup_rEFInd_Windows_RunAsAdmin.bat`,
`Windows/Uninstall rEFInd/Remove_rEFInd_Windows_RunAsAdmin.bat`,
`Windows/BIOS Update/{PRE,POST}_bios_install_RunAsAdmin.bat`,
amplified by `Windows/Install rEFInd/Setup_rEFInd_Windows.ps1`.

Each `.bat` does `if not exist C:\rEFInd_Scripts mkdir` then
`icacls C:\rEFInd_Scripts /inheritance:r /grant:r "*S-1-5-32-544:(OI)(CI)F" ...`.
The comment correctly identifies that directories under `C:\` inherit an
Authenticated-Users-create ACE, but the remediation is incomplete:

- `/inheritance:r` removes only *inherited* ACEs; `/grant:r` replaces entries only for
  the three named SIDs. Neither removes an **explicit ACE an attacker already placed for
  their own SID**, and neither changes **ownership**.
- A non-admin can pre-create `C:\rEFInd_Scripts`. As creator they are the **owner**
  (owner keeps implicit `WRITE_DAC`/`READ_CONTROL` regardless of DACL) and can add a
  self-grant that survives the `icacls` call.

`Setup_rEFInd_Windows.ps1` registers a scheduled task **`-RunLevel Highest`** whose
action runs `C:\rEFInd_Scripts\Setup_rEFInd_Windows.ps1` at **every logon**. The
attacker rewrites that file at leisure → reliable persistent elevated code execution.

**Fix:** after ensuring the directory, (a) refuse a pre-existing dir or reparse point,
and (b) reset owner and purge foreign ACEs — delete-and-recreate a fresh dir (verifying
it is not a reparse point), then add `icacls ... /setowner *S-1-5-32-544` alongside the
grants. Best: follow the GUI installer's own pattern (`install_rEFInd.ps1` stages under
`%SystemRoot%\Temp`, admin-only by construction) and place the logon-task target under
an admin-only root rather than `C:\`.

### 2. [Medium] Junction → elevated arbitrary delete

**File:** `Windows/Uninstall rEFInd/Remove_rEFInd_Windows.ps1`
(`Remove-Item -Path "C:\rEFInd_Scripts" -Recurse -Force`, run elevated).

Because the path is attacker-createable (finding 1), a non-admin can pre-create it as a
directory **junction** to e.g. `C:\Windows\System32`. Windows PowerShell 5.1's
`Remove-Item -Recurse` follows junctions → elevated arbitrary content deletion
(corruption/DoS). The `%SystemRoot%\Temp` staging purges are **not** affected
(admin-only parent).

**Fix:** verify the path is a real directory and not a reparse point before recursive
deletion, or delete only known child files.

### 3. [Medium] rEFInd download has no integrity verification

**Files:** `refind_install_no_pacman.sh`, `scripts/sourceforge_install.sh` (and the
rEFInd_GUI `refind_install_Sourceforge.sh`), plus the Windows `install_rEFInd.ps1`.

The rEFInd release zip is fetched from Sourceforge and checked only with `unzip -t`
(corruption) — never a SHA-256 or GPG signature. The unpacked `refind_x64.efi` becomes
the first boot entry, i.e. it runs **pre-OS with Secure Boot off on the Deck**. A
compromised/redirected Sourceforge mirror delivers an attacker loader that runs at every
boot. (The pacman path is fine — pacman verifies the `refind` package signature.)

**Fix:** pin the known SHA-256 of `refind-bin-gnuefi-0.14.2.zip` and `sha256sum -c`
before unzipping; abort on mismatch.

### 4. [Medium] UEFI driver downloads: unpinned `releases/latest`, magic-byte only

**Files:** all rEFInd install scripts in both repos and both `install_rEFInd.ps1`.

`UsbXbox360Dxe.efi` and `TouchI2cDxe.efi` are fetched from
`github.com/jlobue10/.../releases/latest/download/...` and validated only by a 2-byte
`MZ` check (any PE passes). `releases/latest` pins neither version nor content, so a
future or compromised release silently changes the UEFI code the ESP loads at boot —
the highest-privilege artifact in the product with the weakest pinning.

**Fix:** pin an explicit driver release tag and verify a committed SHA-256 of each
`.efi` before copying; keep the warn-and-continue behavior on failure.

### 5. [Medium] `install-GUI.sh` installs an unsigned release with `pacman -U`

**File:** `install-GUI.sh` (SteamDeck_rEFInd).

The release URL comes from the unauthenticated GitHub API; the `.pkg.tar.zst` is
`wget`'d and installed via `sudo pacman -U --noconfirm`. GitHub releases carry no
`.sig`, and pacman's `LocalFileSigLevel` is `Optional` by default, so the package
installs unsigned — and its `.install` scriptlet runs **as root**. Combined with finding
7 (mutable tags + dispatch rebuild), a tag-moving attacker gets root on every installer
run. Integrity rests entirely on HTTPS + GitHub-account trust with no defense in depth.

**Fix:** publish and verify a `SHA256SUMS` (or minisign) release asset; at minimum
compare against a checksum shipped in the checked-out tag before `pacman -U`.

### 6. [Medium] Pinned Arch build: unverified bootstrap tarball + `SigLevel = Never`

**Files:** `.github/workflows/arch-release.yml`, `scripts/build_GUI_pinned.sh`.

The whole toolchain that produces the *official Deck release binary* is pulled from
`archive.archlinux.org` with package signature verification explicitly disabled, and the
bootstrap tarball is imported with no hash check. The sole integrity boundary is TLS to
one host; a compromised archive host or poisoned CDN cache silently backdoors the
compiler → backdoors every released `.pkg.tar.zst`, and the Qt-ABI check still passes.

**Fix (cheap, total):** both the bootstrap tarball and every package in a dated snapshot
are immutable — hard-code the SHA-256 of `archlinux-bootstrap-2025.07.01-x86_64.tar.zst`
in both files. For packages, install the era-matched archived `archlinux-keyring` and use
`SigLevel = Required` (era keys verify fine against an era keyring). Keep the workflow and
the script in sync as the header already requires.

### 7. [Medium] Package recipes pin mutable git tags with `SKIP` checksums

**Files:** both `PKGBUILD`s, `rEFInd_GUI.spec`, `rEFInd_GUI` `debian`/deb workflow.

Sources are pinned to git *tags* (`#tag=v${pkgver}` / `git clone --branch v$pkgver`)
with `md5sums=('SKIP')` and no `#commit=` / signed-tag check. Tags are force-pushable: an
attacker with push access (stolen PAT, compromised account) moves a tag to a backdoored
commit, triggers the "rebuild an existing release" `workflow_dispatch` (the pkgver guard
only compares version strings), and CI repackages different code under the same release.
Local PKGBUILD builders (the documented CachyOS path) are equally exposed.

**Fix:** pin sources by commit hash (`#commit=<full sha>`), or sign tags and
`git verify-tag` before building. Populate real checksums for the local `.service`
sources in the AUR PKGBUILD.

### 8. [Low] Bare-name native command calls in elevated Windows scripts

**Files:** `Windows/bootsequence-rEFInd-first.ps1`, `Windows/Install
rEFInd/Setup_rEFInd_Windows.ps1`, `Windows/BIOS Update/*`, `Windows/bios_install_prep.ps1`,
`Windows/Uninstall rEFInd/Remove_rEFInd_Windows.ps1`, both `windows/uninstall_rEFInd.ps1`,
`Windows/GUI/diagnose_install_config.ps1`.

These call `bcdedit`/`findstr`/`mountvol`/`reg` by bare name while elevated.
`install_rEFInd.ps1` deliberately does the opposite (`[Environment]::SystemDirectory`),
and CLAUDE.md states the rule. If any user-writable dir is earlier on the machine `PATH`,
a planted `bcdedit.exe`/`mountvol.exe` runs elevated. PowerShell doesn't search CWD, so
PATH-only — but the inconsistency should be closed.

**Fix:** route every native call through `Join-Path ([Environment]::SystemDirectory)
'<tool>.exe'`, matching `install_rEFInd.ps1`.

### 9. [Low] Background randomizer follows a symlink when resolving drop-to owner

**File:** `GUI/src/espops/randomize.cpp` (`randomizeBackground`, Linux; shared/parity-locked).

The code `::stat(bgDir)` (follows symlinks), takes `st.st_uid`, resolves that account and
reads the `*.png` as that owner. On a **multi-user** system, the invoking user can replace
the final path component with a symlink into another user's tree; `stat` follows it, the
fork-child drops to *user2* and streams *user2*'s PNGs to the shared ESP `background.png`,
which *user1* can then read — a marginal cross-user read primitive (PNG-signatured files
only). Depends on `/etc/<product>/background-dir` and its parent staying root-owned
(installer-enforced).

**Fix:** `lstat` the pointer-named dir and refuse a symlink, or open it
`O_NOFOLLOW|O_DIRECTORY` in the dropped-privilege child, or resolve the intended owner
from the root-owned pointer rather than the user-swappable directory. Apply in **both**
repos.

### 10. [Low] GUI result dialogs render ESP-derived strings as rich text

**File:** `GUI/src/mainwindow.cpp` (Install Config/Themes result dialogs, invalid-PNG and
theme-not-found dialogs; shared).

The static `QMessageBox::information/warning/critical` helpers default to `Qt::AutoText`.
The captured helper output shown here can embed ESP-derived strings — notably the
`refind.conf.origin` sidecar, which *another* product/user wrote onto the shared ESP. A
hostile sidecar containing `<a href='https://phish'>Click to finish install</a>` renders
as a live link inside a trusted success dialog.

**Fix:** show these via a `QMessageBox` instance with `setTextFormat(Qt::PlainText)`
wherever captured output or filesystem-derived strings are displayed. Apply in **both**
repos.

### 11. [Low] Unvalidated `$USER`/`$HOME` `sed`-spliced into sudoers/desktop files

**Files:** `install-GUI.sh` / `install-rEFInd-GUI.sh` (sudoers template), and the
`.desktop` `HOME` substitution.

`$USER` is spliced unvalidated into a sudoers rule. A username with `sed`
metacharacters (`@`, `&`, `\`) corrupts the substitution; one with whitespace could in
principle produce a syntactically valid widening rule. **Not exploitable** — the actor is
the same user who already holds sudo, and `visudo -cf` gates installation (a bad rule
fails closed to a password prompt rather than being written). Worth fixing because a
broken `/etc/sudoers.d` file can wedge sudo.

**Fix:** validate against `^[a-z_][a-z0-9_-]*$` before templating (or `printf` the line
instead of `sed`).

### 12. [Low] Unpinned Chocolatey Inno Setup inside the signing path

**Files:** both `.github/workflows/windows-release.yml` (`choco install innosetup -y`).

ISCC.exe builds the installer *between* the two SignPath stages, so its output is what
gets signed. A malicious/unpinned `innosetup` package can embed a payload that then
receives a valid SignPath Foundation signature — the one third-party binary in the signed
path not from a signature-checked repo.

**Fix:** pin the version (`--version=6.x.y`) and/or `Get-AuthenticodeSignature` the ISCC
binaries (publisher "Jordan Russell") before invoking.

### 13. [Low] Assorted hardening (defense-in-depth)

- **`scripts/scan_esp.sh`** temp-mounts unmounted/removable ESPs `ro` only; the sibling
  `diagnose_install_config.sh` already uses `ro,nosuid,nodev,noexec`. Match it.
- **Password-gated staged scripts** (`scan_esp.sh`, installer scripts) live under
  `~/.local/<app>/` and are launched unprivileged before prompting for the user's
  password; since the SHA-256 tamper check was removed nothing verifies them first
  (same-user malware could edit them to capture root on the next click). Same-trust-domain,
  so Low — but the one privileged flow the version-handshake redesign left uncovered.
  Consider moving the scan payload into the root-owned helper, or restoring a hash check
  for the two remaining password-gated scripts.
- **Windows `%LOCALAPPDATA%` junction** (`platform.cpp` `dataDir`/`prepareDataDir`): the
  elevated GUI writes into a user-writable path; a same-user process can pre-plant a
  junction to redirect elevated writes. MSRC treats same-user UAC as not-a-boundary
  (Low). Mirror `install_rEFInd.ps1`'s reparse-point refusal.
- **`previewdialog.cpp` `resolveThemeAsset()`** permits `..` after the `themes/` marker
  (display-only, same-user data — Info). Add `QDir::cleanPath` + prefix check.
- **Update check** (`mainwindow.cpp`) does an unbounded `reply->readAll()`; cap the read
  (a version string needs a few bytes) and set an explicit redirect policy.
- **Unbounded `readAll()`** of foreign `loader/entries/*.conf` from a hostile USB ESP
  (memory DoS at worst); **`checkPNGFile`→`copyPng` TOCTOU** (self-attack only).
- **systemd units** carry no sandboxing directives; the core control (root-owned
  `ExecStart` on the persistent `/etc` overlay, never a `$HOME` path) is correctly in
  place, but low-risk additions (`ProtectKernelModules`, `RestrictAddressFamilies=AF_UNIX
  AF_NETLINK`, `ProtectControlGroups`) are available for the NVRAM/ESP writers.

### [Info] rEFInd_GUI RPM CI — `Source1` staging

`rEFInd_GUI.spec` declares `Source0: rEFInd_bg_randomizer.service` and
`Source1: rEFInd_theme_randomizer.service`, but `.github/workflows/rpm-release.yml` copies
only the bg unit into `SOURCES/`. The spec's `%prep` also `cp`s both units out of its own
fresh git clone into `$RPM_SOURCE_DIR`, so whether the RPM build fails depends on
rpmbuild's upfront `SourceN` existence pre-check. **Verify the RPM release actually
builds**; if it fails with "Bad source", stage the theme unit in the workflow too. This is
a correctness issue, not a security one.

---

## What was verified sound (no action needed)

- **Parity lock intact.** `helper/main.cpp` and all of `espops/` are byte-identical
  between the repos except `espconstants.h` (documented repo-owned values only); the
  `osdetect_*`, `previewdialog`, `uitranslation` and `tests/` files are byte-identical,
  and `osdetect_common.cpp` differs only by the documented `/esp` hunk.
- **The privilege boundary holds.** On Linux, root never reads user-controlled content as
  root: config/theme source reads go through the fork + privilege-drop child
  (`ForkUserFiles`), so a symlink/hardlink under `~/.local` fails as the user (EACCES),
  never exfiltrating to the ESP. Privilege-drop order is textbook (`initgroups → setgid →
  setuid`, each checked, `_exit` on any failure, irreversibility asserted).
- **`SUDO_USER` validation** rejects empty, `root`, unknown users, any name resolving to
  uid 0, and a missing home directory.
- **Config-injection defenses.** `confSanitize`/`confQuote` strip `"`/`{`/`}` and flatten
  newlines; every external string reaching `refind.conf` (menu titles, volume labels,
  loader/icon paths, firmware bootnum, resolution/timeout/showtools from the INI) was
  traced and is sanitized or re-validated at its sink. Crafted systemd-boot titles cannot
  inject a stanza.
- **EFI_LOAD_OPTION parser** (`loadoption.cpp`) is fully bounds-checked; malformed NVRAM
  yields `valid=false`, never an OOB read.
- **Path traversal** is blocked on both the user-context walk (`nameIsClean`) and the
  root write side (`relPathSafe`); theme names are `readdir` single components, never
  passed to the helper as arguments.
- **Randomizer input handling:** 32 MiB per-file cap enforced parent-side, PNG-signature
  check, no image decoding in the helper (no decompression-bomb surface); the
  `background-dir` pointer is validated (root-owned, non-symlink, not group/other-writable)
  and read as data, never sourced.
- **NVRAM surgery fails closed:** disk/part/PARTUUID resolved from `/esp` and NVRAM
  untouched if resolution fails; new rEFInd entry created before old ones are deleted;
  verified by device path + live loader presence; read-back summary; the bounded `chmod
  666 → write → chmod 644` efivars workaround with restore; the "never touch the WINDOWS
  blob" rule respected.
- **`steamos-readonly` bracketing** is complete on every traced path (EXIT trap set
  before disable, distinct exit 70 when restore itself fails).
- **The GUI installer chain on Windows** (`install_rEFInd.ps1` + Inno) is exemplary:
  `%SystemRoot%\Temp` staging with owner=Administrators, `SetAccessRuleProtection`,
  fresh-GUID name, pre-existing-path + reparse-point refusal; `PrivilegesRequired=admin`,
  `{autopf}` target, absolute `{sys}\...` calls, `runascurrentuser` GUI launch.
- **CI is well-run:** every GitHub Action is pinned to a full commit SHA; least-privilege
  `GITHUB_TOKEN` (`contents: read` top-level, elevated only on release-attach); no
  `pull_request_target`; PR-triggered workflows gated to OWNER/MEMBER/COLLABORATOR with no
  secrets for forks; no `${{ }}` script injection; artifacts passed by id (no
  substitution); rEFInd_GUI container images digest-pinned.
- **Secrets scan clean** across the full git history of both repos (AWS/`ghp_`/`xox`/
  `sk-`/`AIza`/PEM patterns); no committed keys; the only "token" references are proper
  `${{ secrets.* }}` expressions.
- **No shell-string command construction from external data** anywhere; all external
  commands use `QProcess` argument lists / `QFile::copy`; all downloads are HTTPS.

---

## Recommended priority

1. **Fix the Windows LPE (findings 1 & 2)** in the legacy `C:\rEFInd_Scripts` tooling —
   the only exploitable issue.
2. **Add download verification** for the rEFInd zip (3), UEFI drivers (4), and the release
   package (5) — these install code that runs as root or pre-OS.
3. **Harden the build/supply chain** — checksum the bootstrap tarball + restore pacman
   signing (6); pin sources by commit hash (7); pin Inno Setup (12).
4. **Shared-code Low fixes** (apply to both repos to keep parity): randomizer symlink (9),
   `PlainText` dialogs (10).
5. **Minor hardening** (8, 11, 13) and the RPM CI verification as time permits.

*Fixes were not applied as part of this audit — this report is the deliverable. Say the
word and I can implement any subset, keeping the shared `espops`/GUI changes byte-identical
across both repos.*
