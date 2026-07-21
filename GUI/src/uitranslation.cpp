#include "uitranslation.h"
#include "platform.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace UiTranslation {

static QString settingsFile()
{
    // Same INI MainWindow uses (the filename is rEFInd_GUI.ini in both repos).
    return Platform::dataDir() + QStringLiteral("/GUI/rEFInd_GUI.ini");
}

static QString catalogPrefix()
{
    return QCoreApplication::applicationName();
}

void apply(const QString &langCode)
{
    // Static so re-applying replaces rather than stacks translators; removing
    // an installed translator is what makes Qt broadcast LanguageChange.
    static QTranslator appTr;
    static QTranslator qtTr;
    QCoreApplication *app = QCoreApplication::instance();
    app->removeTranslator(&appTr);
    app->removeTranslator(&qtTr);

    const QLocale locale = langCode.isEmpty() ? QLocale() : QLocale(langCode);

    // Qt's own strings (standard dialog buttons etc.): Qt installation first,
    // then windeployqt's translations/ next to the exe, then the copies
    // embedded at build time (SteamOS has no qt6-translations package).
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QString qtTrDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
#else
    const QString qtTrDir = QLibraryInfo::location(QLibraryInfo::TranslationsPath);
#endif
    const QString sep = QStringLiteral("_");
    const QString qtBase = QStringLiteral("qtbase");
    if (qtTr.load(locale, qtBase, sep, qtTrDir)
        || qtTr.load(locale, qtBase, sep,
                     QCoreApplication::applicationDirPath() + QStringLiteral("/translations"))
        || qtTr.load(locale, qtBase, sep, QStringLiteral(":/i18n_qtbase")))
        app->installTranslator(&qtTr);

    // App strings: embedded (Qt 6) first, translations/ next to the binary
    // second (Qt 5 builds embed nothing).
    if (appTr.load(locale, catalogPrefix(), sep, QStringLiteral(":/i18n"))
        || appTr.load(locale, catalogPrefix(), sep,
                      QCoreApplication::applicationDirPath() + QStringLiteral("/translations")))
        app->installTranslator(&appTr);
}

QStringList availableLanguages()
{
    const QString prefix = catalogPrefix() + QLatin1Char('_');
    QStringList codes;
    const QStringList entries =
        QDir(QStringLiteral(":/i18n")).entryList({prefix + QStringLiteral("*.qm")});
    for (const QString &entry : entries) {
        QString code = entry.mid(prefix.length());
        code.chop(3); // ".qm"
        codes << code;
    }
    codes.sort();
    return codes;
}

QString saved()
{
    QSettings settings(settingsFile(), QSettings::IniFormat);
    return settings.value(QStringLiteral("Language/UiLanguage")).toString();
}

void save(const QString &langCode)
{
    QSettings settings(settingsFile(), QSettings::IniFormat);
    if (langCode.isEmpty())
        settings.remove(QStringLiteral("Language/UiLanguage"));
    else
        settings.setValue(QStringLiteral("Language/UiLanguage"), langCode);
}

} // namespace UiTranslation
