// Locks the UKI family logic behind manual-stanza UKI support (uki/uki.h):
// kernel-install file-name parsing, RPM-style version order, grouping,
// stanza rendering, and the marker-scoped refind.conf rewrite that the
// kernel-install plugin runs after every kernel add/remove.

#include "uki/uki.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {

const QString kMid = QStringLiteral("ec4de823fa4f4251832ddc066f2a77c5");

void put16(QByteArray &b, int off, quint16 v)
{
    b[off] = char(v & 0xff);
    b[off + 1] = char(v >> 8);
}

void put32(QByteArray &b, int off, quint32 v)
{
    for (int i = 0; i < 4; ++i)
        b[off + i] = char((v >> (8 * i)) & 0xff);
}

// A minimal PE image with .osrel and .uname sections, as ukify lays them out
// (raw size padded past the virtual size with NULs).
QByteArray fakeUki(const QByteArray &osrel, const QByteArray &uname)
{
    QByteArray b(1024, '\0');
    b[0] = 'M';
    b[1] = 'Z';
    put32(b, 0x3C, 64);
    b.replace(64, 4, QByteArray("PE\0\0", 4));
    put16(b, 64 + 4, 0x8664);
    put16(b, 64 + 6, 2);  // sections
    put16(b, 64 + 20, 0); // optional header size
    const struct { const char *name; QByteArray data; int ptr; } secs[] = {
        {".osrel", osrel, 512},
        {".uname", uname, 768},
    };
    for (int s = 0; s < 2; ++s) {
        const int base = 64 + 24 + s * 40;
        b.replace(base, int(qstrlen(secs[s].name)), secs[s].name);
        put32(b, base + 8, quint32(secs[s].data.size()));  // VirtualSize
        put32(b, base + 16, 256);                            // SizeOfRawData (padded)
        put32(b, base + 20, quint32(secs[s].ptr));           // PointerToRawData
        b.replace(secs[s].ptr, secs[s].data.size(), secs[s].data);
    }
    return b;
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

} // namespace

class TestUki : public QObject
{
    Q_OBJECT

private slots:
    void splitMachineIdName()
    {
        QString token, version;
        QVERIFY(Uki::splitFileName(kMid + QStringLiteral("-7.2.10-200.fc44.x86_64.efi"),
                                   QString(), &token, &version));
        QCOMPARE(token, kMid);
        QCOMPARE(version, QStringLiteral("7.2.10-200.fc44.x86_64"));
    }

    void splitOsIdNameUsesUname()
    {
        QString token, version;
        // A token that itself contains "-<digit>" is only split right when
        // the .uname version is known.
        QVERIFY(Uki::splitFileName(QStringLiteral("my-os-2-7.2.10-200.fc44.x86_64.efi"),
                                   QStringLiteral("7.2.10-200.fc44.x86_64"), &token, &version));
        QCOMPARE(token, QStringLiteral("my-os-2"));
        QCOMPARE(version, QStringLiteral("7.2.10-200.fc44.x86_64"));
        QVERIFY(Uki::splitFileName(QStringLiteral("fedora-7.2.10-200.fc44.x86_64.efi"),
                                   QString(), &token, &version));
        QCOMPARE(token, QStringLiteral("fedora"));
        QCOMPARE(version, QStringLiteral("7.2.10-200.fc44.x86_64"));
    }

    void splitUnversionedName()
    {
        // mkinitcpio-style fixed names: the whole stem is the token.
        QString token, version;
        QVERIFY(Uki::splitFileName(QStringLiteral("arch-linux-lts.EFI"),
                                   QStringLiteral("6.18.3-1-lts"), &token, &version));
        QCOMPARE(token, QStringLiteral("arch-linux-lts"));
        QCOMPARE(version, QStringLiteral("6.18.3-1-lts"));
    }

    void splitIgnoresBootCounting()
    {
        QString token, version;
        QVERIFY(Uki::splitFileName(kMid + QStringLiteral("-7.2.10-200.fc44.x86_64+3-0.efi"),
                                   QString(), &token, &version));
        QCOMPARE(token, kMid);
        QCOMPARE(version, QStringLiteral("7.2.10-200.fc44.x86_64"));
    }

    void splitBareVersionName()
    {
        // Fedora's UKI cloud image ships its first UKI named by kernel
        // version alone (built with an empty machine-id): one family, not a
        // token that changes with every version.
        QString token, version;
        QVERIFY(Uki::splitFileName(QStringLiteral("6.19.10-300.fc44.x86_64.efi"),
                                   QStringLiteral("6.19.10-300.fc44.x86_64"), &token, &version));
        QCOMPARE(token, QLatin1String(Uki::kNoToken));
        QCOMPARE(version, QStringLiteral("6.19.10-300.fc44.x86_64"));
        QVERIFY(Uki::splitFileName(QStringLiteral("7.0.1-100.fc44.x86_64.efi"), QString(),
                                   &token, &version));
        QCOMPARE(token, QLatin1String(Uki::kNoToken));
        QCOMPARE(version, QStringLiteral("7.0.1-100.fc44.x86_64"));
    }

    void splitRejectsUnusableNames()
    {
        QVERIFY(!Uki::splitFileName(QStringLiteral(".efi"), QString(), nullptr, nullptr));
        QVERIFY(!Uki::splitFileName(QStringLiteral("vmlinuz-7.2"), QString(), nullptr, nullptr));
        // Would need quoting in refind.conf, which rEFInd does not strip
        // from loader paths.
        QVERIFY(!Uki::splitFileName(QStringLiteral("my kernel.efi"), QString(), nullptr, nullptr));
        QVERIFY(!Uki::splitFileName(QStringLiteral("a\"b.efi"), QString(), nullptr, nullptr));
    }

    void versionOrder()
    {
        QVERIFY(Uki::compareVersions(QStringLiteral("7.2.10-200.fc44"), QStringLiteral("7.2.9-200.fc44")) > 0);
        QVERIFY(Uki::compareVersions(QStringLiteral("7.2.9"), QStringLiteral("7.2.10")) < 0);
        QVERIFY(Uki::compareVersions(QStringLiteral("7.2.0"), QStringLiteral("6.19.12")) > 0);
        QVERIFY(Uki::compareVersions(QStringLiteral("7.2.1"), QStringLiteral("7.2")) > 0);
        QVERIFY(Uki::compareVersions(QStringLiteral("7.2.010"), QStringLiteral("7.2.10")) == 0);
        QVERIFY(Uki::compareVersions(QStringLiteral("7.2.10-201"), QStringLiteral("7.2.10-200")) > 0);
        QCOMPARE(Uki::compareVersions(QString(), QString()), 0);
    }

    void groupsNewestFirst()
    {
        QList<Uki::Image> images;
        for (const QString &v : {QStringLiteral("7.2.9-200.fc44.x86_64"),
                                 QStringLiteral("7.2.10-200.fc44.x86_64"),
                                 QStringLiteral("6.19.12-300.fc44.x86_64")}) {
            Uki::Image img;
            img.fileName = kMid + QLatin1Char('-') + v + QStringLiteral(".efi");
            img.loaderPath = QStringLiteral("/EFI/Linux/") + img.fileName;
            img.token = kMid;
            img.kernelVersion = v;
            images << img;
        }
        Uki::Image other;
        other.fileName = QStringLiteral("arch-linux.efi");
        other.loaderPath = QStringLiteral("/EFI/Linux/arch-linux.efi");
        other.token = QStringLiteral("arch-linux");
        images << other;

        const QList<Uki::Family> fams = Uki::groupImages(images);
        QCOMPARE(fams.size(), 2);
        const Uki::Family &fedora = fams.at(1).token == kMid ? fams.at(1) : fams.at(0);
        QCOMPARE(fedora.versions,
                 QStringList({QStringLiteral("7.2.10-200.fc44.x86_64"),
                              QStringLiteral("7.2.9-200.fc44.x86_64"),
                              QStringLiteral("6.19.12-300.fc44.x86_64")}));
        QCOMPARE(fedora.loaders.first(),
                 QStringLiteral("/EFI/Linux/") + kMid + QStringLiteral("-7.2.10-200.fc44.x86_64.efi"));
    }

    void tokenlessImagesFoldIntoTheirOsFamily()
    {
        // After the first kernel-install on Fedora's UKI cloud image: the
        // shipped tokenless UKI is the same install's older kernel.
        QList<Uki::Image> images;
        Uki::Image bare;
        bare.fileName = QStringLiteral("6.19.10-300.fc44.x86_64.efi");
        bare.loaderPath = QStringLiteral("/EFI/Linux/") + bare.fileName;
        bare.token = QLatin1String(Uki::kNoToken);
        bare.kernelVersion = QStringLiteral("6.19.10-300.fc44.x86_64");
        bare.osId = QStringLiteral("fedora");
        Uki::Image tokened = bare;
        tokened.fileName = kMid + QStringLiteral("-7.2.10-200.fc44.x86_64.efi");
        tokened.loaderPath = QStringLiteral("/EFI/Linux/") + tokened.fileName;
        tokened.token = kMid;
        tokened.kernelVersion = QStringLiteral("7.2.10-200.fc44.x86_64");
        Uki::Image arch;
        arch.fileName = QStringLiteral("arch-linux.efi");
        arch.loaderPath = QStringLiteral("/EFI/Linux/arch-linux.efi");
        arch.token = QStringLiteral("arch-linux");
        arch.osId = QStringLiteral("arch");
        images << bare << tokened << arch;
        const QList<Uki::Family> fams = Uki::groupImages(images);
        QCOMPARE(fams.size(), 2);
        const Uki::Family &fedora = fams.at(0).token == kMid ? fams.at(0) : fams.at(1);
        QCOMPARE(fedora.token, kMid);
        QCOMPARE(fedora.versions,
                 QStringList({QStringLiteral("7.2.10-200.fc44.x86_64"),
                              QStringLiteral("6.19.10-300.fc44.x86_64")}));
    }

    void readsOsrelAndUname()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("x.efi"));
        QVERIFY(writeFile(path, fakeUki("NAME=\"Fedora Linux\"\nID=fedora\nPRETTY_NAME=\"Fedora Linux 44 (Cloud Edition)\"\n",
                                        "7.2.10-200.fc44.x86_64")));
        QString name, id, uname;
        QVERIFY(Uki::readImageInfo(path, &name, &id, &uname));
        QCOMPARE(name, QStringLiteral("Fedora Linux"));
        QCOMPARE(id, QStringLiteral("fedora"));
        QCOMPARE(uname, QStringLiteral("7.2.10-200.fc44.x86_64"));

        QVERIFY(writeFile(path, QByteArray("not a PE image")));
        QVERIFY(!Uki::readImageInfo(path, &name, &id, &uname));
    }

    void scansEspFamilies()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("EFI/Linux")));
        const QByteArray osrel("NAME=\"Fedora Linux\"\nID=fedora\n");
        for (const char *v : {"7.2.9-200.fc44.x86_64", "7.2.10-200.fc44.x86_64"}) {
            QVERIFY(writeFile(dir.filePath(QStringLiteral("EFI/Linux/") + kMid + QLatin1Char('-')
                                           + QLatin1String(v) + QStringLiteral(".efi")),
                              fakeUki(osrel, v)));
        }
        QVERIFY(writeFile(dir.filePath(QStringLiteral("EFI/Linux/readme.txt")), "x"));
        const QList<Uki::Family> fams = Uki::scanFamilies(dir.path());
        QCOMPARE(fams.size(), 1);
        QCOMPARE(fams.first().token, kMid);
        QCOMPARE(fams.first().osName, QStringLiteral("Fedora Linux"));
        QCOMPARE(fams.first().versions.first(), QStringLiteral("7.2.10-200.fc44.x86_64"));
        QCOMPARE(fams.first().loaders.size(), 2);
    }

    void rendersLoaderAndSubmenus()
    {
        const QString text = Uki::renderLoaderLines(
            {QStringLiteral("/EFI/Linux/a-7.2.10.efi"), QStringLiteral("/EFI/Linux/a-7.2.9.efi")},
            {QStringLiteral("7.2.10"), QStringLiteral("7.2.9")});
        QCOMPARE(text, QStringLiteral("\tloader /EFI/Linux/a-7.2.10.efi\n"
                                      "\tsubmenuentry \"Boot 7.2.9\" {\n"
                                      "\t\tloader /EFI/Linux/a-7.2.9.efi\n"
                                      "\t}\n"));
    }

    void refreshRepointsOnlyMarkedStanzas()
    {
        const QString conf = QStringLiteral(
            "timeout 5\n"
            "scanfor manual\n"
            "default_selection \"2\"\n"
            "\n"
            "menuentry \"Windows\" {\n"
            "\ticon /EFI/refind/os_icon1.png\n"
            "\tloader /EFI/Microsoft/Boot/bootmgfw.efi\n"
            "\tgraphics on\n"
            "}\n"
            "\n"
            "# rEFInd_GUI-uki token=fedora\n"
            "menuentry \"Fedora Linux\" {\n"
            "\ticon /EFI/refind/os_icon2.png\n"
            "\tvolume \"1234abcd-0000-0000-0000-000000000001\"\n"
            "\tloader /EFI/Linux/fedora-7.2.9.efi\n"
            "\tgraphics on\n"
            "\tsubmenuentry \"Boot 7.2.8\" {\n"
            "\t\tloader /EFI/Linux/fedora-7.2.8.efi\n"
            "\t}\n"
            "}\n");
        QString seenVolume;
        const Uki::FamilyLookup lookup = [&](const QString &volume) {
            seenVolume = volume;
            Uki::Family fam;
            fam.token = QStringLiteral("fedora");
            fam.loaders = {QStringLiteral("/EFI/Linux/fedora-7.2.10.efi"),
                           QStringLiteral("/EFI/Linux/fedora-7.2.9.efi")};
            fam.versions = {QStringLiteral("7.2.10"), QStringLiteral("7.2.9")};
            Uki::Family other;
            other.token = QStringLiteral("arch-linux");
            other.loaders = {QStringLiteral("/EFI/Linux/arch-linux.efi")};
            other.versions = {QString()};
            return QList<Uki::Family>{other, fam};
        };
        bool changed = false;
        QStringList notes;
        const QString out = Uki::refreshConf(conf, lookup, &changed, &notes);
        QVERIFY(changed);
        QVERIFY(notes.isEmpty());
        QCOMPARE(seenVolume, QStringLiteral("1234abcd-0000-0000-0000-000000000001"));
        const QString expectedTail = QStringLiteral(
            "# rEFInd_GUI-uki token=fedora\n"
            "menuentry \"Fedora Linux\" {\n"
            "\ticon /EFI/refind/os_icon2.png\n"
            "\tvolume \"1234abcd-0000-0000-0000-000000000001\"\n"
            "\tloader /EFI/Linux/fedora-7.2.10.efi\n"
            "\tgraphics on\n"
            "\tsubmenuentry \"Boot 7.2.9\" {\n"
            "\t\tloader /EFI/Linux/fedora-7.2.9.efi\n"
            "\t}\n"
            "}\n");
        // Everything before the marked stanza is byte-identical.
        const int split = conf.indexOf(QStringLiteral("# rEFInd_GUI-uki"));
        QCOMPARE(out.left(split), conf.left(split));
        QCOMPARE(out.mid(split), expectedTail);

        // Idempotent: a second run changes nothing.
        bool again = true;
        QCOMPARE(Uki::refreshConf(out, lookup, &again, nullptr), out);
        QVERIFY(!again);
    }

    void refreshLeavesUnknownFamilyAlone()
    {
        const QString conf = QStringLiteral(
            "# rEFInd_GUI-uki token=gone\n"
            "menuentry \"Gone\" {\n"
            "\tloader /EFI/Linux/gone-1.efi\n"
            "}\n");
        bool changed = true;
        QStringList notes;
        const QString out = Uki::refreshConf(
            conf, [](const QString &) { return QList<Uki::Family>(); }, &changed, &notes);
        QCOMPARE(out, conf);
        QVERIFY(!changed);
        QCOMPARE(notes.size(), 1);
    }

    void refreshSkipsHandEditedShapes()
    {
        // A closing brace sharing a line with content is not this module's
        // format: report it, never guess.
        const QString conf = QStringLiteral(
            "# rEFInd_GUI-uki token=fedora\n"
            "menuentry \"Fedora\" {\n"
            "\tloader /EFI/Linux/fedora-1.efi }\n");
        bool changed = true;
        QStringList notes;
        const QString out = Uki::refreshConf(
            conf,
            [](const QString &) {
                Uki::Family f;
                f.token = QStringLiteral("fedora");
                f.loaders = {QStringLiteral("/EFI/Linux/fedora-2.efi")};
                f.versions = {QStringLiteral("2")};
                return QList<Uki::Family>{f};
            },
            &changed, &notes);
        QCOMPARE(out, conf);
        QVERIFY(!changed);
        QCOMPARE(notes.size(), 1);
    }

    void refreshFollowsChangedTokenByOsId()
    {
        // The tokenless cloud-image UKI is replaced by the first
        // kernel-install'ed one (machine-id token): the OS id in the marker
        // carries the stanza over, and the marker learns the new token.
        const QString conf = QStringLiteral(
            "# rEFInd_GUI-uki token=_none_ os=fedora\n"
            "menuentry \"Fedora Linux\" {\n"
            "\tloader /EFI/Linux/6.19.10-300.fc44.x86_64.efi\n"
            "\tgraphics on\n"
            "}\n");
        const auto lookup = [](const QString &) {
            Uki::Family fedora;
            fedora.token = kMid;
            fedora.osId = QStringLiteral("fedora");
            fedora.loaders = {QStringLiteral("/EFI/Linux/") + kMid + QStringLiteral("-7.0.1.efi")};
            fedora.versions = {QStringLiteral("7.0.1")};
            Uki::Family arch;
            arch.token = QStringLiteral("arch-linux");
            arch.osId = QStringLiteral("arch");
            arch.loaders = {QStringLiteral("/EFI/Linux/arch-linux.efi")};
            arch.versions = {QString()};
            return QList<Uki::Family>{arch, fedora};
        };
        bool changed = false;
        const QString out = Uki::refreshConf(conf, lookup, &changed, nullptr);
        QVERIFY(changed);
        QVERIFY(out.startsWith(QStringLiteral("# rEFInd_GUI-uki token=") + kMid
                               + QStringLiteral(" os=fedora\n")));
        QVERIFY(out.contains(QStringLiteral("\tloader /EFI/Linux/") + kMid + QStringLiteral("-7.0.1.efi\n")));

        // Without an OS id, several families on the volume are ambiguous.
        QString noOs = conf;
        noOs.replace(QStringLiteral(" os=fedora"), QString());
        bool changed2 = true;
        QCOMPARE(Uki::refreshConf(noOs, lookup, &changed2, nullptr), noOs);
        QVERIFY(!changed2);
    }

    void tokenlessFamilyHandsOverToNewerTokenedOne()
    {
        // Fedora's UKI cloud image: the shipped tokenless UKI stays on the
        // ESP after the first kernel-install'ed (machine-id) one arrives, so
        // the token still matches -- the stanza must move to the new kernel.
        const QString conf = QStringLiteral(
            "# rEFInd_GUI-uki token=_none_\n"
            "menuentry \"Linux\" {\n"
            "\tloader /EFI/Linux/6.19.10-300.fc44.x86_64.efi\n"
            "\tgraphics on\n"
            "}\n");
        const auto lookup = [](const QString &) {
            Uki::Family bare;
            bare.token = QLatin1String(Uki::kNoToken);
            bare.loaders = {QStringLiteral("/EFI/Linux/6.19.10-300.fc44.x86_64.efi")};
            bare.versions = {QStringLiteral("6.19.10-300.fc44.x86_64")};
            Uki::Family tokened;
            tokened.token = kMid;
            tokened.loaders = {QStringLiteral("/EFI/Linux/") + kMid + QStringLiteral("-7.0.1-100.fc44.x86_64.efi")};
            tokened.versions = {QStringLiteral("7.0.1-100.fc44.x86_64")};
            return QList<Uki::Family>{bare, tokened};
        };
        bool changed = false;
        const QString out = Uki::refreshConf(conf, lookup, &changed, nullptr);
        QVERIFY(changed);
        QVERIFY(out.startsWith(QStringLiteral("# rEFInd_GUI-uki token=") + kMid + QLatin1Char('\n')));
        QVERIFY(out.contains(QStringLiteral("-7.0.1-100.fc44.x86_64.efi\n")));
    }

    void uninstalledKernelsLeaveTheMenu()
    {
        // Fedora's kernel package removes with --entry-type=type1, so its
        // UKI stays on the ESP: an image whose modules tree is gone (or the
        // kernel being removed right now) is dropped, never the last image.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString mods = dir.filePath(QStringLiteral("modules"));
        QVERIFY(QDir().mkpath(mods + QStringLiteral("/7.2.10-200.fc44.x86_64")));
        QVERIFY(writeFile(mods + QStringLiteral("/7.2.10-200.fc44.x86_64/modules.builtin"), "x"));
        // A leftover dir without the package-owned files is not installed.
        QVERIFY(QDir().mkpath(mods + QStringLiteral("/6.19.10-300.fc44.x86_64/weak-updates")));
        QVERIFY(Uki::modulesTreeUsable(mods));
        QVERIFY(Uki::kernelInstalled(QStringLiteral("7.2.10-200.fc44.x86_64"), mods));
        QVERIFY(!Uki::kernelInstalled(QStringLiteral("6.19.10-300.fc44.x86_64"), mods));
        QVERIFY(Uki::kernelInstalled(QString(), mods));
        QVERIFY(!Uki::modulesTreeUsable(dir.filePath(QStringLiteral("nothing"))));

        QStringList loaders{QStringLiteral("/EFI/Linux/a-7.2.11.efi"), QStringLiteral("/EFI/Linux/a-7.2.10.efi"),
                            QStringLiteral("/EFI/Linux/a-6.19.efi")};
        QStringList versions{QStringLiteral("7.2.11"), QStringLiteral("7.2.10"), QStringLiteral("6.19")};
        const QStringList dropped = Uki::dropUninstalledKernels(
            &loaders, &versions,
            [](const QString &v) { return v == QLatin1String("7.2.10"); });
        QCOMPARE(dropped, QStringList({QStringLiteral("/EFI/Linux/a-7.2.11.efi"),
                                       QStringLiteral("/EFI/Linux/a-6.19.efi")}));
        QCOMPARE(loaders, QStringList{QStringLiteral("/EFI/Linux/a-7.2.10.efi")});
        QCOMPARE(versions, QStringList{QStringLiteral("7.2.10")});

        // Never empties a family.
        QVERIFY(Uki::dropUninstalledKernels(&loaders, &versions,
                                            [](const QString &) { return false; }).isEmpty());
        QCOMPARE(loaders.size(), 1);
    }

    void onlyThisMachinesFamiliesArePruned()
    {
        QVERIFY(Uki::isThisMachinesFamily(kMid, QString(), kMid.toUpper(), QStringLiteral("fedora")));
        QVERIFY(Uki::isThisMachinesFamily(QLatin1String(Uki::kNoToken), QStringLiteral("fedora"),
                                          kMid, QStringLiteral("fedora")));
        // Another install's family on a shared ESP.
        QVERIFY(!Uki::isThisMachinesFamily(QStringLiteral("arch-linux"), QStringLiteral("arch"),
                                           kMid, QStringLiteral("fedora")));
        QVERIFY(!Uki::isThisMachinesFamily(QLatin1String(Uki::kNoToken), QStringLiteral("arch"),
                                           kMid, QStringLiteral("fedora")));
    }

    void ukiBootEntriesAreRecognized()
    {
        Uki::BootVar direct;
        direct.loaderPath = QStringLiteral("\\EFI\\Linux\\") + kMid + QStringLiteral("-7.2.10.efi");
        QCOMPARE(Uki::ukiBootedBy(direct), QStringLiteral("/EFI/Linux/") + kMid + QStringLiteral("-7.2.10.efi"));

        // uki-direct: shim with the UKI path as UTF-16LE optional data.
        Uki::BootVar viaShim;
        viaShim.loaderPath = QStringLiteral("\\EFI\\fedora\\shimx64.efi");
        const QString arg = QStringLiteral("\\EFI\\Linux\\6.19.10-300.fc44.x86_64.efi ");
        viaShim.optionalData = QByteArray(reinterpret_cast<const char *>(arg.utf16()), arg.size() * 2)
                               + QByteArray(2, '\0');
        QCOMPARE(Uki::ukiBootedBy(viaShim), QStringLiteral("/EFI/Linux/6.19.10-300.fc44.x86_64.efi"));

        Uki::BootVar windows;
        windows.loaderPath = QStringLiteral("\\EFI\\Microsoft\\Boot\\bootmgfw.efi");
        windows.optionalData = QByteArray("WINDOWS\0\x01\0\0\0", 12);
        QVERIFY(Uki::ukiBootedBy(windows).isEmpty());
    }

    void refindReclaimsPriorityFromUkiEntries()
    {
        const auto isRefind = [](const QString &p) {
            return p.contains(QLatin1String("refind_x64"), Qt::CaseInsensitive);
        };
        Uki::BootVar refind;
        refind.num = 4;
        refind.loaderPath = QStringLiteral("\\EFI\\refind\\refind_x64.efi");
        refind.active = true;
        Uki::BootVar uki;
        uki.num = 5;
        uki.loaderPath = QStringLiteral("\\EFI\\Linux\\") + kMid + QStringLiteral("-7.2.10.efi");
        uki.active = true;
        Uki::BootVar windows;
        windows.num = 0;
        windows.loaderPath = QStringLiteral("\\EFI\\Microsoft\\Boot\\bootmgfw.efi");
        windows.active = true;
        const QList<Uki::BootVar> vars{refind, uki, windows};

        // Kernel just installed: BootNext -> the UKI entry.
        Uki::PriorityPlan p = Uki::planRefindPriority({4, 0}, 5, vars, isRefind);
        QVERIFY(p.clearBootNext);
        QVERIFY(p.bootOrder.isEmpty());

        // After the boot-success service moved the UKI entry first.
        p = Uki::planRefindPriority({5, 4, 0}, -1, vars, isRefind);
        QVERIFY(!p.clearBootNext);
        QCOMPARE(p.bootOrder, QList<quint16>({4, 5, 0}));

        // The user put Windows first on purpose: not ours to undo.
        p = Uki::planRefindPriority({0, 4, 5}, -1, vars, isRefind);
        QVERIFY(p.bootOrder.isEmpty());
        // ...nor a BootNext that targets Windows (e.g. a one-time reboot).
        p = Uki::planRefindPriority({0, 4, 5}, 0, vars, isRefind);
        QVERIFY(!p.clearBootNext);

        // rEFInd removed from BootOrder: leave NVRAM alone entirely.
        p = Uki::planRefindPriority({5, 0}, 5, vars, isRefind);
        QVERIFY(!p.haveRefind);
        QVERIFY(!p.clearBootNext);
        QVERIFY(p.bootOrder.isEmpty());
    }

    void markedStanzasReportsLoaders()
    {
        const QString conf = QStringLiteral(
            "# rEFInd_GUI-uki token=") + kMid + QStringLiteral("\n"
            "menuentry \"Fedora Linux\" {\n"
            "\tvolume \"VOL\"\n"
            "\tloader /EFI/Linux/x.efi\n"
            "}\n");
        const QList<Uki::MarkedStanza> m = Uki::markedStanzas(conf);
        QCOMPARE(m.size(), 1);
        QCOMPARE(m.first().token, kMid);
        QCOMPARE(m.first().volume, QStringLiteral("VOL"));
        QCOMPARE(m.first().loader, QStringLiteral("/EFI/Linux/x.efi"));
    }
};

QTEST_APPLESS_MAIN(TestUki)
#include "tst_uki.moc"
