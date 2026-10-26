// Locks the documented classification behavior of
// OSDetector::classifyLoaderPath (the deep-scan/efibootmgr recovery path):
// real OS loaders in, rEFInd itself / removable-media fallback / driver and
// tool dirs / non-bootmgfw Microsoft plumbing out. The osdetect_* sources are
// linked completely untouched (they are parity-locked with the sibling repo);
// the private-access define is confined to this test translation unit.
#define private public
#include "osdetect.h"
#undef private

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

class TestOsDetect : public QObject
{
    Q_OBJECT

private slots:
    void windowsLoader()
    {
        BootEntry e;
        QVERIFY(OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/Microsoft/Boot/bootmgfw.efi"), e));
        QCOMPARE(e.displayName, QStringLiteral("Windows"));
        QCOMPARE(e.loaderPath, QStringLiteral("/EFI/Microsoft/Boot/bootmgfw.efi"));
        QVERIFY(!e.supportsFirmwareBootnum);
    }

    void windowsLoaderCaseInsensitive()
    {
        BootEntry e;
        QVERIFY(OSDetector::classifyLoaderPath(
            QStringLiteral("/efi/microsoft/boot/BOOTMGFW.EFI"), e));
        QCOMPARE(e.displayName, QStringLiteral("Windows"));
        // The canonical path is emitted regardless of the input's case.
        QCOMPARE(e.loaderPath, QStringLiteral("/EFI/Microsoft/Boot/bootmgfw.efi"));
    }

    void steamosLoader()
    {
        BootEntry e;
        QVERIFY(OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/steamos/steamcl.efi"), e));
        QCOMPARE(e.displayName, QStringLiteral("SteamOS"));
        QVERIFY(e.supportsFirmwareBootnum);
    }

    void microsoftPlumbingIsSkipped()
    {
        // Everything under /EFI/Microsoft except bootmgfw is Windows
        // plumbing, not a bootable OS (bogus "Microsoft" entry regression).
        BootEntry e;
        QVERIFY(!OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/Microsoft/Boot/bootmgr.efi"), e));
        QVERIFY(!OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/Microsoft/Boot/memtest.efi"), e));
        QVERIFY(!OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/Microsoft/Recovery/WinRE.efi"), e));
    }

    void skipListIsHonored()
    {
        BootEntry e;
        // rEFInd itself and the removable-media fallback loader.
        QVERIFY(!OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/refind/refind_x64.efi"), e));
        QVERIFY(!OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/BOOT/bootx64.efi"), e));
        QVERIFY(!OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/refind/refind_aa64.efi"), e));
        QVERIFY(!OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/BOOT/bootaa64.efi"), e));
        QVERIFY(!OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/shellaa64/Shell.efi"), e));
        // Utility/driver dirs.
        QVERIFY(!OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/memtest86/memtest.efi"), e));
        QVERIFY(!OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/drivers_x64/UsbXbox360Dxe.efi"), e));
        QVERIFY(!OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/tools/shell.efi"), e));
    }

    void loaderWithoutVendorDirIsSkipped()
    {
        BootEntry e;
        QVERIFY(!OSDetector::classifyLoaderPath(QStringLiteral("/bootx64.efi"), e));
        QVERIFY(!OSDetector::classifyLoaderPath(QStringLiteral("/shellx64.efi"), e));
    }

    void linuxVendorLoaderIsAccepted()
    {
        // Exact display name depends on the vendor-name table (and, for the
        // running distro's own ESP, on /etc/os-release), so only the stable
        // invariants are asserted here.
        BootEntry e;
        QVERIFY(OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/fedora/shimx64.efi"), e));
        QVERIFY(!e.displayName.isEmpty());
        QCOMPARE(e.loaderPath, QStringLiteral("/EFI/fedora/shimx64.efi"));
        QCOMPARE(e.menuName, e.displayName);
    }
    void ukiLoaderIsClassifiedAsFamily()
    {
        // /EFI/Linux has no vendor dir and its files end in .efi -- the
        // vendor-dir rule alone would reject it. Recovery paths (deep scan,
        // efibootmgr) see names only; the entry carries its entry token so
        // detect() can fold one family into one slot.
        BootEntry e;
        const QString path = QStringLiteral(
            "/EFI/Linux/ec4de823fa4f4251832ddc066f2a77c5-7.2.10-200.fc44.x86_64.efi");
        QVERIFY(OSDetector::classifyLoaderPath(path, e));
        QCOMPARE(e.loaderPath, path);
        QCOMPARE(e.ukiToken, QStringLiteral("ec4de823fa4f4251832ddc066f2a77c5"));
        QCOMPARE(e.ukiVersions, QStringList{QStringLiteral("7.2.10-200.fc44.x86_64")});

        BootEntry named;
        QVERIFY(OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/Linux/fedora-7.2.10-200.fc44.x86_64.efi"), named));
        QCOMPARE(named.displayName, QStringLiteral("Fedora"));

        BootEntry bad;
        QVERIFY(!OSDetector::classifyLoaderPath(QStringLiteral("/EFI/Linux/.efi"), bad));
    }

    void ukiRecoveryEntriesMergeNewestFirst()
    {
        QList<BootEntry> files;
        for (const char *v : {"7.2.9-200.fc44.x86_64", "7.2.10-200.fc44.x86_64"}) {
            BootEntry e;
            QVERIFY(OSDetector::classifyLoaderPath(
                QStringLiteral("/EFI/Linux/fedora-") + QLatin1String(v) + QStringLiteral(".efi"), e));
            files << e;
        }
        BootEntry windows;
        QVERIFY(OSDetector::classifyLoaderPath(QStringLiteral("/EFI/Microsoft/Boot/bootmgfw.efi"), windows));
        files.prepend(windows);
        const QList<BootEntry> merged = OSDetector::mergeUkiEntries(files);
        QCOMPARE(merged.size(), 2);
        QCOMPARE(merged.at(1).loaderPath, QStringLiteral("/EFI/Linux/fedora-7.2.10-200.fc44.x86_64.efi"));
        QCOMPARE(merged.at(1).ukiLoaders.size(), 2);
        QCOMPARE(merged.at(1).ukiVersions.first(), QStringLiteral("7.2.10-200.fc44.x86_64"));
    }

    void firmwareShimEntryLaunchingUkiIsTheUki()
    {
        // Fedora uki-direct (captured from a Fedora 44 UEFI-UKI image): the
        // loader is shim and the UKI path is UTF-16LE hex optional data.
        // That entry is the UKI family; the rEFInd entry is skipped as usual.
        const QString out = QStringLiteral(
            "Boot0003* 6.19.10-300.fc44.x86_64\tHD(2,GPT,0604e560-34e2-427c-8973-df07fef6e85d,0x1800,0x1f4000)/"
            "\\EFI\\fedora\\shimx64.efi5c004500460049005c004c0069006e00750078005c0036002e00310039002e"
            "00310030002d003300300030002e0066006300340034002e007800380036005f00360034002e0065006600690020000000\n"
            "Boot0004* rEFInd Boot Manager\tHD(2,GPT,0604e560-34e2-427c-8973-df07fef6e85d,0x1800,0x1f4000)/"
            "\\EFI\\refind\\refind_x64.efi\n"
            "Boot0005* Fedora\tHD(2,GPT,0604e560-34e2-427c-8973-df07fef6e85d,0x1800,0x1f4000)/"
            "\\EFI\\fedora\\shimx64.efi\n"
            "Boot0006* Windows Boot Manager\tHD(1,GPT,11111111-2222-3333-4444-555555555555,0x800,0x32000)/"
            "\\EFI\\Microsoft\\Boot\\bootmgfw.efi57494e444f5753000100000088000000\n");
        const QList<BootEntry> entries = OSDetector::parseEfibootmgrEntries(
            out, QStringLiteral("0604e560-34e2-427c-8973-df07fef6e85d"));
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.at(0).loaderPath, QStringLiteral("/EFI/Linux/6.19.10-300.fc44.x86_64.efi"));
        QCOMPARE(entries.at(0).ukiToken, QStringLiteral("_none_"));
        QCOMPARE(entries.at(1).loaderPath, QStringLiteral("/EFI/fedora/shimx64.efi"));
        QCOMPARE(entries.at(1).ukiToken, QString());

        const QList<BootEntry> win = OSDetector::parseEfibootmgrEntries(
            out, QStringLiteral("11111111-2222-3333-4444-555555555555"));
        QCOMPARE(win.size(), 1);
        QCOMPARE(win.first().displayName, QStringLiteral("Windows"));
    }

    void ukiDirectShimDirIsNotAnOs()
    {
        // Fedora uki-direct: EFI/fedora holds shim + an unconfigured GRUB and
        // a BOOTX64.CSV whose every line launches an \EFI\Linux UKI. Only the
        // UKI family is an OS; with a grub.cfg the vendor dir counts again.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("EFI/Linux")));
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("EFI/fedora")));
        for (const QString &name : {QStringLiteral("EFI/fedora/shimx64.efi"),
                                    QStringLiteral("EFI/fedora/grubx64.efi"),
                                    QStringLiteral("EFI/Linux/fedora-7.2.10-200.fc44.x86_64.efi")}) {
            QFile f(dir.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        const QString csv = QStringLiteral(
            "shimx64.efi,Fedora 7.2.10 (UKI),\\EFI\\Linux\\fedora-7.2.10-200.fc44.x86_64.efi ,Comment\n");
        {
            QFile f(dir.filePath(QStringLiteral("EFI/fedora/BOOTX64.CSV")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("\xff\xfe");
            f.write(reinterpret_cast<const char *>(csv.utf16()), csv.size() * 2);
        }
        {
            // As shipped beside it: the ia32 CSV keeps its stock GRUB line.
            QFile f(dir.filePath(QStringLiteral("EFI/fedora/BOOTIA32.CSV")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("shimia32.efi,Fedora,,This is the boot entry for Fedora\n");
        }
        OSDetector detector;
        QList<BootEntry> entries = detector.scanEspRoot(dir.path(), QString());
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().ukiToken, QStringLiteral("fedora"));

        {
            QFile f(dir.filePath(QStringLiteral("EFI/fedora/grub.cfg")));
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        entries = detector.scanEspRoot(dir.path(), QString());
        QCOMPARE(entries.size(), 2);
    }

    void archSuffixSelectsLoaderNames()
    {
        // An ARM64 laptop (ASUS ProArt P16 / NVIDIA N1X): vendor dirs hold
        // *aa64.efi loaders and uki-direct writes BOOTAA64.CSV. The suffix
        // follows the running CPU; the override stands in for it here, so
        // this runs the same on an x86-64 build machine.
        const QString saved = OSDetector::efiArchSuffixOverride;
        OSDetector::efiArchSuffixOverride = QStringLiteral("aa64");
        QCOMPARE(OSDetector::efiArchSuffix(), QStringLiteral("aa64"));
        QCOMPARE(OSDetector::vendorLoaderCandidates(),
                 QStringList({QStringLiteral("shimaa64.efi"), QStringLiteral("grubaa64.efi"),
                              QStringLiteral("systemd-bootaa64.efi")}));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        for (const QString &sub : {QStringLiteral("EFI/systemd"), QStringLiteral("EFI/fedora"),
                                   QStringLiteral("EFI/Microsoft/Boot"), QStringLiteral("EFI/Linux")})
            QVERIFY(QDir(dir.path()).mkpath(sub));
        for (const QString &name : {QStringLiteral("EFI/systemd/systemd-bootaa64.efi"),
                                    QStringLiteral("EFI/fedora/shimaa64.efi"),
                                    QStringLiteral("EFI/fedora/grubaa64.efi"),
                                    QStringLiteral("EFI/Microsoft/Boot/bootmgfw.efi")}) {
            QFile f(dir.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        OSDetector detector;
        QList<BootEntry> entries = detector.scanEspRoot(dir.path(), QStringLiteral("Arch Linux ARM"));
        QCOMPARE(entries.size(), 3);
        QCOMPARE(entries.at(0).loaderPath, QStringLiteral("/EFI/Microsoft/Boot/bootmgfw.efi"));
        // shim beats GRUB, under the aa64 name.
        QCOMPARE(entries.at(1).loaderPath, QStringLiteral("/EFI/fedora/shimaa64.efi"));
        QCOMPARE(entries.at(2).loaderPath, QStringLiteral("/EFI/systemd/systemd-bootaa64.efi"));
        QCOMPARE(entries.at(2).displayName, QStringLiteral("Arch Linux ARM"));

        // uki-direct on ARM64: a BOOTAA64.CSV launching only UKIs hides the
        // shim dir, exactly as BOOTX64.CSV does on x86-64.
        {
            QFile f(dir.filePath(QStringLiteral("EFI/Linux/fedora-7.2.10-200.fc44.aarch64.efi")));
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        const QString csv = QStringLiteral(
            "shimaa64.efi,Fedora 7.2.10 (UKI),\\EFI\\Linux\\fedora-7.2.10-200.fc44.aarch64.efi ,Comment\n");
        {
            QFile f(dir.filePath(QStringLiteral("EFI/fedora/BOOTAA64.CSV")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("\xff\xfe");
            f.write(reinterpret_cast<const char *>(csv.utf16()), csv.size() * 2);
        }
        entries = detector.scanEspRoot(dir.path(), QString());
        QCOMPARE(entries.size(), 3);
        QCOMPARE(entries.at(1).loaderPath, QStringLiteral("/EFI/systemd/systemd-bootaa64.efi"));
        QCOMPARE(entries.at(2).ukiToken, QStringLiteral("fedora"));

        // The same ESP seen from an x86-64 machine: the aa64 loaders are not
        // bootable there, so only Windows and the UKI family remain.
        OSDetector::efiArchSuffixOverride = QStringLiteral("x64");
        entries = detector.scanEspRoot(dir.path(), QString());
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.at(0).displayName, QStringLiteral("Windows"));
        QCOMPARE(entries.at(1).ukiToken, QStringLiteral("fedora"));
        OSDetector::efiArchSuffixOverride = saved;
    }

    void scanEspRootFindsUkiFamilies()
    {
        // Names-only fixture (no PE sections): two versions of one token are
        // one slottable entry, a distinct fixed-name UKI is another.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("EFI/Linux")));
        for (const QString &name : {QStringLiteral("fedora-7.2.9-200.fc44.x86_64.efi"),
                                    QStringLiteral("fedora-7.2.10-200.fc44.x86_64.efi"),
                                    QStringLiteral("arch-linux.efi")}) {
            QFile f(dir.filePath(QStringLiteral("EFI/Linux/") + name));
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        OSDetector detector;
        const QList<BootEntry> entries = detector.scanEspRoot(dir.path(), QString());
        QCOMPARE(entries.size(), 2);
        const BootEntry &arch = entries.at(0);
        const BootEntry &fedora = entries.at(1);
        QCOMPARE(arch.ukiToken, QStringLiteral("arch-linux"));
        QCOMPARE(fedora.ukiToken, QStringLiteral("fedora"));
        QCOMPARE(fedora.displayName, QStringLiteral("Fedora"));
        QCOMPARE(fedora.loaderPath, QStringLiteral("/EFI/Linux/fedora-7.2.10-200.fc44.x86_64.efi"));
        QCOMPARE(fedora.ukiLoaders,
                 QStringList({QStringLiteral("/EFI/Linux/fedora-7.2.10-200.fc44.x86_64.efi"),
                              QStringLiteral("/EFI/Linux/fedora-7.2.9-200.fc44.x86_64.efi")}));
    }
};

QTEST_APPLESS_MAIN(TestOsDetect)
#include "tst_osdetect.moc"
