// espops — see loadoption.h for the parity rules (byte-identical between
// rEFInd_GUI and SteamDeck_rEFInd; repo specifics live in espconstants.h).
//
// theme.conf asset paths are relative to rEFInd's own directory on the ESP
// ("banner themes/<name>/background.png"), so a theme only works from a
// directory called exactly what its theme.conf says. Themes dropped in by
// hand rarely are: a GitHub zip of "ursamajor-rEFInd" unpacks as
// "ursamajor-rEFInd-master", a release tarball as "darkmini-2.2.1". Every
// asset path then dangles, and rEFInd answers a missing banner by drawing
// its own embedded logo -- stretched over the whole screen when the config
// says banner_scale fillscreen (issue #101: "no matter how I set the
// background and theme, they never take effect").
//
// retargetThemeConf() is the one fix for that, applied wherever a
// theme.conf becomes the ESP's themes/active_theme.conf: Create Config's
// staging, the boot-time theme randomizer, and the preview (which must show
// what will boot). It is a byte-level pass that touches nothing but the
// theme-directory component of an asset path, so comments, spacing and line
// endings survive unchanged.

#ifndef ESPOPS_THEMECONF_H
#define ESPOPS_THEMECONF_H

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace EspOps {

// Returns `conf` (the bytes of <themes>/<themeDir>/theme.conf) with every
// asset path that names a theme directory which does not exist re-rooted
// onto `themeDir`. `siblingDirs` lists the directories that do exist next
// to it (pass the listing of the same themes directory); a path naming one
// of those, or `themeDir` itself, is left alone -- only a dangling
// reference is rewritten. Names compare case-insensitively, as on the
// ESP's FAT filesystem. `rewritten`, when given, receives the number of
// paths changed. Returns `conf` unchanged when nothing needs rewriting,
// when `themeDir` cannot be spelled inside a config token, or when the
// file is UTF-16 (rEFInd reads those; this byte-level pass does not).
QByteArray retargetThemeConf(const QByteArray &conf, const QString &themeDir,
                             const QStringList &siblingDirs,
                             int *rewritten = nullptr);

// The distinct theme directories `conf`'s asset paths name, in order of
// first appearance. Used after a config install to tell the user when the
// active theme's files are not on the ESP.
QStringList themeDirsReferenced(const QByteArray &conf);

} // namespace EspOps

#endif // ESPOPS_THEMECONF_H
