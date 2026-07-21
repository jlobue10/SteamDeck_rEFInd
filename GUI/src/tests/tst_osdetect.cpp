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
};

QTEST_APPLESS_MAIN(TestOsDetect)
#include "tst_osdetect.moc"
