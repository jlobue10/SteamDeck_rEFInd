# Native helper consolidation — design

Status: **Linux implemented on this branch; Windows port pending.**
Phases 1-3 are done for Linux: the espops library, the helper binary, both
sudo-gated installs, both randomizers, the version handshake, the
exact-argument sudoers rules, unit repointing, installer/packaging updates,
and deletion of the superseded Linux scripts. The Windows espresolve
port and the in-process Install Config / Install Themes switchover are
written (espresolve_win.cpp: native NVRAM walk + volume enumeration +
letterless-volume directory mounts) but are **compile- and
hardware-unverified** — build on MSYS2 UCRT64 and exercise before trusting.
The Scheduled-Task migration is
also written (wintasks.cpp: native COM registration with the battery
settings preserved, unchanged task names; helper gains the Windows
`bootnext` subcommand) — same unverified status. Still to do once Windows
is verified on hardware: delete the superseded .ps1 files and update
assemble-deploy/Inno/signing accordingly, ship the helper exe in deploy/,
CLAUDE.md/README rewrites, and the on-hardware QA pass (§6 step 4). Until
then the .ps1 files remain shipped and the installers/uninstallers keep
their existing flows.
Scope decisions made with the project owner (2026-08):

1. **Full consolidation** — Install Config, Install Themes, both randomizers,
   the Windows bootnext task payload, and ESP resolution all move from
   bash/PowerShell into compiled code.
2. **Vehicle: a small dedicated helper binary** (QtCore-only console app),
   not the GUI executable with CLI flags.
3. **Passwordless-only** — the zenity password fallback for Install Config is
   dropped (Install Themes is already passwordless-only).
4. This document lands first and is reviewed before any implementation.

A tailored copy of this document exists in the sibling rEFInd_GUI repo
(`NATIVE_HELPER_DESIGN.md` there). The architecture is shared and rEFInd_GUI
is the implementation lead (it is already the parity source for the shared
detection code); the sections that differ here are the SteamOS ones: what
survives an OS update, the pinned toolchain, and the Deck's
`bootnext-refind.service`.

## 1. Motivation

Today every privileged GUI action runs an on-disk script, which forces three
pieces of trust machinery to exist:

- **The tamper hash-check** (`Platform::installConfigScriptTrusted` /
  `installThemesScriptTrusted`, `matchesShippedScript()`, four scripts'
  bytes embedded via `resources.qrc`): the GUI refuses to run
  `/etc/SteamDeck_rEFInd/install_config_from_GUI.sh` /
  `install_themes_from_GUI.sh` (and, on the fallback path, the `~/.local`
  staged `install_config_from_GUI.sh` + `lib_esp_target.sh`) unless they
  SHA-256-match the embedded copies. Consequence: *any* edit to those four
  scripts requires a rebuilt, re-released binary or every user's Install
  Config / Install Themes is blocked as "modified".
- **The Windows location-trust check** (`Platform::trustedScriptPath()`):
  hash-checking is impossible on Windows (Authenticode signing rewrites the
  `.ps1` files), so trust degrades to "resolves under Program Files".
- **Behavioral-parity duplication**: the NVRAM-first ESP resolution exists
  in bash three times over (`lib_esp_target.sh`, plus the self-contained
  inlined copies in both `_root.sh` helpers — CLAUDE.md's "keep the two in
  behavioral parity") *and* in PowerShell (`Windows/GUI/uefi_refind.ps1`),
  all kept in sync by hand across two repos.

If the logic is compiled into trusted binaries, the hash-check machinery,
the location-trust check for these actions, the inlining rule, and the
bash↔PowerShell parity burden all disappear. What **cannot** disappear on
Linux is the privilege boundary itself: the GUI deliberately runs
unprivileged, and sudo/systemd can only grant trust to a root-owned path on
disk. The sudoers rule therefore stays — it just points at a binary instead
of a script.

Secondary wins:

- A compiled helper does the privilege-hygiene parts *better* than bash:
  `runuser -u "$SUDO_USER" -- cat` becomes fork + setgroups/setgid/setuid +
  open + fd handoff — no PAM stack, no shell, no `timeout(1)` (whose
  RLIMIT_FSIZE-doesn't-survive-runuser workaround also goes away).
- The load-option parsing that PowerShell does byte-wise
  (`GetFirmwareEnvironmentVariableW`) and bash does by scraping
  `efibootmgr -v` text unifies into **one** `EFI_LOAD_OPTION` parser used on
  both platforms — retiring the "efibootmgr ≥ 18 label anchoring" class of
  bugs from the *resolution* path (NVRAM writes are a different story —
  §2.6).
- Helper messages become `tr()`-translatable later (the scripts are
  English-only today partly *because* of the tamper hash). Out of scope for
  the first release, but unlocked.

## 2. Architecture

### 2.1 Components

```
GUI/src/
  espops/            NEW — static library, platform-split like osdetect:
    espops.h           public surface (results, exit codes, progress sink)
    loadoption.cpp     EFI_LOAD_OPTION / device-path parser (pure, testable)
    espresolve_*.cpp   NVRAM-first rEFInd-ESP resolution (linux/win/common)
    configinstall.cpp  staged publish-last config install
    themesinstall.cpp  staged per-theme dir swap with rollback
    randomize.cpp      bg + theme randomizer payloads
    userio_linux.cpp   fork/setuid fd-handoff reads of the invoking user's files
  helper/            NEW — SteamDeck_rEFInd_helper (QtCore console app)
    main.cpp           subcommand dispatch, --version
  ...                gui target links espops too
```

- **`espops`** holds all logic. It is built into both the GUI and the helper.
  Like the `osdetect_*` files, `espops/` is kept **byte-identical** between
  the two repos, with the per-repo differences (data-dir name, the Deck's
  `/esp`-first preference, product name in messages) isolated in one small
  constants header that each repo owns — the same workflow as the existing
  one-hunk `isRunningSystemEsp()` difference.
- **`SteamDeck_rEFInd_helper`** is a QtCore-only console binary (no Widgets,
  no Network). One source file; everything real lives in `espops`.

### 2.2 Subcommand surface

| Subcommand | Invoked by | Replaces |
|---|---|---|
| `install-config` | GUI via `sudo -n` (Linux) | `install_config_from_GUI_root.sh` **and** the zenity fallback `install_config_from_GUI.sh` (dropped, decision 3) |
| `install-themes` | GUI via `sudo -n` (Linux) | `install_themes_from_GUI_root.sh` |
| `randomize-background` | systemd unit (Linux), Scheduled Task (Windows) | `rEFInd_bg_randomizer.sh` / `.ps1` |
| `randomize-theme` | systemd unit (Linux), Scheduled Task (Windows) | `rEFInd_theme_randomizer.sh` / `.ps1` |
| `bootnext` | Scheduled Task (**Windows only**) | `bootnext_refind_task.ps1`'s payload path via `uefi_refind.ps1` |
| `--version` | GUI (version handshake, both platforms) | the tamper hash-check |

**Deliberately NOT a subcommand: the Deck's `bootnext-refind.service`
payload.** `restore_EFI_entries.sh` stays a bash script (see §2.6) — it was
never part of the tamper/sudoers surface this design removes, and porting it
would split `toggle_entry_active()` (the SteamOS 3.9 chmod-666 NVRAM-write
quirk) across C++ and the four bash installers that must keep their copies.
It also does not source `lib_esp_target.sh`, so it doesn't block deleting
the library.

Exit codes preserve today's contract exactly (the GUI keys its dialogs off
them): 0 success, 2 no/invalid invoking user, 3 no rEFInd ESP, 4 read-only
target, 5 staging/space failure, 6 no source config / no themes. The
randomizer subcommands keep the "cosmetic service" rule: content and
environment problems warn on stderr and **exit 0**; nonzero stays reserved
for internal errors.

On **Windows the GUI itself is already elevated** (requireAdministrator
manifest), so the Install Config / Install Themes buttons call straight into
`espops` **in-process** — no helper launch, no PowerShell, no
`windowsSystemExecutable()`, no UTF-8 prologue. The helper exe exists on
Windows solely so the Scheduled Tasks have something to run when the GUI
isn't up.

On **Linux the GUI stays unprivileged** and runs
`sudo -n /etc/SteamDeck_rEFInd/SteamDeck_rEFInd_helper install-config`
synchronously, capturing combined output for the result dialog — the exact
model the passwordless script path uses today. `installConfigShowsOwnDialogs()`
simplifies away: with the zenity fallback gone, every path is synchronous
and the GUI always owns the dialog.

### 2.3 Trust model and SteamOS update survival

**Linux.** `install-GUI.sh` (and the PKGBUILD, which already installs the
root-owned script copies) installs the helper root-owned 0755 at
`/etc/SteamDeck_rEFInd/SteamDeck_rEFInd_helper`. That path is on the
persistent `/etc` overlay, so — exactly like today's scripts and sudoers
rule, and *unlike* `/usr/bin/SteamDeck_rEFInd` — it **survives SteamOS
updates**. The `~/.local` staged copy of the GUI binary that survives
updates is user-writable and is never a sudo target; nothing about that
arrangement changes.

The sudoers drop-in keeps its name
(`/etc/sudoers.d/zz_SteamDeck_rEFInd_install_config`), its load-bearing
`zz_` ordering, 0440 mode, and visudo-gating; its two lines become
exact-argument commands:

```
USER ALL = NOPASSWD: /etc/SteamDeck_rEFInd/SteamDeck_rEFInd_helper install-config
USER ALL = NOPASSWD: /etc/SteamDeck_rEFInd/SteamDeck_rEFInd_helper install-themes
```

(sudo matches listed arguments exactly, so this is as tight as today's `""`
args-forbidden rules: only those two argument vectors run without a
password.) The randomizer units repoint `ExecStart` at the helper; their
"never execute a user-writable path as root" comments carry over verbatim.

The GUI's `passwordlessConfigInstallReady()` probe becomes
`sudo -n -l -- <helper> install-config`. When the rule or helper is missing
the GUI shows "re-run install-GUI.sh to repair" — the zenity fallback is
**dropped**, deleting `scripts/install_config_from_GUI.sh` rather than
porting it. (Install Themes already behaves exactly this way.)

**Version handshake replaces the tamper hash.** The hash-check's real jobs
were (a) refusing to password-elevate the user-writable `~/.local` staged
scripts on the fallback path — gone with the fallback — and (b) catching
skew between the binary and the `/etc` copies (e.g. a user who updated the
GUI but never re-ran `install-GUI.sh`). (b) is replaced by the GUI running
`helper --version` (no sudo needed; the file is world-executable) and
comparing against `APP_VERSION`: mismatch → the same "reinstall" dialog.
This is *skew detection, not a security boundary* — the security boundary
is root ownership of the `/etc` helper, exactly as it is for the scripts
today. `matchesShippedScript()`, all four `resources.qrc` script embeds, and
both `*ScriptTrusted()` Linux implementations are deleted, along with
CLAUDE.md's "self-contained" and "placeholder-free" script rules (the
`SUDO_USER`-at-runtime rule survives as code in `userio_linux.cpp`).

**Windows.** The helper exe ships beside the GUI under Program Files
(unwritable by standard users — the same reason no portable build is
published), carries the requireAdministrator manifest, and is
Authenticode-signed like the GUI exe. `trustedScriptPath()` disappears for
these actions; the Scheduled Tasks' action is the absolute Program Files
path of the helper. The "Windows deliberately skips the hash check"
asymmetry disappears — both platforms converge on signed/root-owned
binaries plus the version handshake.

### 2.4 The shared EFI load-option parser

`espops/loadoption.cpp` parses raw `Boot####` variable bytes
(EFI_LOAD_OPTION: attributes, file-path-list length, description,
device-path list) into {description, GPT partition GUID, loader path,
optional-data}. Platform shims feed it:

- Linux: read `/sys/firmware/efi/efivars/Boot####-8be4df61-…` and
  `BootOrder-…` directly (world-readable; skip the 4-byte attributes
  prefix). No `efibootmgr -v` text scraping anywhere in the resolution path.
- Windows: `GetFirmwareEnvironmentVariableW` — the same call
  `uefi_refind.ps1` makes today, now in C++.

Resolution order is a straight port of the scripts' `resolve_refind_dir()` /
`Get-RefindNvramEsp`, preserved bug-for-bug: BootOrder-ordered entries
first, then entries missing from BootOrder (conventional
`Boot0000`–`Boot00FF` sweep on Windows); loader-path tier
(`\EFI\refind\refind*.efi`) before exact-label tier (`rEFInd`); the chosen
ESP must actually contain `EFI/refind/refind*.efi` (stale-NVRAM guard); then
any ESP containing rEFInd; then the running system's ESP (`/esp` first on
the Deck, via the constants header).

### 2.5 Ported hardening (behavior contracts, not suggestions)

Each of these exists because of a real field failure; the C++ port keeps the
semantics:

- **Staged, publish-last, never atomic-claiming** (FAT32 has no rename
  atomicity): every file lands as `.<name>.new.<suffix>` in its destination
  dir, the stale-staging sweep runs right after the destination is ensured
  (traps/destructors can't survive SIGKILL or power loss), assets and the
  staged `active_theme.conf` (into `themes/`, per `dest_dir_for()`) publish
  before `refind.conf`, `refind.conf.prev` backup, `sync` before temp
  mounts go away.
- **Read-only probe mounts**: un-mounted ESPs are probed
  `ro,nosuid,nodev,noexec` via exec of absolute-path `/usr/bin/mount`
  (parity with the scripts; no libmount dependency), only the chosen target
  is remounted rw, and only if it was our own probe mount. Cleanup
  unmounts on every exit path — including the subshell-registration
  problem the scripts solve with a tempfile, which becomes a plain member
  variable in C++.
- **systemd automount awareness**: stat `<point>/.` before trusting any
  conventional mountpoint (plain stat doesn't trigger the automount —
  AT_NO_AUTOMOUNT since Linux 4.14), and never take the first findmnt row
  at face value (autofs row sorts before the real vfat row). The helper
  resolves mounts via `/proc/self/mounts` filtering on the device, the
  C-side equivalent of `findmnt -S <dev>`, which is immune.
- **User-context reads**: root never opens anything under the invoking
  user's home. `userio_linux.cpp` forks, drops to `SUDO_USER`
  (setgroups → setgid → setuid, in that order), opens the source file in
  the child, and streams it to the parent over a pipe — the fd-passing
  equivalent of today's `runuser … cat`, with the same "images optional,
  refind.conf mandatory and non-empty" rules and a size cap enforced
  in-stream (replacing the `head -c` pipe). Theme trees transfer the same
  way (child enumerates and streams; parent writes), replacing the
  `tar | tar --no-same-owner` pipe while keeping its guarantees: symlinks
  refused (vfat can't hold them anyway), no `..`/absolute members, no
  extraction escape.
- **Themes install**: free-space precheck, per-theme staged directory swap
  with `.old-` rollback, same exit codes (2/3/4/5/6).
- **Randomizers**: enumeration with a wall-clock timeout; the bg randomizer
  reads the root-owned `/etc/SteamDeck_rEFInd/background-dir` pointer **as
  data** (never sourced/executed — same rule, now structurally guaranteed);
  the theme randomizer takes **no user-writable input at all** (candidates
  are the ESP's own root-owned `themes/*/theme.conf`), exits 0 silently
  unless `refind.conf` contains the include line, and keeps the anti-repeat
  compare.

### 2.6 What still shells out / stays bash

Deliberately unchanged:

- **`scripts/restore_EFI_entries.sh` and `bootnext-refind.service`** stay
  exactly as they are. Rationale: NVRAM *writes* on SteamOS carry the
  efibootmgr 18-3/efivar 39 can't-rewrite-existing-Boot#### quirk, whose
  `toggle_entry_active()` workaround is duplicated (by documented
  convention, "keep them in sync") across the four bash installers and
  `uninstall_rEFInd.sh` — all of which stay bash. Porting only the systemd
  payload would split that quirk logic across two languages. The unit
  already ExecStarts a root-owned `/etc` copy; there is no trust machinery
  on it to remove.
- **NVRAM writes in general** keep going through exec'd `efibootmgr` on
  Linux. Reading efivars is safe; writing them carries the
  immutable-attribute dance. The helper never writes NVRAM on Linux — no
  subcommand needs to.
- **Windows `bootnext`** reuses the already-validated
  `SetFirmwareEnvironmentVariableW` pattern from `install_rEFInd.ps1`,
  ported to C++ with the same rules: never touch entries carrying the
  `"WINDOWS"` blob; BootNext only.
- **mount/umount**: exec'd absolute paths, as above.
- The four rEFInd install scripts, `scripts/uninstall_rEFInd.sh`,
  `Windows/GUI/install_rEFInd.ps1`, `uninstall_rEFInd.ps1`, and
  `install-GUI.sh`: standalone installer tooling run in terminals, out of
  scope. Likewise the top-level `Windows/` dual-boot-fix tooling and the
  five-script UEFI-driver download steps.
- **`scripts/scan_esp.sh` / Deep Scan**: password-gated zenity tooling,
  standalone-safe, not part of the NOPASSWD surface. Candidate for a later
  `helper scan-esp` subcommand, explicitly out of scope now.

### 2.7 Scheduled Task registration (Windows)

The three `Windows/GUI/*_task.ps1` wrappers register/unregister tasks whose
action is `powershell.exe -File <payload>.ps1`. With the payloads gone,
registration moves into the GUI natively via the Task Scheduler COM API
(`ITaskService`), preserving the handheld-critical settings the wrappers set
(`AllowStartIfOnBatteries`, `DontStopIfGoingOnBatteries`,
`StartWhenAvailable`, `MultipleInstances IgnoreNew`, 5-minute execution
limit, at-logon trigger, RunLevel Highest) — bare `schtasks.exe` cannot
express the battery settings, which is why COM and not a one-liner. Task
actions point at the Program Files helper exe with the subcommand argument.
The Inno `[UninstallRun]` step keeps disabling all three tasks; old task
names registered by previous versions are unregistered on upgrade. The Inno
installer's unchecked-by-default bootnext checkbox runs the helper
(`bootnext --enable`-equivalent registration) instead of the ps1.

## 3. File-by-file impact (this repo)

**Deleted** (after migration completes):

- `scripts/install_config_from_GUI.sh` (zenity fallback — dropped, not
  ported), `scripts/install_config_from_GUI_root.sh`,
  `scripts/install_themes_from_GUI_root.sh`
- `scripts/lib_esp_target.sh` — after the randomizers go native it has
  **zero remaining consumers** (`restore_EFI_entries.sh` never sourced it;
  the install scripts legitimately target `/esp` directly)
- `scripts/rEFInd_bg_randomizer.sh`, `scripts/rEFInd_theme_randomizer.sh`
- `Windows/GUI/install_config_from_GUI.ps1`, `install_themes_from_GUI.ps1`,
  `rEFInd_bg_randomizer.ps1`, `rEFInd_theme_randomizer.ps1`,
  `uefi_refind.ps1`, and all three `*_task.ps1` wrappers
- In `GUI/src`: `matchesShippedScript()`, both `*ScriptTrusted()` Linux
  implementations, the four script entries in `resources.qrc`,
  `installConfigShowsOwnDialogs()` (always GUI-owned dialogs now)

**Kept, unchanged in role**: `SteamDeck_rEFInd_install.sh`,
`refind_install_no_pacman.sh`, `scripts/pacman_install.sh`,
`scripts/sourceforge_install.sh`, `scripts/uninstall_rEFInd.sh`,
`scripts/restore_EFI_entries.sh` (+ `bootnext-refind.service`),
`scripts/scan_esp.sh`, `install-GUI.sh` (role unchanged, content updated),
`Windows/GUI/install_rEFInd.ps1` / `uninstall_rEFInd.ps1` (the uninstaller
inherits `uefi_refind.ps1`'s helper block as a private copy — its new parity
partner is `espops/espresolve_*`), the top-level `Windows/` tooling,
`zz_SteamDeck_rEFInd_install_config` (same file, new two lines), both
randomizer `.service` units (same names, `ExecStart` repointed).

**Modified**:

- `GUI/src/CMakeLists.txt`: new `espops` static lib +
  `SteamDeck_rEFInd_helper` target; helper version stamped from the same
  `project(VERSION)` so the handshake adds **no** new entry to the
  version-sync list.
- `platform.cpp`: Linux `installConfig()`/`installThemes()` run the helper
  via `sudo -n`; Windows variants call `espops` in-process;
  randomizer/bootnext toggles use systemctl (unchanged) / COM task
  registration (new).
- `install-GUI.sh`: install the helper binary to `/etc/SteamDeck_rEFInd/`,
  write the new sudoers content, remove the superseded `/etc/*.sh` copies
  on upgrade. The "check out the release tag before disabling readonly so
  staged scripts hash-match" step relaxes to plain version alignment.
- `PKGBUILD`: build + package the helper into `/etc/SteamDeck_rEFInd/`
  (where the root script copies already live) and the repointed units.
- `scripts/build_GUI_pinned.sh` + `.github/workflows/arch-release.yml`:
  build the helper inside the same pinned snapshot and extend the
  **`Qt_6.9` symbol-version assertion to both binaries** — the helper links
  libQt6Core and would fail on-Deck in exactly the same silent way if built
  against a newer Qt.
- `Windows/GUI/assemble-deploy.sh`, `SteamDeck_rEFInd.iss`,
  `.github/workflows/windows-release.yml`: ship + SignPath-sign the helper
  exe (artifact set shrinks: eight fewer `.ps1`, one more exe).
- `GUI/src/tests/`: the load-option parser and staging planner become the
  first unit-testable privileged code in the repo — add ctest coverage
  under the existing `BUILD_GUI_TESTS` gate (fixtures from real
  `efibootmgr -v` dumps / raw variable reads).
- `CLAUDE.md`, `README.md`, `I18N_AUDIT.md`, `qa/DECK_QA_CHECKLIST.md`:
  rewritten sections after implementation (the QA checklist is exactly the
  harness this migration needs a fresh pass of).

## 4. Migration and compatibility

Upgrade is atomic per machine because `install-GUI.sh` replaces both sides
in one run; the mixed states:

- **New GUI, old `/etc`** (user installed the new package but didn't re-run
  `install-GUI.sh` — or a SteamOS update rolled back `/usr` but `/etc` and
  `/home` persisted): the `sudo -n -l` probe for the new argument vector
  fails and the version handshake finds no helper → "re-run install-GUI.sh"
  dialog. No old script is ever executed by the new GUI.
- **Old GUI, new `/etc`**: the old GUI's hash check fails against the
  now-absent scripts → its existing tamper dialog, which already says
  reinstall. Acceptable; this state only exists mid-upgrade.
- **Old sudoers + new helper**: the old lines whitelist paths that no
  longer exist and are inert; the installer overwrites the file
  (visudo-gated, `zz_` name preserved).
- **systemd units**: the installer re-copies the repointed units and
  daemon-reloads; a randomizer left enabled across the upgrade runs the
  helper on next boot with no user action. `bootnext-refind.service` is
  untouched.
- **Windows upgrade**: Inno replaces the Program Files tree; first GUI
  launch re-registers any enabled tasks against the helper exe path and
  unregisters the old powershell.exe-action task names.
- `scripts/uninstall_rEFInd.sh` / `Windows/GUI/uninstall_rEFInd.ps1`:
  remove the helper, the sudoers file, and both old and new task/unit
  generations.

## 5. Risks and mitigations

- **~600 lines of battle-tested bash re-implemented with no test suite and
  hardware-only validation.** The largest real risk. Mitigations: `espops`
  is structured so the pure parts (load-option parsing, staging plans, path
  sanitization) are unit-tested under `BUILD_GUI_TESTS`; the port is
  contract-first (§2.5 is the checklist); a full `qa/` pass on a real Deck
  gates the release; implementation happens in rEFInd_GUI first and is
  ported here by the established copy-plus-constants-header workflow.
- **Root attack surface**: QtCore linked into a NOPASSWD root binary.
  Smaller and non-interpreted compared to bash + coreutils + the shell;
  Widgets/Network/DBus are not linked; the argument surface is two fixed
  vectors enforced by sudoers.
- **Auditability**: users lose "read the privileged script on the device".
  Partially compensated: the helper source is short, `--version` ties the
  binary to a tag, and the invariants are documented here rather than in
  script comments.
- **Cross-repo parity**: `espops/` becomes parity-locked like `osdetect_*`
  (byte-identical, one constants header per repo). This *replaces* the
  current bash-inline/bash-lib/ps1 three-way parity, it doesn't add to it.
  The one new seam: `uninstall_rEFInd.ps1`'s private NVRAM helper block ↔
  `espops/espresolve_win.cpp`.
- **Pinned-toolchain coupling**: the helper is a second binary that must be
  built in the Arch Archive snapshot and pass the `Qt_6.9` assertion; both
  build paths change together (workflow + `build_GUI_pinned.sh`), same as
  every snapshot bump today.
- **Helper output i18n**: helper stdout is shown in GUI dialogs. First
  release keeps English (status quo); `tr()` in `espops` is a follow-up
  once the tamper-hash constraint is gone (`I18N_AUDIT.md` updated then).

## 6. Implementation order (one release train)

Phases are implementation order, **not** separate releases — phase 2 alone
would ship a C++↔bash parity split for the randomizers, so the release that
ships any of this ships all of it.

1. `espops` static lib + helper skeleton + `--version` handshake + unit
   tests for the load-option parser and staging planner.
2. `install-config` / `install-themes` native on both platforms; GUI paths
   switched; tamper machinery deleted; sudoers/installer updated.
3. Randomizers + Windows bootnext into the helper; units repointed; COM
   task registration; payload/wrapper/`uefi_refind.ps1` scripts and
   `lib_esp_target.sh` deleted.
4. Packaging (PKGBUILD/pinned build/Inno/workflows), uninstallers, docs
   (CLAUDE.md/README/I18N_AUDIT/QA checklist), full QA pass on a real Deck
   (LCD and OLED if available) and a Windows-side pass on the same Deck.

Cross-repo: rEFInd_GUI implements first (it is the parity source for shared
code already); this repo ports by copying `espops/` + `helper/` and
re-applying the constants header — the same workflow the `osdetect_*` files
use today.
