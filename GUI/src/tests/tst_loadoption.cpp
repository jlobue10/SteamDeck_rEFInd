// Unit tests for the espops EFI_LOAD_OPTION parser (parity-locked between
// rEFInd_GUI and SteamDeck_rEFInd like the code under test).

#include "espops/loadoption.h"

#include <QtTest>

using namespace EspOps;

namespace {

void appendU16(QByteArray &b, quint16 v)
{
    b.append(char(v & 0xff));
    b.append(char(v >> 8));
}

void appendU32(QByteArray &b, quint32 v)
{
    appendU16(b, quint16(v & 0xffff));
    appendU16(b, quint16(v >> 16));
}

void appendU64(QByteArray &b, quint64 v)
{
    appendU32(b, quint32(v & 0xffffffff));
    appendU32(b, quint32(v >> 32));
}

QByteArray utf16z(const QString &s)
{
    QByteArray b;
    for (const QChar &c : s)
        appendU16(b, c.unicode());
    appendU16(b, 0);
    return b;
}

// The GUID used everywhere below, in EFI byte order (first three fields
// little-endian): 12345678-9abc-def0-1234-56789abcdef0
const QByteArray kGuidBytes = QByteArray::fromHex("78563412bc9af0de123456789abcdef0");
const QString kGuidText = QStringLiteral("12345678-9abc-def0-1234-56789abcdef0");

QByteArray hardDriveNode(const QByteArray &sigBytes, quint8 sigType,
                         quint32 partNum = 1)
{
    QByteArray n;
    n.append(char(0x04)); // Media
    n.append(char(0x01)); // Hard Drive
    appendU16(n, 42);
    appendU32(n, partNum);
    appendU64(n, 2048);    // partition start
    appendU64(n, 409600);  // partition size
    n.append(sigBytes);    // 16-byte signature
    n.append(char(0x02));  // MBRType: GPT
    n.append(char(sigType));
    return n;
}

QByteArray filePathNode(const QString &path)
{
    const QByteArray p = utf16z(path);
    QByteArray n;
    n.append(char(0x04)); // Media
    n.append(char(0x04)); // File Path
    appendU16(n, quint16(4 + p.size()));
    n.append(p);
    return n;
}

QByteArray endNode()
{
    QByteArray n;
    n.append(char(0x7f));
    n.append(char(0xff));
    appendU16(n, 4);
    return n;
}

QByteArray buildLoadOption(quint32 attributes, const QString &description,
                           const QByteArray &devicePath,
                           const QByteArray &optionalData = QByteArray())
{
    QByteArray b;
    appendU32(b, attributes);
    appendU16(b, quint16(devicePath.size()));
    b.append(utf16z(description));
    b.append(devicePath);
    b.append(optionalData);
    return b;
}

} // namespace

class TestLoadOption : public QObject
{
    Q_OBJECT

private slots:
    void parsesTypicalRefindEntry()
    {
        const QByteArray raw = buildLoadOption(
            0x1, QStringLiteral("rEFInd"),
            hardDriveNode(kGuidBytes, 0x02, 3)
                + filePathNode(QStringLiteral("\\EFI\\refind\\refind_x64.efi"))
                + endNode());
        const LoadOption o = parseLoadOption(raw);
        QVERIFY(o.valid);
        QVERIFY(o.active());
        QCOMPARE(o.description, QStringLiteral("rEFInd"));
        QCOMPARE(o.gptPartitionGuid, kGuidText);
        QCOMPARE(o.partitionNumber, 3u);
        QCOMPARE(o.loaderPath, QStringLiteral("\\EFI\\refind\\refind_x64.efi"));
        QVERIFY(o.optionalData.isEmpty());
        QVERIFY(!o.hasWindowsOptionalData());
    }

    void inactiveEntry()
    {
        const QByteArray raw = buildLoadOption(0x0, QStringLiteral("x"), endNode());
        const LoadOption o = parseLoadOption(raw);
        QVERIFY(o.valid);
        QVERIFY(!o.active());
    }

    void windowsBlobDetected()
    {
        // {bootmgr}'s optional data carries ASCII "WINDOWS"
        // (57 49 4e 44 4f 57 53) followed by BCD plumbing.
        const QByteArray blob = QByteArray::fromHex("57494e444f5753")
            + QByteArrayLiteral("...BCDOBJECT={guid}");
        const QByteArray raw = buildLoadOption(
            0x1, QStringLiteral("Windows Boot Manager"),
            hardDriveNode(kGuidBytes, 0x02)
                + filePathNode(QStringLiteral("\\EFI\\Microsoft\\Boot\\bootmgfw.efi"))
                + endNode(),
            blob);
        const LoadOption o = parseLoadOption(raw);
        QVERIFY(o.valid);
        QCOMPARE(o.optionalData, blob);
        QVERIFY(o.hasWindowsOptionalData());
    }

    void mbrSignatureYieldsNoGuid()
    {
        const QByteArray raw = buildLoadOption(
            0x1, QStringLiteral("Legacy"),
            hardDriveNode(kGuidBytes, 0x01) + endNode());
        const LoadOption o = parseLoadOption(raw);
        QVERIFY(o.valid);
        QVERIFY(o.gptPartitionGuid.isEmpty());
        QCOMPARE(o.partitionNumber, 0u);
    }

    void skipsUnknownNodes()
    {
        // A vendor node (type 0x01 ACPI here, any non-media type works)
        // before the interesting ones must be skipped by length.
        QByteArray vendor;
        vendor.append(char(0x01));
        vendor.append(char(0x01));
        appendU16(vendor, 12);
        vendor.append(QByteArray(8, char(0xab)));
        const QByteArray raw = buildLoadOption(
            0x1, QStringLiteral("d"),
            vendor + hardDriveNode(kGuidBytes, 0x02)
                + filePathNode(QStringLiteral("\\EFI\\BOOT\\BOOTX64.EFI"))
                + endNode());
        const LoadOption o = parseLoadOption(raw);
        QVERIFY(o.valid);
        QCOMPARE(o.gptPartitionGuid, kGuidText);
        QCOMPARE(o.loaderPath, QStringLiteral("\\EFI\\BOOT\\BOOTX64.EFI"));
    }

    void firstNodesWin()
    {
        // Multi-instance or just repeated nodes: the first hard-drive and
        // first file-path node are authoritative.
        const QByteArray otherGuid = QByteArray::fromHex("00000000000000000000000000000001");
        const QByteArray raw = buildLoadOption(
            0x1, QStringLiteral("d"),
            hardDriveNode(kGuidBytes, 0x02, 1)
                + filePathNode(QStringLiteral("\\first.efi"))
                + hardDriveNode(otherGuid, 0x02, 9)
                + filePathNode(QStringLiteral("\\second.efi"))
                + endNode());
        const LoadOption o = parseLoadOption(raw);
        QVERIFY(o.valid);
        QCOMPARE(o.gptPartitionGuid, kGuidText);
        QCOMPARE(o.partitionNumber, 1u);
        QCOMPARE(o.loaderPath, QStringLiteral("\\first.efi"));
    }

    void truncationNeverCrashes()
    {
        const QByteArray full = buildLoadOption(
            0x1, QStringLiteral("rEFInd"),
            hardDriveNode(kGuidBytes, 0x02)
                + filePathNode(QStringLiteral("\\EFI\\refind\\refind_x64.efi"))
                + endNode(),
            QByteArrayLiteral("opt"));
        // Every prefix of a valid option must parse without crashing. A cut
        // anywhere before the optional-data region (the last 3 bytes here)
        // truncates the device-path list and must be rejected; a cut inside
        // optionalData still parses (shorter blob).
        const int pathsEnd = full.size() - 3;
        for (int len = 0; len < full.size(); ++len) {
            const LoadOption o = parseLoadOption(full.left(len));
            if (len < pathsEnd)
                QVERIFY2(!o.valid, qPrintable(QStringLiteral("len %1").arg(len)));
        }
        QVERIFY(parseLoadOption(full).valid);
    }

    void garbageRejected()
    {
        QVERIFY(!parseLoadOption(QByteArray()).valid);
        QVERIFY(!parseLoadOption(QByteArray(3, char(0xff))).valid);
        QVERIFY(!parseLoadOption(QByteArray(64, char(0x00))).valid);
        QVERIFY(!parseLoadOption(QByteArray(64, char(0xff))).valid);
    }

    void missingEndNodeRejected()
    {
        const QByteArray raw = buildLoadOption(
            0x1, QStringLiteral("d"), hardDriveNode(kGuidBytes, 0x02));
        QVERIFY(!parseLoadOption(raw).valid);
    }

    void bootOrder()
    {
        QByteArray b;
        appendU16(b, 0x0003);
        appendU16(b, 0x0000);
        appendU16(b, 0x2001);
        QCOMPARE(parseBootOrder(b), (QList<quint16>{3, 0, 0x2001}));
        b.append(char(0x7f)); // trailing odd byte ignored
        QCOMPARE(parseBootOrder(b).size(), 3);
        QCOMPARE(parseBootOrder(QByteArray()).size(), 0);
    }

    void bootNumFormat()
    {
        QCOMPARE(formatBootNum(0), QStringLiteral("Boot0000"));
        QCOMPARE(formatBootNum(0x2001), QStringLiteral("Boot2001"));
        QCOMPARE(formatBootNum(0xffff), QStringLiteral("BootFFFF"));
    }

    void efivarsPrefixStripped()
    {
        QByteArray raw = QByteArray::fromHex("07000000") + QByteArrayLiteral("rest");
        QCOMPARE(fromEfivarsPayload(raw), QByteArrayLiteral("rest"));
        QVERIFY(fromEfivarsPayload(QByteArray::fromHex("070000")).isEmpty());
    }

    void refindLoaderMatcher()
    {
        QVERIFY(loaderLooksLikeRefind(QStringLiteral("\\EFI\\refind\\refind_x64.efi")));
        QVERIFY(loaderLooksLikeRefind(QStringLiteral("\\EFI\\refind\\refind.efi")));
        QVERIFY(loaderLooksLikeRefind(QStringLiteral("\\EFI\\REFIND\\REFIND_X64.EFI")));
        QVERIFY(loaderLooksLikeRefind(QStringLiteral("/EFI/refind/refind_x64.efi")));
        // The [^\\] guard: rEFInd's *drivers* must not match.
        QVERIFY(!loaderLooksLikeRefind(
            QStringLiteral("\\EFI\\refind\\drivers_x64\\ext4_x64.efi")));
        QVERIFY(!loaderLooksLikeRefind(QStringLiteral("\\EFI\\Microsoft\\Boot\\bootmgfw.efi")));
        // Deliberate script-parity quirk (NATIVE_HELPER_DESIGN.md §2.4: the
        // resolution order is ported bug-for-bug): the bash matcher is a
        // *contains* grep, so a ".efi.bak" suffix still matches — the
        // ".efi" can end mid-string. Keep this behavior until both repos
        // decide to change it together.
        QVERIFY(loaderLooksLikeRefind(QStringLiteral("\\EFI\\refind\\refind_x64.efi.bak")));
        QVERIFY(!loaderLooksLikeRefind(QString()));
    }
};

QTEST_APPLESS_MAIN(TestLoadOption)
#include "tst_loadoption.moc"
