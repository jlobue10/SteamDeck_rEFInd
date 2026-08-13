// Unit tests for the espops staged config install (parity-locked between
// rEFInd_GUI and SteamDeck_rEFInd like the code under test). Runs against a
// QTemporaryDir standing in for both the user's GUI dir and the ESP's
// EFI/refind dir, with DirectUserFiles substituting the fork/setuid reader.

#include "espops/configinstall.h"
#include "espops/userio.h"

#include <QtTest>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

using namespace EspOps;

class TestConfigInstall : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir src;
    QTemporaryDir esp;
    DirectUserFiles files;

    QString srcPath(const QString &name) const { return src.path() + '/' + name; }
    QString refindDir() const { return esp.path() + "/EFI/refind"; }

    void writeSrc(const QString &name, const QByteArray &content)
    {
        QFile f(srcPath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(content), qint64(content.size()));
    }

    QByteArray readBack(const QString &rel)
    {
        QFile f(refindDir() + '/' + rel);
        if (!f.open(QIODevice::ReadOnly))
            return QByteArray();
        return f.readAll();
    }

private slots:
    void init()
    {
        // Fresh directories per test.
        QVERIFY(src.isValid());
        QVERIFY(esp.isValid());
        for (QTemporaryDir *d : {&src, &esp}) {
            QDir dir(d->path());
            dir.removeRecursively();
            QVERIFY(QDir().mkpath(d->path()));
        }
    }

    void installsFullSet()
    {
        writeSrc("refind.conf", "timeout 5\n");
        writeSrc("background.png", "PNG1");
        writeSrc("os_icon1.png", "PNG2");
        writeSrc("active_theme.conf", "banner themes/wave/bg.png\n");

        const InstallOutcome o = installConfigSet(files, src.path(), refindDir());
        QCOMPARE(o.exitCode, 0);
        QVERIFY(o.lines.join('\n').contains("Installed 4 file(s)"));
        QCOMPARE(readBack("refind.conf"), QByteArrayLiteral("timeout 5\n"));
        QCOMPARE(readBack("background.png"), QByteArrayLiteral("PNG1"));
        QCOMPARE(readBack("os_icon1.png"), QByteArrayLiteral("PNG2"));
        // active_theme.conf publishes into the themes/ subdirectory.
        QCOMPARE(readBack("themes/active_theme.conf"),
                 QByteArrayLiteral("banner themes/wave/bg.png\n"));
        QVERIFY(!QFile::exists(refindDir() + "/active_theme.conf"));
        // No staging leftovers.
        const QStringList leftovers = QDir(refindDir()).entryList(
            {QStringLiteral(".*.new.*")}, QDir::Files | QDir::Hidden);
        QVERIFY2(leftovers.isEmpty(), qPrintable(leftovers.join(' ')));
    }

    void missingConfigFailsWith6()
    {
        writeSrc("background.png", "PNG");
        const InstallOutcome o = installConfigSet(files, src.path(), refindDir());
        QCOMPARE(o.exitCode, 6);
        QVERIFY(o.lines.first().contains("No non-empty refind.conf"));
        // Images alone must not land either.
        QVERIFY(!QFile::exists(refindDir() + "/background.png"));
    }

    void emptyConfigFailsWith6()
    {
        writeSrc("refind.conf", "");
        const InstallOutcome o = installConfigSet(files, src.path(), refindDir());
        QCOMPARE(o.exitCode, 6);
    }

    void imagesAreOptional()
    {
        writeSrc("refind.conf", "timeout 5\n");
        const InstallOutcome o = installConfigSet(files, src.path(), refindDir());
        QCOMPARE(o.exitCode, 0);
        QVERIFY(o.lines.join('\n').contains("Installed 1 file(s)"));
    }

    void previousConfigPreserved()
    {
        QVERIFY(QDir().mkpath(refindDir()));
        {
            QFile old(refindDir() + "/refind.conf");
            QVERIFY(old.open(QIODevice::WriteOnly));
            old.write("old config\n");
        }
        writeSrc("refind.conf", "new config\n");
        const InstallOutcome o = installConfigSet(files, src.path(), refindDir());
        QCOMPARE(o.exitCode, 0);
        QCOMPARE(readBack("refind.conf"), QByteArrayLiteral("new config\n"));
        QCOMPARE(readBack("refind.conf.prev"), QByteArrayLiteral("old config\n"));
    }

    void staleStagingSwept()
    {
        QVERIFY(QDir().mkpath(refindDir() + "/themes"));
        // Leftovers from a hypothetical interrupted run...
        for (const QString &p :
             {refindDir() + "/.refind.conf.new.abc123",
              refindDir() + "/.background.png.new.zz",
              refindDir() + "/themes/.active_theme.conf.new.q"}) {
            QFile f(p);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("stale");
        }
        // ...and a live dotfile that must NOT be swept (wrong shape).
        {
            QFile f(refindDir() + "/.refind.conf.mine");
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("keep");
        }
        writeSrc("refind.conf", "cfg\n");
        const InstallOutcome o = installConfigSet(files, src.path(), refindDir());
        QCOMPARE(o.exitCode, 0);
        QVERIFY(!QFile::exists(refindDir() + "/.refind.conf.new.abc123"));
        QVERIFY(!QFile::exists(refindDir() + "/.background.png.new.zz"));
        QVERIFY(!QFile::exists(refindDir() + "/themes/.active_theme.conf.new.q"));
        QVERIFY(QFile::exists(refindDir() + "/.refind.conf.mine"));
    }

    void unwritableTargetFails()
    {
#ifdef Q_OS_UNIX
        if (::geteuid() == 0)
            QSKIP("root ignores directory permissions");
        writeSrc("refind.conf", "cfg\n");
        QVERIFY(QDir().mkpath(refindDir()));
        QFile::setPermissions(refindDir(),
                              QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        const InstallOutcome o = installConfigSet(files, src.path(), refindDir());
        QFile::setPermissions(refindDir(),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                  | QFileDevice::ExeOwner);
        QVERIFY(o.exitCode == 4 || o.exitCode == 5);
#else
        QSKIP("POSIX permission test");
#endif
    }

    void destDirRouting()
    {
        QCOMPARE(destDirFor("refind.conf", "/x"), QStringLiteral("/x"));
        QCOMPARE(destDirFor("background.png", "/x"), QStringLiteral("/x"));
        QCOMPARE(destDirFor("active_theme.conf", "/x"), QStringLiteral("/x/themes"));
    }
};

QTEST_APPLESS_MAIN(TestConfigInstall)
#include "tst_configinstall.moc"
