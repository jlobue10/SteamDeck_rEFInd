#ifndef PLATFORM_H
#define PLATFORM_H

#include <QString>
#include <QStringList>

// Thin platform layer so mainwindow.cpp stays free of #ifdef Q_OS_WIN.
// Linux: xterm + scripts/*.sh + systemd (incl. the Deck's bootnext-refind
// service). Windows: PowerShell scripts + Scheduled Task (the exe runs elevated
// via its requireAdministrator manifest).
namespace Platform {

// App data root: ~/.local/SteamDeck_rEFInd on Linux,
// %LOCALAPPDATA%\SteamDeck_rEFInd on Windows.
QString dataDir();

// Populate a new per-user data directory from the immutable files shipped next
// to the executable. Existing user files are never overwritten.
void prepareDataDir();

// Launches the rEFInd installer for the chosen source in a new terminal window.
bool runInstallerScript(const QString &installSource);

// Installs the generated config + PNGs onto the ESP. Returns 0 on success.
// Linux, passwordless path (root-owned /etc/SteamDeck_rEFInd script +
// NOPASSWD sudoers rule installed by install-GUI.sh): runs `sudo -n` on the
// script synchronously and captures its combined output into *output for the
// caller to present. Linux, fallback when the rule is missing: launches the
// interactive zenity script detached; *output stays empty and a nonzero
// return only means the launch itself failed. Windows: runs the PowerShell
// installer synchronously with no console window and captures its combined
// output into *output.
int installConfig(QString *output = nullptr);

// True when installConfig() reports its result to the user itself (the Linux
// zenity fallback's dialogs); false when the caller must present the captured
// output (the Linux passwordless path and Windows).
bool installConfigShowsOwnDialogs();

// True when the config-install script(s) about to be used are byte-identical
// (SHA-256) to the copies this build shipped, so they are safe to run with
// root privileges. Passwordless path: checks the root-owned /etc copy
// (catches a stale copy from another GUI version). Fallback path: checks the
// staged zenity script and the ESP-resolution helper it sources into its
// root payload. On mismatch (tampered, missing, or from another version)
// returns false and puts the offending file's path in *detail — the caller
// must refuse to run them and suggest reinstalling. On Windows, privileged
// scripts must resolve beneath the Program Files installation directory.
bool installConfigScriptTrusted(QString *detail = nullptr);

// Enables/disables the boot-background randomizer (systemd unit / scheduled task).
bool setBackgroundRandomizer(bool enable);

// Enables/disables the Deck's bootnext-refind.service (keeps rEFInd first in the
// boot order). Linux only.
bool setBootnextService(bool enable);

// systemd-based features (Sysd + Rand BG buttons) exist on Linux only.
bool systemdFeaturesAvailable();

// Runs the elevated ESP scan (scripts/scan_esp.sh), caching the EFI/ tree for
// detection to read. Blocks while the script prompts for a password and shows
// its own result dialogs. Returns 0 on success. Linux only: the Windows build
// runs elevated and scans ESPs directly.
int runEspDeepScan();

// Whether an ESP the GUI wants to scan is unreadable, so the Deep Scan button
// is worth offering. False on Windows.
bool espDeepScanUseful();

// SteamOS firmware_bootnum lookup needs efibootmgr (Linux only).
bool firmwareBootnumSupported();

// Which OS leads auto-selection (slot 1 + default boot): SteamOS on the Deck
// (Linux), Windows on the Windows build.
bool preferWindowsAsDefault();

// Entries for the Install Source combo box.
QStringList installSourceOptions();

} // namespace Platform

#endif // PLATFORM_H
