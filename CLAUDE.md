# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

SteamDeck_rEFInd installs and configures the [rEFInd](https://www.rodsbooks.com/refind/) boot manager for dual/multi-booting a Steam Deck (SteamOS + Windows/Linux/Batocera/etc). It ships two ways to use it:

1. **Shell script installers** (bash, run on the Deck itself in desktop mode) that install rEFInd and drop a hand-authored `refind.conf`.
2. **A Qt/C++ GUI** (`GUI/src/`) that generates `refind.conf` from user input instead of requiring manual edits, and provides buttons for install/uninstall/systemd toggling.

There is no app backend, package manifest, or test suite — this is an installer/config-generator project glued together with bash and one Qt binary. Treat correctness in terms of "does the resulting shell command / config stanza do the right thing on a real Steam Deck," not unit tests.

## Repo layout

- `SteamDeck_rEFInd_install.sh` — primary installer: pacman-based rEFInd install, EFI boot entry surgery via `efibootmgr`, copies `refind-GUI.conf` → `/esp/efi/refind/refind.conf`, installs the `bootnext-refind` systemd service.
- `refind_install_no_pacman.sh` — same end result, but downloads rEFInd from Sourceforge instead of using `pacman` (fallback when SteamOS pacman repos are broken).
- `install-GUI.sh` — installs the GUI app: disables `steamos-readonly`, stages files into `$HOME/.local/SteamDeck_rEFInd/`, pulls the latest GitHub release `.pkg.tar.zst` and installs it with `pacman -U`, re-enables `steamos-readonly`.
- `refind.conf` / `refind-GUI.conf` — the actual rEFInd config templates. Boot entries are **manually authored stanzas** (not rEFInd's auto-detection) specifically to control icon left-to-right order on the boot screen. `refind-GUI.conf` is the stripped/templated version the GUI overwrites with generated stanzas.
- `PKGBUILD` — Arch/`makepkg` package definition; builds the GUI via CMake and installs the binary, desktop file, icon, and both systemd unit files. This is the source of truth for the GUI's release artifact.
- `GUI/src/` — Qt Widgets C++ app (`mainwindow.cpp`/`.h`/`.ui`, `main.cpp`, `CMakeLists.txt`). All GUI logic lives in `mainwindow.cpp` as one large file with free functions and globals (not modularized) — see below.
- `scripts/` — small bash helpers invoked by the GUI or systemd units (`install_config_from_GUI.sh` uses `zenity` for a sudo password prompt, `restore_EFI_entries.sh` recreates missing SteamOS/rEFInd EFI entries, `rEFInd_bg_randomizer.sh`, `pacman_install.sh`, `sourceforge_install.sh`).
- `systemd/` — `bootnext-refind.service` (forces rEFInd to the front of the EFI boot order on every boot — SteamOS's A/B partition scheme can otherwise reorder/drop it) and `rEFInd_bg_randomizer.service`.
- `Windows/` — PowerShell/batch scripts run **from Windows**, not the Deck: fixing the "Windows boots straight past rEFInd" problem (`bootsequence-rEFInd-first.ps1`), a BIOS-update pre/post EFI-entry save-restore pair, and a background randomizer for the Windows side.
- `icons/`, `backgrounds/` — boot-menu assets (icons must be 128x128 PNG, backgrounds 1280x800 PNG) referenced by path from `refind.conf`.
- `VERSION` — plain version string, currently `1.4.0`; also duplicated in `PKGBUILD`'s `pkgver` and hardcoded as `VERSION = 140` in `GUI/src/mainwindow.cpp`. **Keep all three in sync when bumping version.**

## Building the GUI

```
cd GUI/src
mkdir -p build && cd build
cmake ..
make
```
Requires Qt5 or Qt6 (`Widgets`, `LinguistTools` components) and a C++17 toolchain — this only builds meaningfully on Linux (targets SteamOS/Arch). There is no test suite; the GUI is validated by building it and exercising it manually against a real or spare `/esp` partition.

The official release artifact is produced via `PKGBUILD` (`makepkg`), which drives the same CMake build and packages the binary plus both systemd units.

## Architecture notes for `GUI/src/mainwindow.cpp`

- State is held in file-scope globals (`QString`/`std::string`/`bool`), not member variables — e.g. `Boot_Option_1..4`, `Background`, `refind_install_source`, `Firmware_BootNum_bool`. Slots read/write these directly.
- Settings persistence uses `QSettings` at `~/.local/SteamDeck_rEFInd/GUI/rEFInd_GUI.ini` (`readSettings()`/`writeSettings()`), so GUI selections survive relaunch.
- `CreateBootStanza()` builds one rEFInd `menuentry` stanza per boot slot from the dropdown/line-edit selections; `getDefaultBoot()` decides which stanza becomes the default/first-boot entry; `getPartitionGUIDLabel()` resolves the `volume` UUID line for SD-card/USB Windows entries (see the "Windows from Micro SD card" caveat below).
- "Install rEFInd" and "Create/Install Config" button handlers shell out to the bash scripts in `scripts/` (often via an `xterm`/`zenity` prompt for sudo) rather than reimplementing privileged operations in C++ — keep that split when adding features (GUI = build config + orchestrate, bash = anything needing `sudo`).
- The `Firmware_bootnum` option only works if a SteamOS EFI boot entry already exists at config-generation time (queried via `efibootmgr`); it exists solely to keep the SteamOS icon visible during the rEFInd→SteamOS handoff.

## Working with `refind.conf`

- Boot stanza order in the file is the left-to-right icon order on screen — this is deliberate (see repo layout note above); don't "clean up" the file into auto-detect mode.
- SD-card/USB Windows entries need a `volume <UUID>` line pointing at that device's ESP, and must have their `disabled` line removed to activate — this is currently a known-broken path in GUI generation (see README "Windows from Micro SD card instructions"), so manual edits are still expected there.
- Icon/background filenames referenced in `refind.conf` must match actual files shipped in `icons/`/`backgrounds/` (or the GUI's staged copies under `~/.local/SteamDeck_rEFInd/`).

## Conventions to follow when editing scripts

- Every privileged install/uninstall script wraps its `pacman`/file-copy work in `sudo steamos-readonly disable` ... `sudo steamos-readonly enable` — preserve that bracketing in any new script that writes outside `$HOME`.
- EFI boot-entry manipulation always goes through `efibootmgr`, parsing `efibootmgr` output with `grep -Eo '[0-9]{1,4}'` to extract boot numbers by label (`Windows`, `SteamOS`, `rEFInd`) — follow the same pattern rather than assuming fixed boot numbers, since they vary per device.
- Scripts target `/dev/nvme0n1` partition 1 as the ESP when recreating EFI entries — this assumes the Deck's internal NVMe layout; don't generalize it to arbitrary disks.
