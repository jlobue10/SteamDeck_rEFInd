#ifndef UITRANSLATION_H
#define UITRANSLATION_H

#include <QString>
#include <QStringList>

// Runtime language selection. The app's .qm catalogs are embedded under
// ":/i18n" as "<applicationName>_<code>.qm"; Qt's own qtbase catalogs are
// searched in the Qt installation, next to the binary (windeployqt layout),
// and finally in the ":/i18n_qtbase" copies embedded at build time (SteamOS
// ships no qt6-translations package).
namespace UiTranslation {

// Installs the app + qtbase translators for langCode ("" = system locale).
// Safe to call again at runtime: replaces the previous translators, which
// makes Qt deliver a LanguageChange event to all widgets.
void apply(const QString &langCode);

// Language codes with an embedded catalog (from ":/i18n"), sorted.
QStringList availableLanguages();

// The user's saved override from the settings INI ("" = follow the system).
QString saved();
void save(const QString &langCode);

} // namespace UiTranslation

#endif // UITRANSLATION_H
