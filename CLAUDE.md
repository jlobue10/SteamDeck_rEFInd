# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

SteamDeck_rEFInd installs and configures the [rEFInd](https://www.rodsbooks.com/refind/) boot manager for dual/multi-booting a Steam Deck (SteamOS + Windows/Linux/Batocera/etc). It ships two ways to use it:

1. **Shell script installers** (bash, run on the Deck itself in desktop mode) that install rEFInd and drop a hand-authored `refind.conf`.
2. **A Qt/C++ GUI** (`GUI/src/`) that auto-detects the installed OSes/bootloaders on the EFI System Partition, generates `refind.conf` from that, and provides buttons for install/uninstall/systemd toggling. As of 2.0.0 the GUI also builds and runs on **Windows** (configure rEFInd from the Windows side of a dual-boot Deck).

There is no app backend or test suite — this is an installer/config-generator project glued together with bash/PowerShell and one Qt binary. Treat correctness in terms of "does the resulting shell command / config stanza do the right thing on a real Steam Deck," not unit tests.

## Repo layout

- `SteamDeck_rEFInd_install.sh` — primary installer: pacman-based rEFInd install, EFI boot entry surgery via `efibootmgr`, copies `refind-GUI.conf` → `/esp/efi/refind/refind.conf`, installs the `bootnext-refind` systemd service.
- `refind_install_no_pacman.sh` — same end result, but downloads rEFInd from Sourceforge instead of using `pacman` (fallback when SteamOS pacman repos are broken).
- `install-GUI.sh` — installs the GUI app: disables `steamos-readonly`, stages files into `$HOME/.local/SteamDeck_rEFInd/`, pulls the latest GitHub release `.pkg.tar.zst` and installs it with `pacman -U`, re-enables `steamos-readonly`.
- `refind.conf` / `refind-GUI.conf` — the actual rEFInd config templates. Boot entries are **manually authored stanzas** (not rEFInd's auto-detection) specifically to control icon left-to-right order on the boot screen. `refind-GUI.conf` is the stripped/templated version the GUI overwrites with generated stanzas.
- `PKGBUILD` — Arch/`makepkg` package definition; builds the GUI via CMake and installs the binary, desktop file, icon, and both systemd unit files. This is the source of truth for the GUI's release artifact.
- `GUI/src/` — Qt Widgets C++ app. `mainwindow.cpp`/`.h`/`.ui` (config generator + button orchestration), `osdetect.*` (OS/bootloader detection, split `osdetect_common/linux/win.cpp`), `platform.cpp/.h` (the only `#ifdef Q_OS_WIN` site), and `rEFInd_GUI.manifest`/`.rc` (Windows UAC manifest). See architecture notes below.
- `Windows/GUI/` — the Windows Qt-build packaging (distinct from the top-level `Windows/` dual-boot-fix tooling): PowerShell equivalents of the Deck scripts, `assemble-deploy.sh`/`copydeps.sh`, the Inno Setup script `SteamDeck_rEFInd.iss`, and `SIGNING.md`. Nested under `Windows/` (not a lowercase `windows/`) to avoid a case collision on Windows filesystems.
- `scripts/` — small bash helpers invoked by the GUI or systemd units (`install_config_from_GUI.sh` uses `zenity` for a sudo password prompt, `restore_EFI_entries.sh` recreates missing SteamOS/rEFInd EFI entries, `rEFInd_bg_randomizer.sh`, `pacman_install.sh`, `sourceforge_install.sh`).
- `systemd/` — `bootnext-refind.service` (forces rEFInd to the front of the EFI boot order on every boot — SteamOS's A/B partition scheme can otherwise reorder/drop it) and `rEFInd_bg_randomizer.service`.
- `Windows/` — PowerShell/batch scripts run **from Windows**, not the Deck: fixing the "Windows boots straight past rEFInd" problem (`bootsequence-rEFInd-first.ps1`), a BIOS-update pre/post EFI-entry save-restore pair, and a background randomizer for the Windows side.
- `icons/`, `backgrounds/` — boot-menu assets (icons must be 128x128 PNG, backgrounds 1280x800 PNG) referenced by path from `refind.conf`.
- `VERSION` — plain version string (`2.0.0`), fetched by the update check and compared via `QVersionNumber`. Duplicated in: `GUI/src/mainwindow.cpp` (`APP_VERSION`), `GUI/src/CMakeLists.txt` (`project(... VERSION)`), `GUI/src/rEFInd_GUI.manifest` (four-part), `Windows/GUI/SteamDeck_rEFInd.iss` (`AppVersion`), and `PKGBUILD` (`pkgver`). **Keep all in sync when bumping.**

## Building the GUI

Linux (SteamOS/Arch):
```
cd GUI/src
mkdir -p build && cd build
cmake ..
make
```
Windows (MSYS2 UCRT64): `cmake -G Ninja -S GUI/src -B build-win && cmake --build build-win`, then `bash Windows/GUI/assemble-deploy.sh build-win/SteamDeck_rEFInd.exe deploy` and compile `Windows/GUI/SteamDeck_rEFInd.iss` with Inno Setup. The Windows exe embeds a `requireAdministrator` manifest (via `rEFInd_GUI.rc` for MinGW / a link flag for MSVC). Pushing a `v*` tag runs `.github/workflows/windows-release.yml` to build + SignPath-sign + release the installer and portable ZIP (see `Windows/GUI/SIGNING.md`).

Requires Qt5 or Qt6 (`Widgets`, `LinguistTools`) and a C++17 toolchain. No test suite; validate by building and exercising against a real or spare ESP. The official Linux release artifact is produced via `PKGBUILD` (`makepkg`).

## Architecture notes for `GUI/src`

As of 2.0.0 the GUI was refactored to match the sibling `rEFInd_GUI` repo (member state instead of file-scope globals; all external commands via `QProcess` argument lists / `QFile::copy`, never shell strings built from user input). Never reintroduce `system()`/`popen` with interpolated user data.

- **`OSDetector`** (`osdetect.*`): `osdetect_common.cpp` scans the ESP's `EFI/` vendor dirs for loaders (SteamOS `steamcl.efi`, Windows `bootmgfw.efi`, shim/GRUB/systemd-boot) and assembles cross-volume entries (SYSTEM labels, Batocera/Ventoy, removable ESPs by partition GUID). `osdetect_linux.cpp` enumerates via `lsblk -J` and finds the ESP — **`/esp` first** on the Deck. `osdetect_win.cpp` uses PowerShell + `mountvol`. The removable-ESP-by-partition-GUID path **fixes the old Windows-on-SD-card `volume` bug** that the README documents.
- **`Platform`** (`platform.cpp`, the only `#ifdef Q_OS_WIN`): data dir, installer/config/randomizer launchers (Linux = xterm + `scripts/*.sh` + systemd incl. the Deck's `bootnext-refind.service`; Windows = `.ps1` + Scheduled Task), `firmwareBootnumSupported()`, `preferWindowsAsDefault()` (SteamOS leads on the Deck, Windows on the Windows build), and the Pacman/Sourceforge (Linux) vs Sourceforge-only (Windows) install sources. The Sysd buttons are disabled when `systemdFeaturesAvailable()` is false (Windows).
- **`MainWindow`**: detection populates the four boot-option combos (`BootEntry` via `QVariant`); `applyAutoSelection()` + `compactBootSelections()` pack detected OSes into slots 1, 2, … with no gaps, and the **Rescan** button re-detects and resets to defaults. `createBootStanza()` renders one data-driven stanza per non-None slot (stanza order = on-screen icon order). Settings persist **by text** (`BootOption0XText`) in `~/.local/SteamDeck_rEFInd/GUI/rEFInd_GUI.ini`.
- The `Firmware_bootnum` option (SteamOS) still needs an existing SteamOS EFI entry at config-generation time (queried via `efibootmgr`); it keeps the SteamOS icon visible during the rEFInd→SteamOS handoff. Linux only.
- Keep the split: GUI builds config + orchestrates; the bash/PowerShell scripts do anything needing elevation.

## Working with `refind.conf`

- Boot stanza order in the file is the left-to-right icon order on screen — this is deliberate (see repo layout note above); don't "clean up" the file into auto-detect mode.
- SD-card/USB Windows entries need a `volume <partition GUID>` line pointing at that device's ESP. As of 2.0.0 the detection engine resolves this automatically (the old README-documented broken path), so manual edits are no longer required when the media is inserted at Create-Config time.
- Icon/background filenames referenced in `refind.conf` must match actual files shipped in `icons/`/`backgrounds/` (or the GUI's staged copies under `~/.local/SteamDeck_rEFInd/`).

## Conventions to follow when editing scripts

- Every privileged install/uninstall script wraps its `pacman`/file-copy work in `sudo steamos-readonly disable` ... `sudo steamos-readonly enable` — preserve that bracketing in any new script that writes outside `$HOME`.
- EFI boot-entry manipulation always goes through `efibootmgr`, parsing `efibootmgr` output with `grep -Eo '[0-9]{1,4}'` to extract boot numbers by label (`Windows`, `SteamOS`, `rEFInd`) — follow the same pattern rather than assuming fixed boot numbers, since they vary per device.
- Scripts target `/dev/nvme0n1` partition 1 as the ESP when recreating EFI entries — this assumes the Deck's internal NVMe layout; don't generalize it to arbitrary disks.
