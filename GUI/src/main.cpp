#include "mainwindow.h"
#include "platform.h"
#include "uitranslation.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    // The name doubles as the translation-catalog prefix (see uitranslation.cpp).
    a.setApplicationName(QStringLiteral("SteamDeck_rEFInd"));
    a.setWindowIcon(QIcon(QStringLiteral(":/SteamDeck_rEFInd.png")));
    Platform::prepareDataDir();

    // Saved override from the INI, or the system locale. Translator loading,
    // fallback order, and the qtbase catalogs all live in UiTranslation.
    UiTranslation::apply(UiTranslation::saved());

    // Qt queries this key from the installed translators to decide whether to
    // mirror the whole widget layout; the RTL language files (ar, fa, ur)
    // translate it to "RTL". The call itself is only an lupdate anchor so the
    // key survives .ts regeneration.
    (void)QCoreApplication::translate("QGuiApplication", "QT_LAYOUT_DIRECTION");

    MainWindow w;
    w.show();
    return a.exec();
}
