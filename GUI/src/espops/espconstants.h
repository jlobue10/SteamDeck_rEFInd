// Repo-specific constants for the parity-locked espops/ code and the helper
// binary. This is the ONE espops file that differs between rEFInd_GUI and
// SteamDeck_rEFInd — everything else in espops/ and helper/ must stay
// byte-identical between the repos (same workflow as the osdetect_* files).

#ifndef ESPOPS_ESPCONSTANTS_H
#define ESPOPS_ESPCONSTANTS_H

namespace EspOps {

// Product name as used in messages and the helper's usage text.
constexpr const char kProductName[] = "SteamDeck_rEFInd";

// Data dir name under ~/.local (Linux) / %LOCALAPPDATA% (Windows).
constexpr const char kDataDirName[] = "SteamDeck_rEFInd";

// Root-owned install location of the helper binary on Linux — the path the
// sudoers NOPASSWD lines and the systemd randomizer units point at. Lives
// on the persistent /etc overlay so it survives SteamOS updates (unlike
// /usr/bin, which the A/B rootfs swap replaces wholesale).
constexpr const char kHelperEtcPath[] = "/etc/SteamDeck_rEFInd/SteamDeck_rEFInd_helper";

// The user-facing installer script, named in "re-run the installer"
// diagnostics.
constexpr const char kInstallerName[] = "install-GUI.sh";

// Root-owned pointer file naming the user's backgrounds directory, written
// by the installer and read (as data, never sourced) by the background
// randomizer. Lives on the persistent /etc overlay.
constexpr const char kBackgroundDirPointer[] = "/etc/SteamDeck_rEFInd/background-dir";

// Windows only. The at-logon Scheduled Tasks this product manages: the
// suffix appended to kProductName for the task name, and the helper
// subcommand the task runs. Null-terminated. `migrate-tasks` re-points
// every one of these that already exists at the installed helper exe.
// The bootnext task is the Windows counterpart of the Deck's
// bootnext-refind.service and has no rEFInd_GUI equivalent.
struct LogonTaskSpec
{
    const char *nameSuffix;
    const char *subcommand;
};
inline constexpr LogonTaskSpec kLogonTasks[] = {
    {"_bg_randomizer", "randomize-background"},
    {"_theme_randomizer", "randomize-theme"},
    {"_bootnext", "bootnext"},
    {nullptr, nullptr},
};

// Windows only. Task names registered by earlier versions, whose action ran
// a .ps1 wrapper that no longer ships. On upgrade each one that still exists
// is re-registered under its current name and then unregistered, so an
// enabled feature survives instead of silently failing at every logon.
// "rEFInd Boot Sequence" is the pre-2.3.1 bcdedit task from the Dual Boot
// Fix zip that the bootnext task superseded. Null-terminated.
struct LegacyLogonTask
{
    const char *oldName;
    const char *newNameSuffix;
    const char *subcommand;
};
inline constexpr LegacyLogonTask kLegacyLogonTasks[] = {
    {"rEFInd_bg_randomizer", "_bg_randomizer", "randomize-background"},
    {"rEFInd Boot Sequence", "_bootnext", "bootnext"},
    {nullptr, nullptr, nullptr},
};

// "Running system's ESP" fallback mountpoints, in preference order, for
// when NVRAM resolution finds nothing (first install, not yet booted
// through rEFInd). /esp first on the Deck. Null-terminated.
inline constexpr const char *kSystemEspMountpoints[] = {
    "/esp",
    "/boot/efi",
    "/efi",
    "/boot",
    nullptr,
};

} // namespace EspOps

#endif // ESPOPS_ESPCONSTANTS_H
