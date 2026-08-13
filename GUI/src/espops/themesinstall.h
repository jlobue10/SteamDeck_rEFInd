// espops — see loadoption.h for the parity rules (byte-identical between
// rEFInd_GUI and SteamDeck_rEFInd; repo specifics live in espconstants.h).
//
// The staged themes install, ported from the root themes helper scripts
// (NATIVE_HELPER_DESIGN.md §2.5). Copy strategy: the whole selected tree is
// copied into a private staging directory ON the ESP first (same
// filesystem), then each theme directory is swapped into place by renames
// (live -> .old-<name>, staged -> live, drop .old). A failure before the
// swap loop leaves the live tree completely untouched; a failure mid-swap
// leaves every theme either fully old or fully new, never half-copied.
// Enumeration and every byte read run in the invoking user's context
// (userio.h) — symlinks are skipped outright (vfat cannot hold them), and
// relative paths are validated against '..'/absolute escapes before any
// root-side write.

#ifndef ESPOPS_THEMESINSTALL_H
#define ESPOPS_THEMESINSTALL_H

#include "configinstall.h" // InstallOutcome

#include <QString>

namespace EspOps {

class UserFiles;

// Install every first-level directory of `srcThemesDir` that carries a
// non-empty theme.conf onto `refindDir`/themes. Exit codes: 0 success,
// 4 read-only target, 5 staging/space failure, 6 no themes found (2 and 3
// are diagnosed by the caller, as with the config install).
InstallOutcome installThemeSet(UserFiles &user, const QString &srcThemesDir,
                               const QString &refindDir);

} // namespace EspOps

#endif // ESPOPS_THEMESINSTALL_H
