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

// Installs the generated config + PNGs onto the ESP. Returns 0 on success
// (Linux: launches the interactive zenity script; Windows: blocking install).
int installConfig();

// Enables/disables the boot-background randomizer (systemd unit / scheduled task).
bool setBackgroundRandomizer(bool enable);

// Enables/disables the Deck's bootnext-refind.service (keeps rEFInd first in the
// boot order). Linux only.
bool setBootnextService(bool enable);

// systemd-based features (Sysd + Rand BG buttons) exist on Linux only.
bool systemdFeaturesAvailable();

// SteamOS firmware_bootnum lookup needs efibootmgr (Linux only).
bool firmwareBootnumSupported();

// Which OS leads auto-selection (slot 1 + default boot): SteamOS on the Deck
// (Linux), Windows on the Windows build.
bool preferWindowsAsDefault();

// Entries for the Install Source combo box.
QStringList installSourceOptions();

} // namespace Platform

#endif // PLATFORM_H
