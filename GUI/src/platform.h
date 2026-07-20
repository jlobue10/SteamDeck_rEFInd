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

// Launches the rEFInd installer for the chosen source in a new terminal window.
bool runInstallerScript(const QString &installSource);

// Installs the generated config + PNGs onto the ESP. Returns 0 on success.
// Linux: launches the interactive zenity script detached; *output stays empty
// and a nonzero return only means the launch itself failed. Windows: runs the
// PowerShell installer synchronously with no console window and captures its
// combined output into *output for the caller to present.
int installConfig(QString *output = nullptr);

// True when installConfig() reports its result to the user itself (the Linux
// script's zenity dialogs); false when the caller must present the captured
// output (Windows).
bool installConfigShowsOwnDialogs();

// True when the config-install script and the ESP-resolution helper it
// sources are byte-identical (SHA-256) to the copies this build shipped, so
// they are safe to feed to a root shell. On mismatch (tampered, missing, or
// from another version) returns false and puts the offending file's path in
// *detail — the caller must refuse to run them and suggest reinstalling.
// Always true on Windows: the .ps1 scripts are Authenticode-signed instead,
// and signing rewrites the file so a build-time hash could never match.
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
