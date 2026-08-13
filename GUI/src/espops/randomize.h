// espops — see loadoption.h for the parity rules (byte-identical between
// rEFInd_GUI and SteamDeck_rEFInd; repo specifics live in espconstants.h).
//
// The boot-time randomizer payloads (NATIVE_HELPER_DESIGN.md §2.5), run by
// the systemd units on Linux and the Scheduled Tasks on Windows. These are
// cosmetic services: content and environment problems emit a warning line
// and return 0 — a red failed unit for a missing background would be worse
// than the symptom. Nonzero is reserved for internal errors.
//
// The native port unifies the two repos' script variants (which had
// drifted): the theme randomizer keeps SteamDeck_rEFInd's
// include-line gate (exit 0 silently unless refind.conf actually includes
// themes/active_theme.conf — theming off is the normal state) and staged
// rename publish, plus rEFInd_GUI's anti-repeat compare against the
// current active_theme.conf. The background randomizer adopts the hardened
// SteamDeck_rEFInd semantics in both repos: the backgrounds directory
// comes from the root-owned pointer file (kBackgroundDirPointer, written
// by the installer; validated and read as data, never sourced), and every
// byte of user content is read in the directory owner's context with a
// size cap and a PNG signature check before anything touches the ESP.

#ifndef ESPOPS_RANDOMIZE_H
#define ESPOPS_RANDOMIZE_H

#include <QString>
#include <QStringList>

namespace EspOps {

class UserFiles;

// Replace <refindDir>/themes/active_theme.conf with a random installed
// theme's theme.conf. Takes no user-writable input at all — candidates are
// the ESP's own root-owned themes/*/theme.conf.
int randomizeTheme(const QString &refindDir, QStringList *warnings);

// Replace <refindDir>/background.png with a random PNG from the
// user's backgrounds directory (resolved via the pointer file; every read
// of user content goes through `user`-context IO built from the directory
// owner).
int randomizeBackground(const QString &refindDir, QStringList *warnings);

} // namespace EspOps

#endif // ESPOPS_RANDOMIZE_H
