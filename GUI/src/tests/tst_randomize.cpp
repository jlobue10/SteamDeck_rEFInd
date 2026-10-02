// Unit tests for the espops theme randomizer (parity-locked between
// rEFInd_GUI and SteamDeck_rEFInd like the code under test). The background
// randomizer's Linux path reads a fixed /etc pointer file and drops
// privileges, so it is exercised on hardware instead; randomizeTheme takes
// only the resolved refind dir and is fully testable here.

#include "espops/randomize.h"

#include <QtTest>

using namespace EspOps;

class TestRandomize : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir esp;

    QString refindDir() const { return esp.path() + "/EFI/refind"; }

    void writeFile(const QString &path, const QByteArray &content)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(content), qint64(content.size()));
    }

    QByteArray active()
    {
        QFile f(refindDir() + "/themes/active_theme.conf");
        if (!f.open(QIODevice::ReadOnly))
            return QByteArray();
        return f.readAll();
    }

private slots:
    void init()
    {
        QVERIFY(esp.isValid());
        QDir(esp.path()).removeRecursively();
        QVERIFY(QDir().mkpath(esp.path()));
    }

    void silentWithoutIncludeLine()
    {
        // Theming off is the normal state: no include line means exit 0
        // with NO warning and nothing written.
        writeFile(refindDir() + "/refind.conf", "timeout 5\n");
        writeFile(refindDir() + "/themes/wave/theme.conf", "banner a\n");
        QStringList warnings;
        QCOMPARE(randomizeTheme(refindDir(), &warnings), 0);
        QVERIFY(warnings.isEmpty());
        QVERIFY(active().isEmpty());
    }

    void picksATheme()
    {
        writeFile(refindDir() + "/refind.conf",
                  "timeout 5\ninclude themes/active_theme.conf\n");
        writeFile(refindDir() + "/themes/wave/theme.conf", "banner wave\n");
        QStringList warnings;
        QCOMPARE(randomizeTheme(refindDir(), &warnings), 0);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        QCOMPARE(active(), QByteArrayLiteral("banner wave\n"));
        // No staging leftovers.
        const QStringList stale = QDir(refindDir() + "/themes").entryList(
            {QStringLiteral(".active_theme.conf.*")}, QDir::Files | QDir::Hidden);
        QVERIFY2(stale.isEmpty(), qPrintable(stale.join(' ')));
    }

    void antiRepeatFlipsBetweenTwoThemes()
    {
        writeFile(refindDir() + "/refind.conf",
                  "include themes/active_theme.conf\n");
        writeFile(refindDir() + "/themes/wave/theme.conf", "banner wave\n");
        writeFile(refindDir() + "/themes/Matrix/theme.conf", "banner matrix\n");
        // With exactly two themes the anti-repeat makes every run flip to
        // the other one — deterministic despite the random pick.
        QCOMPARE(randomizeTheme(refindDir(), nullptr), 0);
        QByteArray previous = active();
        QVERIFY(!previous.isEmpty());
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(randomizeTheme(refindDir(), nullptr), 0);
            const QByteArray current = active();
            QVERIFY2(current != previous, qPrintable(QString::number(i)));
            previous = current;
        }
    }

    void warnsWhenNoThemesInstalled()
    {
        writeFile(refindDir() + "/refind.conf",
                  "include themes/active_theme.conf\n");
        QVERIFY(QDir().mkpath(refindDir() + "/themes"));
        QStringList warnings;
        QCOMPARE(randomizeTheme(refindDir(), &warnings), 0);
        QCOMPARE(warnings.size(), 1);
        QVERIFY(warnings.first().contains("no themes found"));
    }

    void emptyThemeConfIgnored()
    {
        writeFile(refindDir() + "/refind.conf",
                  "include themes/active_theme.conf\n");
        writeFile(refindDir() + "/themes/broken/theme.conf", "");
        writeFile(refindDir() + "/themes/good/theme.conf", "banner g\n");
        QCOMPARE(randomizeTheme(refindDir(), nullptr), 0);
        QCOMPARE(active(), QByteArrayLiteral("banner g\n"));
    }

    void misnamedThemeDirRetargeted()
    {
        // A hand-added theme installed under its download name: theme.conf
        // says themes/ursamajor-rEFInd/..., the tree is
        // themes/ursamajor-rEFInd-master/. The published active_theme.conf
        // must point at the directory that exists.
        writeFile(refindDir() + "/refind.conf",
                  "include themes/active_theme.conf\n");
        writeFile(refindDir() + "/themes/ursamajor-rEFInd-master/theme.conf",
                  "banner themes/ursamajor-rEFInd/background.png\n"
                  "showtools shutdown\n");
        QStringList warnings;
        QCOMPARE(randomizeTheme(refindDir(), &warnings), 0);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        QCOMPARE(active(),
                 QByteArrayLiteral(
                     "banner themes/ursamajor-rEFInd-master/background.png\n"
                     "showtools shutdown\n"));
        // The installed theme.conf itself is never modified.
        QFile conf(refindDir() + "/themes/ursamajor-rEFInd-master/theme.conf");
        QVERIFY(conf.open(QIODevice::ReadOnly));
        QVERIFY(conf.readAll().contains("themes/ursamajor-rEFInd/background.png"));
    }

    void antiRepeatSeesRetargetedContent()
    {
        // The anti-repeat compare must match the active file against what a
        // candidate would be published as, not against its raw theme.conf --
        // otherwise a retargeted theme never counts as "already active" and
        // can be picked twice in a row.
        writeFile(refindDir() + "/refind.conf",
                  "include themes/active_theme.conf\n");
        writeFile(refindDir() + "/themes/x-master/theme.conf",
                  "banner themes/x/bg.png\n");
        writeFile(refindDir() + "/themes/wave/theme.conf",
                  "banner themes/wave/bg.png\n");
        QByteArray previous;
        for (int i = 0; i < 6; ++i) {
            QCOMPARE(randomizeTheme(refindDir(), nullptr), 0);
            const QByteArray current = active();
            QVERIFY2(current != previous, qPrintable(QString::number(i)));
            QVERIFY(!current.contains("themes/x/"));
            previous = current;
        }
    }
};

QTEST_APPLESS_MAIN(TestRandomize)
#include "tst_randomize.moc"
