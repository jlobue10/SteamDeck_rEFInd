#include "mainwindow.h"
#include <QApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(QStringLiteral(":/SteamDeck_rEFInd.png")));

    // Qt's own strings (standard dialog buttons etc.) — shipped with the Qt
    // runtime, not with this app, so a missing file just means English.
    QTranslator qtTranslator;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QString qtTrDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
#else
    const QString qtTrDir = QLibraryInfo::location(QLibraryInfo::TranslationsPath);
#endif
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"), qtTrDir)
        // Deployed Windows builds have no Qt install; windeployqt copies the
        // qtbase_*.qm files into translations/ next to the exe instead.
        || qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                             QCoreApplication::applicationDirPath()
                                 + QStringLiteral("/translations")))
        a.installTranslator(&qtTranslator);

    // App strings: the QLocale overload walks the locale's fallback chain
    // (es_MX -> es -> en), unlike a filename built from QLocale::name().
    // Qt 6 builds embed the .qm files under :/i18n (qt_add_translations in
    // CMakeLists.txt); Qt 5 builds only generate them into translations/
    // next to the binary, hence the second location.
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("SteamDeck_rEFInd"), QStringLiteral("_"),
                        QStringLiteral(":/i18n"))
        || translator.load(QLocale(), QStringLiteral("SteamDeck_rEFInd"), QStringLiteral("_"),
                           QCoreApplication::applicationDirPath()
                               + QStringLiteral("/translations")))
        a.installTranslator(&translator);

    MainWindow w;
    w.show();
    return a.exec();
}
