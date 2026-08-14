// Unit tests for the espops staged config install (parity-locked between
// rEFInd_GUI and SteamDeck_rEFInd like the code under test). Runs against a
// QTemporaryDir standing in for both the user's GUI dir and the ESP's
// EFI/refind dir, with DirectUserFiles substituting the fork/setuid reader.

#include "espops/configinstall.h"
#include "espops/espconstants.h"
#include "espops/userio.h"

#include <QCryptographicHash>
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

    void originSidecarWritten()
    {
        writeSrc("refind.conf", "timeout 5\n");
        const InstallOutcome o = installConfigSet(files, src.path(), refindDir());
        QCOMPARE(o.exitCode, 0);
        const QByteArray origin = readBack("refind.conf.origin");
        QVERIFY(origin.contains("product=" + QByteArray(kProductName)));
        QVERIFY(origin.contains("platform=" + QByteArray(originPlatformName())));
        // sha256 of the published bytes, so the next install can tell a
        // hand edit from this publish.
        const QByteArray sha =
            QCryptographicHash::hash(QByteArrayLiteral("timeout 5\n"),
                                     QCryptographicHash::Sha256)
                .toHex();
        QVERIFY2(origin.contains("sha256=" + sha), origin.constData());
        // Replacing our own untouched install stays silent.
        writeSrc("refind.conf", "timeout 10\n");
        const InstallOutcome again = installConfigSet(files, src.path(), refindDir());
        QCOMPARE(again.exitCode, 0);
        QVERIFY2(!again.lines.join('\n').contains("Note:"),
                 qPrintable(again.lines.join('\n')));
    }

    void crossInstallerReplacementNoted()
    {
        QVERIFY(QDir().mkpath(refindDir()));
        {
            QFile old(refindDir() + "/refind.conf");
            QVERIFY(old.open(QIODevice::WriteOnly));
            old.write("their config\n");
        }
        const QByteArray theirSha =
            QCryptographicHash::hash(QByteArrayLiteral("their config\n"),
                                     QCryptographicHash::Sha256)
                .toHex();
        {
            QFile origin(refindDir() + "/refind.conf.origin");
            QVERIFY(origin.open(QIODevice::WriteOnly));
            origin.write("# comment\nproduct=Other_GUI\nplatform=Windows\n"
                         "installed=2026-08-14T10:15:00\nsha256="
                         + theirSha + "\n");
        }
        writeSrc("refind.conf", "our config\n");
        const InstallOutcome o = installConfigSet(files, src.path(), refindDir());
        QCOMPARE(o.exitCode, 0);
        const QString all = o.lines.join('\n');
        QVERIFY2(all.contains("installed by Other_GUI (Windows) "
                              "on 2026-08-14T10:15:00"),
                 qPrintable(all));
        // The sidecar now names this build as the installer.
        QVERIFY(readBack("refind.conf.origin")
                    .contains("product=" + QByteArray(kProductName)));
    }

    void handEditedReplacementNoted()
    {
        QVERIFY(QDir().mkpath(refindDir()));
        {
            QFile old(refindDir() + "/refind.conf");
            QVERIFY(old.open(QIODevice::WriteOnly));
            old.write("edited by hand\n");
        }
        {
            QFile origin(refindDir() + "/refind.conf.origin");
            QVERIFY(origin.open(QIODevice::WriteOnly));
            // Our own product/platform, but a sha that no longer matches
            // the live bytes.
            origin.write(QByteArray("product=") + kProductName + "\nplatform="
                         + originPlatformName() + "\nsha256=" + QByteArray(64, '0')
                         + "\n");
        }
        writeSrc("refind.conf", "new\n");
        const InstallOutcome o = installConfigSet(files, src.path(), refindDir());
        QCOMPARE(o.exitCode, 0);
        QVERIFY2(o.lines.join('\n').contains("changed by hand or by another tool"),
                 qPrintable(o.lines.join('\n')));
    }

    void staleStagingSwept()
    {
        QVERIFY(QDir().mkpath(refindDir() + "/themes"));
        // Leftovers from a hypothetical interrupted run...
        for (const QString &p :
             {refindDir() + "/.refind.conf.new.abc123",
              refindDir() + "/.background.png.new.zz",
              refindDir() + "/.refind.conf.origin.new.k7",
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
        QVERIFY(!QFile::exists(refindDir() + "/.refind.conf.origin.new.k7"));
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
