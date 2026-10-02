// Unit tests for the espops theme.conf path re-rooting (parity-locked
// between rEFInd_GUI and SteamDeck_rEFInd like the code under test). Pure
// byte transforms: no filesystem, no ESP.

#include "espops/themeconf.h"

#include <QtTest>

using namespace EspOps;

class TestThemeConf : public QObject
{
    Q_OBJECT

private slots:
    void matchingDirUntouched()
    {
        const QByteArray conf = "banner themes/wave/bg.png\n"
                                "icons_dir themes/wave/icons\n";
        int n = -1;
        QCOMPARE(retargetThemeConf(conf, "wave", {"wave"}, &n), conf);
        QCOMPARE(n, 0);
    }

    void downloadSuffixRetargeted()
    {
        // Issue #101: the upstream ursamajor theme.conf, unpacked from a
        // GitHub zip into "ursamajor-rEFInd-master".
        const QByteArray conf =
            "# Ursa Major rEFInd theme\n"
            "hideui singleuser,hints,arrows,label,badges\n"
            "icons_dir themes/ursamajor-rEFInd/icons\n"
            "banner themes/ursamajor-rEFInd/background.png\n"
            "banner_scale fillscreen\n"
            "selection_big   themes/ursamajor-rEFInd/selection_big.png\n"
            "selection_small themes/ursamajor-rEFInd/selection_small.png\n"
            "showtools shutdown\n";
        const QByteArray want =
            "# Ursa Major rEFInd theme\n"
            "hideui singleuser,hints,arrows,label,badges\n"
            "icons_dir themes/ursamajor-rEFInd-master/icons\n"
            "banner themes/ursamajor-rEFInd-master/background.png\n"
            "banner_scale fillscreen\n"
            "selection_big   themes/ursamajor-rEFInd-master/selection_big.png\n"
            "selection_small themes/ursamajor-rEFInd-master/selection_small.png\n"
            "showtools shutdown\n";
        int n = 0;
        QCOMPARE(retargetThemeConf(conf, "ursamajor-rEFInd-master",
                                   {"ursamajor-rEFInd-master", "wave"}, &n),
                 want);
        QCOMPARE(n, 4);
    }

    void existingSiblingLeftAlone()
    {
        // The named directory exists (the user has both the renamed and the
        // "-master" tree): the reference works as written.
        const QByteArray conf = "banner themes/ursamajor-rEFInd/background.png\n";
        int n = -1;
        QCOMPARE(retargetThemeConf(conf, "ursamajor-rEFInd-master",
                                   {"ursamajor-rEFInd", "ursamajor-rEFInd-master"},
                                   &n),
                 conf);
        QCOMPARE(n, 0);
    }

    void namesCompareCaseInsensitively()
    {
        // FAT resolves themes/Wave and themes/wave to the same directory.
        const QByteArray conf = "banner themes/Wave/bg.png\n";
        QCOMPARE(retargetThemeConf(conf, "wave", {}), conf);
        QCOMPARE(retargetThemeConf(conf, "other", {"WAVE"}), conf);
    }

    void commentsAndOtherDirectivesUntouched()
    {
        const QByteArray conf =
            "#banner themes/old/bg.png\n"
            "  # icons_dir themes/old/icons\n"
            "showtools shutdown,reboot\n"
            "dont_scan_dirs themes/old/x\n"
            "include themes/old/extra.conf\n"
            "\n";
        int n = -1;
        QCOMPARE(retargetThemeConf(conf, "new", {}, &n), conf);
        QCOMPARE(n, 0);
    }

    void absoluteAndBackslashSpellings()
    {
        const QByteArray conf =
            "banner /EFI/refind/themes/old/bg.png\n"
            "selection_big EFI\\refind\\Themes\\old\\sel.png\n";
        const QByteArray want =
            "banner /EFI/refind/themes/new/bg.png\n"
            "selection_big EFI\\refind\\Themes\\new\\sel.png\n";
        QCOMPARE(retargetThemeConf(conf, "new", {}), want);
    }

    void lineEndingsAndSpacingPreserved()
    {
        const QByteArray conf = "\tbanner\t themes/old/bg.png  \r\n"
                                "icons_dir themes/old/icons"; // no final newline
        const QByteArray want = "\tbanner\t themes/new/bg.png  \r\n"
                                "icons_dir themes/new/icons";
        int n = 0;
        QCOMPARE(retargetThemeConf(conf, "new", {}, &n), want);
        QCOMPARE(n, 2);
    }

    void fileDirectlyInThemesIsNotADirectory()
    {
        // "themes/bg.png" names a file kept in themes/ itself; only
        // icons_dir may end at the directory component.
        const QByteArray conf = "banner themes/bg.png\n"
                                "icons_dir themes/old\n";
        const QByteArray want = "banner themes/bg.png\n"
                                "icons_dir themes/new\n";
        QCOMPARE(retargetThemeConf(conf, "new", {}), want);
    }

    void quotedPathsAndNamesWithSpaces()
    {
        const QByteArray conf = "banner \"themes/old theme/bg.png\"\n"
                                "icons_dir themes/old/icons\n";
        const QByteArray want = "banner \"themes/my theme/bg.png\"\n"
                                "icons_dir \"themes/my theme/icons\"\n";
        QCOMPARE(retargetThemeConf(conf, "my theme", {}), want);
    }

    void unusableInputReturnedUnchanged()
    {
        const QByteArray conf = "banner themes/old/bg.png\n";
        // A name that cannot be spelled inside a config token.
        QCOMPARE(retargetThemeConf(conf, "a\"b", {}), conf);
        QCOMPARE(retargetThemeConf(conf, QString(), {}), conf);
        // UTF-16 with a BOM: rEFInd reads it, the byte-level pass skips it.
        const QByteArray utf16 = QByteArray("\xff\xfe", 2)
            + QByteArray("b\0a\0n\0n\0e\0r\0", 12);
        int n = -1;
        QCOMPARE(retargetThemeConf(utf16, "new", {}, &n), utf16);
        QCOMPARE(n, 0);
        QVERIFY(themeDirsReferenced(utf16).isEmpty());
    }

    void referencedDirsListedOnce()
    {
        const QByteArray conf =
            "icons_dir themes/wave/icons\n"
            "banner themes/Wave/bg.png\n"
            "selection_big themes/other/sel.png\n"
            "# banner themes/commented/bg.png\n"
            "banner_scale fillscreen\n";
        QCOMPARE(themeDirsReferenced(conf), QStringList({"wave", "other"}));
        QVERIFY(themeDirsReferenced("showtools shutdown\n").isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestThemeConf)
#include "tst_themeconf.moc"
