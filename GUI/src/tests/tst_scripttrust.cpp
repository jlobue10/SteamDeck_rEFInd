// Unit tests for the helper-script trust decision (ScriptTrust): the elevated
// GUI only runs install_rEFInd.ps1 from beneath a protected root, and when it
// refuses it says why. Runs against QTemporaryDir fixtures standing in for
// Program Files and the installation directory.

#include "../scripttrust.h"

#include <QtTest>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

class TestScriptTrust : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir temp;
    QString root;   // stands in for Program Files
    QString appDir; // the installation directory below it

    void writeScript(const QString &dir)
    {
        QVERIFY(QDir().mkpath(dir + "/windows"));
        QFile f(dir + "/windows/install_rEFInd.ps1");
        QVERIFY(f.open(QIODevice::WriteOnly));
        QVERIFY(f.write("Write-Host hi\n") > 0);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(temp.isValid());
        root = QFileInfo(temp.path()).canonicalFilePath() + "/Program Files";
        appDir = root + "/rEFInd_GUI";
        writeScript(appDir);
    }

    void trustedBelowProtectedRoot()
    {
        const auto v = ScriptTrust::evaluate(appDir, "install_rEFInd.ps1", {QString(), root});
        QVERIFY2(v.trusted(), qPrintable(v.detail));
        QVERIFY(v.detail.isEmpty());
        QVERIFY(v.scriptPath.endsWith("/rEFInd_GUI/windows/install_rEFInd.ps1"));
        // Casing and a trailing slash do not matter.
        QVERIFY(ScriptTrust::evaluate(appDir.toUpper() + "/", "install_rEFInd.ps1", {root.toLower()}).trusted()
                || !QFileInfo::exists(appDir.toUpper()));
    }

    void refusedOutsideProtectedRoots()
    {
        const QString elsewhere = QFileInfo(temp.path()).canonicalFilePath() + "/Downloads/rEFInd_GUI";
        writeScript(elsewhere);
        const auto v = ScriptTrust::evaluate(elsewhere, "install_rEFInd.ps1", {root});
        QVERIFY(!v.trusted());
        QVERIFY2(v.detail.contains("not inside a protected program directory"), qPrintable(v.detail));
        QVERIFY2(v.detail.contains(QDir::toNativeSeparators(elsewhere)), qPrintable(v.detail));
        QVERIFY2(v.detail.contains(QDir::toNativeSeparators(root)), qPrintable(v.detail));
    }

    void refusedWhenScriptMissing()
    {
        const QString bare = root + "/bare";
        QVERIFY(QDir().mkpath(bare + "/windows"));
        const auto v = ScriptTrust::evaluate(bare, "install_rEFInd.ps1", {root});
        QVERIFY(!v.trusted());
        QVERIFY2(v.detail.contains("is missing from"), qPrintable(v.detail));
        QVERIFY2(v.detail.contains("install_rEFInd.ps1"), qPrintable(v.detail));
    }

    void refusedWhenAppDirUnresolvable()
    {
        const auto v = ScriptTrust::evaluate(root + "/does-not-exist", "install_rEFInd.ps1", {root});
        QVERIFY(!v.trusted());
        QVERIFY2(v.detail.contains("could not be resolved"), qPrintable(v.detail));
        QVERIFY(!ScriptTrust::evaluate(QString(), "install_rEFInd.ps1", {root}).trusted());
    }

    void noProtectedRootsNamesNone()
    {
        const auto v = ScriptTrust::evaluate(appDir, "install_rEFInd.ps1", {QString()});
        QVERIFY(!v.trusted());
        QVERIFY2(v.detail.contains("none found"), qPrintable(v.detail));
    }

    void shortNameLaunchPathIsExpanded()
    {
#ifdef Q_OS_WIN
        const std::wstring wide = QDir::toNativeSeparators(appDir).toStdWString();
        wchar_t buffer[MAX_PATH] = {};
        const DWORD length = GetShortPathNameW(wide.c_str(), buffer, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            QSKIP("GetShortPathName failed for the fixture");
        const QString shortDir = QDir::fromNativeSeparators(QString::fromWCharArray(buffer, length));
        if (shortDir.compare(appDir, Qt::CaseInsensitive) == 0)
            QSKIP("this volume does not generate 8.3 names; nothing to expand");
        // Qt alone keeps the short form, which the root comparison would refuse.
        const auto qtOnly = ScriptTrust::evaluate(shortDir, "install_rEFInd.ps1", {root},
            [](const QString &p) { return QFileInfo(p).canonicalFilePath(); });
        QVERIFY(!qtOnly.trusted());
        // The default canonicalizer expands it and the install is trusted.
        const auto v = ScriptTrust::evaluate(shortDir, "install_rEFInd.ps1", {root});
        QVERIFY2(v.trusted(), qPrintable(v.detail));
        QCOMPARE(ScriptTrust::longPath(shortDir).toLower(), appDir.toLower());
#else
        QSKIP("8.3 short names are a Windows concept");
#endif
    }
};

QTEST_GUILESS_MAIN(TestScriptTrust)
#include "tst_scripttrust.moc"
