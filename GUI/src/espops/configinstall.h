// espops — see loadoption.h for the parity rules (byte-identical between
// rEFInd_GUI and SteamDeck_rEFInd; repo specifics live in espconstants.h).
//
// The staged, publish-last config install, ported from the root config
// helper scripts (NATIVE_HELPER_DESIGN.md §2.5). Deliberately NOT atomic —
// FAT32 has no rename atomicity: every file lands as .<name>.new.<suffix>
// in its destination directory first, a stale-staging sweep runs right
// after the destination is ensured (no cleanup survives SIGKILL or power
// loss), assets and the optional themes/active_theme.conf publish before
// refind.conf, and the previous refind.conf is preserved as
// refind.conf.prev through its own same-directory staging file.

#ifndef ESPOPS_CONFIGINSTALL_H
#define ESPOPS_CONFIGINSTALL_H

#include <QString>
#include <QStringList>

namespace EspOps {

class UserFiles;

struct InstallOutcome
{
    // Exit-code contract shared with the scripts this replaces (the GUI
    // keys its dialogs off these): 0 success, 3 no rEFInd ESP, 4 read-only
    // target, 5 staging/space failure, 6 no source config. (2 — no/invalid
    // invoking user — is diagnosed by the caller before this runs.)
    int exitCode = 0;
    // Human-readable progress/diagnostic lines, shown verbatim in the
    // GUI's result dialog. English on purpose (status quo of the scripts;
    // see NATIVE_HELPER_DESIGN.md §5 on i18n).
    QStringList lines;
};

// Install the generated config set from `srcDir` (the user's
// <dataDir>/GUI) onto `refindDir` (the resolved <esp>/EFI/refind). All
// source reads go through `user`. refind.conf is mandatory and must be
// non-empty; the images and active_theme.conf are optional, but images
// alone do not count as a successful installation.
InstallOutcome installConfigSet(UserFiles &user, const QString &srcDir,
                                const QString &refindDir);

// The destination directory for one file of the set: active_theme.conf
// publishes into refindDir/themes (the generated config's include line
// points at themes/active_theme.conf), everything else lands next to
// refind.conf. Exposed for tests.
QString destDirFor(const QString &fileName, const QString &refindDir);

} // namespace EspOps

#endif // ESPOPS_CONFIGINSTALL_H
