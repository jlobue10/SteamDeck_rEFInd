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
