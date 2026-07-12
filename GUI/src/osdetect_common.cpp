#include "osdetect.h"

#include <QDir>
#include <QFile>
#include <QMap>
#include <QProcess>

// GPT partition type GUID for EFI System Partitions (lowercase, no braces).
static const QString ESP_PARTTYPE_GPT = QStringLiteral("c12a7328-f81f-11d2-ba4b-00a0c93ec93b");
// MBR partition type for ESPs.
static const QString ESP_PARTTYPE_MBR = QStringLiteral("0xef");

QString OSDetector::runCommand(const QString &program, const QStringList &args, bool *ok)
{
    QProcess proc;
    proc.start(program, args);
    const bool finished = proc.waitForStarted(5000) && proc.waitForFinished(30000);
    if (ok)
        *ok = finished && proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    return QString::fromUtf8(proc.readAllStandardOutput());
}

bool OSDetector::isEsp(const Partition &p)
{
    const QString type = p.partType.toLower();
    return type == ESP_PARTTYPE_GPT || type == ESP_PARTTYPE_MBR;
}

QString OSDetector::displayNameForVendorDir(const QString &dirName)
{
    const QString lower = dirName.toLower();
    // If this vendor dir belongs to the running distro's family, use its real
    // name (e.g. Nobara and Bazzite both boot from /EFI/fedora). On Windows
    // runningOsIds() is empty and the static map below decides.
    if (runningOsIds().contains(lower)) {
        const QString name = runningOsName();
        if (!name.isEmpty())
            return name;
    }
    static const QMap<QString, QString> known = {
        {QStringLiteral("fedora"), QStringLiteral("Fedora")},
        {QStringLiteral("ubuntu"), QStringLiteral("Ubuntu")},
        {QStringLiteral("debian"), QStringLiteral("Debian")},
        {QStringLiteral("opensuse"), QStringLiteral("openSUSE")},
        {QStringLiteral("neon"), QStringLiteral("KDE Neon")},
        {QStringLiteral("cachyos"), QStringLiteral("CachyOS")},
        {QStringLiteral("centos"), QStringLiteral("CentOS")},
        {QStringLiteral("manjaro"), QStringLiteral("Manjaro")},
        {QStringLiteral("kali"), QStringLiteral("Kali")},
        {QStringLiteral("elementary"), QStringLiteral("Elementary")},
        {QStringLiteral("arch"), QStringLiteral("Arch")},
        {QStringLiteral("endeavouros"), QStringLiteral("EndeavourOS")},
        {QStringLiteral("garuda"), QStringLiteral("Garuda")},
        {QStringLiteral("pop"), QStringLiteral("Pop!_OS")},
        {QStringLiteral("nobara"), QStringLiteral("Nobara")},
        {QStringLiteral("bazzite"), QStringLiteral("Bazzite")},
        {QStringLiteral("chimeraos"), QStringLiteral("ChimeraOS")},
        {QStringLiteral("systemd"), QStringLiteral("systemd-boot")},
    };
    if (known.contains(lower))
        return known.value(lower);
    QString name = dirName;
    name[0] = name.at(0).toUpper();
    return name;
}

QList<BootEntry> OSDetector::scanEspRoot(const QString &rootPath)
{
    QList<BootEntry> entries;
    QDir efiDir(rootPath + "/EFI");
    if (!efiDir.exists())
        return entries;

    static const QStringList skipDirs = {
        QStringLiteral("boot"), QStringLiteral("refind"), QStringLiteral("keys"),
        QStringLiteral("fonts"), QStringLiteral("memtest86"), QStringLiteral("shellx64"),
    };

    BootEntry windows, steam;
    QList<BootEntry> linuxEntries;
    const QStringList dirs = efiDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &dirName : dirs) {
        const QString lower = dirName.toLower();
        if (skipDirs.contains(lower) || lower.startsWith(QLatin1String("drivers"))
            || lower.startsWith(QLatin1String("tools")))
            continue;
        const QDir sub(efiDir.filePath(dirName));
        if (lower == QLatin1String("microsoft")) {
            if (QFile::exists(sub.filePath(QStringLiteral("Boot/bootmgfw.efi")))) {
                windows.displayName = QStringLiteral("Windows");
                windows.menuName = QStringLiteral("Windows");
                windows.loaderPath = QStringLiteral("/EFI/Microsoft/Boot/bootmgfw.efi");
            }
            continue;
        }
        if (lower == QLatin1String("steamos")) {
            if (QFile::exists(sub.filePath(QStringLiteral("steamcl.efi")))) {
                steam.displayName = QStringLiteral("SteamOS");
                steam.menuName = QStringLiteral("SteamOS");
                steam.loaderPath = QStringLiteral("/EFI/steamos/steamcl.efi");
                steam.supportsFirmwareBootnum = true;
            }
            continue;
        }
        // Generic Linux vendor dir: prefer shim (secure boot), then GRUB, then systemd-boot.
        static const QStringList loaderCandidates = {
            QStringLiteral("shimx64.efi"), QStringLiteral("grubx64.efi"),
            QStringLiteral("systemd-bootx64.efi"),
        };
        QString loaderFile;
        for (const QString &candidate : loaderCandidates) {
            if (QFile::exists(sub.filePath(candidate))) {
                loaderFile = candidate;
                break;
            }
        }
        if (loaderFile.isEmpty())
            continue;
        BootEntry e;
        e.displayName = displayNameForVendorDir(dirName);
        e.menuName = e.displayName;
        e.loaderPath = QStringLiteral("/EFI/") + dirName + "/" + loaderFile;
        linuxEntries.append(e);
    }

    if (!windows.loaderPath.isEmpty())
        entries.append(windows);
    entries.append(linuxEntries);
    if (!steam.loaderPath.isEmpty())
        entries.append(steam);
    return entries;
}

QList<BootEntry> OSDetector::assembleEntries(const QList<Partition> &partitions, QList<BootEntry> mounted)
{
    QList<BootEntry> entries = mounted;

    bool haveWindows = false;
    for (const BootEntry &e : entries) {
        if (e.loaderPath == QLatin1String("/EFI/Microsoft/Boot/bootmgfw.efi") && e.volume.isEmpty())
            haveWindows = true;
    }

    auto addUnique = [&entries](const BootEntry &e) {
        for (const BootEntry &existing : entries) {
            if (existing.displayName == e.displayName)
                return;
        }
        entries.append(e);
    };

    for (const Partition &p : partitions) {
        // Removable media recognized by well-known labels, ESP-typed or not.
        if (p.label.compare(QLatin1String("BATOCERA"), Qt::CaseInsensitive) == 0) {
            addUnique({QStringLiteral("Batocera"), QStringLiteral("Batocera"),
                       QStringLiteral("/EFI/BOOT/bootx64.efi"), QStringLiteral("BATOCERA"), false});
            continue;
        }
        if (p.label.compare(QLatin1String("VTOYEFI"), Qt::CaseInsensitive) == 0) {
            addUnique({QStringLiteral("Ventoy"), QStringLiteral("Ventoy"),
                       QStringLiteral("/EFI/BOOT/grubx64_real.efi"), QStringLiteral("VTOYEFI"), false});
            continue;
        }
        if (!isEsp(p) || !p.mountPoint.isEmpty())
            continue;
        const bool onSdCard = p.path.contains(QLatin1String("mmcblk"))
                              || p.transport == QLatin1String("mmc")
                              || p.transport == QLatin1String("sd");
        const bool onUsb = p.transport == QLatin1String("usb");
        const bool windowsLabel = p.label.compare(QLatin1String("SYSTEM"), Qt::CaseInsensitive) == 0
                                  || p.label.compare(QLatin1String("SYSTEM_DRV"), Qt::CaseInsensitive) == 0;
        if (windowsLabel && !p.removable && !onSdCard && !onUsb) {
            // Windows ESP on another internal drive/partition (e.g. Bazzite
            // auto-partitioning on ROG Ally / Legion Go): reference by label.
            BootEntry w;
            w.displayName = haveWindows ? QStringLiteral("Windows (") + p.label + ")" : QStringLiteral("Windows");
            w.menuName = QStringLiteral("Windows");
            w.loaderPath = QStringLiteral("/EFI/Microsoft/Boot/bootmgfw.efi");
            w.volume = p.label;
            addUnique(w);
            haveWindows = true;
        } else if ((onSdCard || onUsb || p.removable) && !p.partUuid.isEmpty()) {
            BootEntry w;
            w.displayName = onSdCard ? QStringLiteral("Windows (SD)") : QStringLiteral("Windows (USB)");
            w.menuName = onSdCard ? QStringLiteral("Windows Micro SD") : QStringLiteral("Windows USB");
            w.loaderPath = QStringLiteral("/EFI/Microsoft/Boot/bootmgfw.efi");
            w.volume = p.partUuid;
            addUnique(w);
        }
    }
    return entries;
}

QList<BootEntry> OSDetector::detect()
{
    cachedPartitions = listPartitions();
    cacheValid = true;
    bool release = false;
    const QString espRoot = acquireEspRoot(cachedPartitions, release);
    QList<BootEntry> mounted;
    if (!espRoot.isEmpty())
        mounted = scanEspRoot(espRoot);
    if (release)
        releaseEspRoot(espRoot);
    return assembleEntries(cachedPartitions, mounted);
}

QString OSDetector::removableEspPartUuid(bool sdCard)
{
    if (!cacheValid) {
        cachedPartitions = listPartitions();
        cacheValid = true;
    }
    for (const Partition &p : cachedPartitions) {
        if (!isEsp(p) || p.partUuid.isEmpty())
            continue;
        const bool onSdCard = p.path.contains(QLatin1String("mmcblk"))
                              || p.transport == QLatin1String("mmc")
                              || p.transport == QLatin1String("sd");
        const bool onUsb = p.transport == QLatin1String("usb");
        if (sdCard ? onSdCard : (onUsb || (p.removable && !onSdCard)))
            return p.partUuid;
    }
    return {};
}
