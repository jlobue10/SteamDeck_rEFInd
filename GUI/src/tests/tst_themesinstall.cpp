// Unit tests for the espops staged themes install (parity-locked between
// rEFInd_GUI and SteamDeck_rEFInd like the code under test).

#include "espops/themesinstall.h"
#include "espops/userio.h"

#include <QtTest>

using namespace EspOps;

class TestThemesInstall : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir src;
    QTemporaryDir esp;
    DirectUserFiles files;

    QString refindDir() const { return esp.path() + "/EFI/refind"; }
    QString themesDir() const { return refindDir() + "/themes"; }

    void writeFile(const QString &path, const QByteArray &content)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(content), qint64(content.size()));
    }

    QByteArray readBack(const QString &rel)
    {
        QFile f(themesDir() + '/' + rel);
        if (!f.open(QIODevice::ReadOnly))
            return QByteArray();
        return f.readAll();
    }

private slots:
    void init()
    {
        QVERIFY(src.isValid());
        QVERIFY(esp.isValid());
        for (QTemporaryDir *d : {&src, &esp}) {
            QDir dir(d->path());
            dir.removeRecursively();
            QVERIFY(QDir().mkpath(d->path()));
        }
    }

    void installsThemes()
    {
        writeFile(src.path() + "/wave/theme.conf", "banner themes/wave/bg.png\n");
        writeFile(src.path() + "/wave/bg.png", "PNG-A");
        writeFile(src.path() + "/wave/icons/os_linux.png", "PNG-B");
        writeFile(src.path() + "/Matrix/theme.conf", "banner b.png\n");
        // Not themes: hidden dir, dir without theme.conf, empty theme.conf.
        writeFile(src.path() + "/.hidden/theme.conf", "x");
        writeFile(src.path() + "/notatheme/readme.txt", "x");
        writeFile(src.path() + "/empty/theme.conf", "");

        const InstallOutcome o = installThemeSet(files, src.path(), refindDir());
        QCOMPARE(o.exitCode, 0);
        QVERIFY(o.lines.join('\n').contains("Installed 2 theme(s)"));
        QCOMPARE(readBack("wave/theme.conf"),
                 QByteArrayLiteral("banner themes/wave/bg.png\n"));
        QCOMPARE(readBack("wave/icons/os_linux.png"), QByteArrayLiteral("PNG-B"));
        QCOMPARE(readBack("Matrix/theme.conf"), QByteArrayLiteral("banner b.png\n"));
        QVERIFY(!QFileInfo::exists(themesDir() + "/.hidden"));
        QVERIFY(!QFileInfo::exists(themesDir() + "/notatheme"));
        QVERIFY(!QFileInfo::exists(themesDir() + "/empty"));
        // No staging leftovers.
        const QStringList leftovers = QDir(themesDir()).entryList(
            {QStringLiteral(".install-staging-*"), QStringLiteral(".old-*")},
            QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
        QVERIFY2(leftovers.isEmpty(), qPrintable(leftovers.join(' ')));
    }

    void replacesExistingTheme()
    {
        // A pre-existing installed theme with an extra stale file must be
        // replaced wholesale, not merged.
        writeFile(themesDir() + "/wave/theme.conf", "old\n");
        writeFile(themesDir() + "/wave/stale.png", "STALE");
        writeFile(src.path() + "/wave/theme.conf", "new\n");

        const InstallOutcome o = installThemeSet(files, src.path(), refindDir());
        QCOMPARE(o.exitCode, 0);
        QCOMPARE(readBack("wave/theme.conf"), QByteArrayLiteral("new\n"));
        QVERIFY(!QFileInfo::exists(themesDir() + "/wave/stale.png"));
    }

    void noThemesFailsWith6()
    {
        writeFile(src.path() + "/notatheme/readme.txt", "x");
        const InstallOutcome o = installThemeSet(files, src.path(), refindDir());
        QCOMPARE(o.exitCode, 6);
        QVERIFY(o.lines.first().contains("No themes were found"));
    }

    void staleStagingSwept()
    {
        writeFile(themesDir() + "/.install-staging-zzz/wave/theme.conf", "stale");
        writeFile(themesDir() + "/.old-wave/theme.conf", "stale");
        writeFile(src.path() + "/wave/theme.conf", "cfg\n");
        const InstallOutcome o = installThemeSet(files, src.path(), refindDir());
        QCOMPARE(o.exitCode, 0);
        QVERIFY(!QFileInfo::exists(themesDir() + "/.install-staging-zzz"));
        QVERIFY(!QFileInfo::exists(themesDir() + "/.old-wave"));
    }

    void otherInstalledThemesUntouched()
    {
        writeFile(themesDir() + "/UserTheme/theme.conf", "mine\n");
        writeFile(src.path() + "/wave/theme.conf", "cfg\n");
        const InstallOutcome o = installThemeSet(files, src.path(), refindDir());
        QCOMPARE(o.exitCode, 0);
        QCOMPARE(readBack("UserTheme/theme.conf"), QByteArrayLiteral("mine\n"));
    }
};

QTEST_APPLESS_MAIN(TestThemesInstall)
#include "tst_themesinstall.moc"
