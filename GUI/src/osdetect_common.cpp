#include "osdetect.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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

static const QMap<QString, QString> &knownDistroNames()
{
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
    return known;
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
    const QMap<QString, QString> &known = knownDistroNames();
    if (known.contains(lower))
        return known.value(lower);
    QString name = dirName;
    name[0] = name.at(0).toUpper();
    return name;
}

// A bare systemd-boot vendor dir (/EFI/systemd) carries no distro name of its
// own, but the boot entries it was installed with do. Read the first `title`
// from loader/entries/*.conf and map any word of it onto a known distro name
// ("Linux Cachyos Deckify" -> "CachyOS"), falling back to the raw title. This
// is what names a foreign systemd-boot ESP (e.g. CachyOS scanned from Windows,
// where os-release isn't available).
static QString systemdBootDistroName(const QString &rootPath)
{
    QDir entriesDir(rootPath + "/loader/entries");
    const QStringList confs = entriesDir.entryList({QStringLiteral("*.conf")},
                                                   QDir::Files, QDir::Name);
    for (const QString &conf : confs) {
        QFile f(entriesDir.filePath(conf));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
        for (const QString &line : lines) {
            const QString trimmed = line.trimmed();
            if (!trimmed.startsWith(QLatin1String("title")))
                continue;
            const QString title = trimmed.mid(5).trimmed();
            if (title.isEmpty())
                break;
            const QStringList words = title.toLower().split(' ', Qt::SkipEmptyParts);
            for (const QString &word : words) {
                if (knownDistroNames().contains(word))
                    return knownDistroNames().value(word);
            }
            return title;
        }
    }
    return {};
}

QList<BootEntry> OSDetector::scanEspRoot(const QString &rootPath, const QString &runningDistroName)
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
        // A bare systemd-boot install (loader in /EFI/systemd, no distro dir)
        // has no vendor name of its own; on the running system's ESP, name it
        // after the running distro, and elsewhere (e.g. a Linux ESP scanned
        // from Windows) after its loader/entries titles, before settling for
        // the generic "systemd-boot".
        if (lower == QLatin1String("systemd") && !runningDistroName.isEmpty()) {
            e.displayName = runningDistroName;
        } else if (lower == QLatin1String("systemd")) {
            const QString fromEntries = systemdBootDistroName(rootPath);
            e.displayName = !fromEntries.isEmpty() ? fromEntries
                                                   : displayNameForVendorDir(dirName);
        } else {
            e.displayName = displayNameForVendorDir(dirName);
        }
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
        // Any already-found Windows counts, whether it was scanned (now tagged
        // with its ESP's volume) or added earlier without one.
        if (e.loaderPath == QLatin1String("/EFI/Microsoft/Boot/bootmgfw.efi"))
            haveWindows = true;
    }

    auto addUnique = [&entries](const BootEntry &e) {
        for (const BootEntry &existing : entries) {
            if (existing.displayName == e.displayName)
                return;
        }
        entries.append(e);
    };

    // True when a scanned entry already covers this exact loader on this
    // volume -- the cross-volume fallbacks below must not duplicate an ESP
    // that was reachable and scanned (on Windows even letterless non-system
    // ESPs are mounted and scanned now).
    auto haveOnVolume = [&entries](const QString &loader, const QString &volume) {
        for (const BootEntry &existing : entries) {
            if (existing.loaderPath == loader && existing.volume == volume)
                return true;
        }
        return false;
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
        if (haveOnVolume(QStringLiteral("/EFI/Microsoft/Boot/bootmgfw.efi"), espVolumeId(p)))
            continue;
        if (windowsLabel && !p.removable && !onSdCard && !onUsb) {
            // Windows ESP on another internal drive/partition (e.g. a separate
            // Windows disk, or Bazzite auto-partitioning on ROG Ally / Legion
            // Go). This ESP is usually unmounted, so it isn't scanned above;
            // reference it by partition GUID so the stanza boots from whichever
            // ESP rEFInd itself launched from.
            BootEntry w;
            w.displayName = haveWindows ? QStringLiteral("Windows (") + p.label + ")" : QStringLiteral("Windows");
            w.menuName = QStringLiteral("Windows");
            w.loaderPath = QStringLiteral("/EFI/Microsoft/Boot/bootmgfw.efi");
            w.volume = espVolumeId(p);
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

QString OSDetector::espVolumeId(const Partition &p)
{
    // rEFInd resolves a volume by partition GUID, filesystem label, or number.
    // The GUID is unique and stable, so prefer it; fall back to the label.
    return !p.partUuid.isEmpty() ? p.partUuid : p.label;
}

bool OSDetector::classifyLoaderPath(const QString &loaderPath, BootEntry &entry)
{
    const QString lower = loaderPath.toLower();

    if (lower == QLatin1String("/efi/microsoft/boot/bootmgfw.efi")) {
        entry.displayName = QStringLiteral("Windows");
        entry.menuName = QStringLiteral("Windows");
        entry.loaderPath = QStringLiteral("/EFI/Microsoft/Boot/bootmgfw.efi");
        return true;
    }
    if (lower == QLatin1String("/efi/steamos/steamcl.efi")) {
        entry.displayName = QStringLiteral("SteamOS");
        entry.menuName = QStringLiteral("SteamOS");
        entry.loaderPath = QStringLiteral("/EFI/steamos/steamcl.efi");
        entry.supportsFirmwareBootnum = true;
        return true;
    }
    // Everything else under /EFI/Microsoft (bootmgr.efi, memtest.efi,
    // Recovery\...) is Windows plumbing, not a bootable OS of its own — the
    // deep scan lists every .efi it finds, so without this a Windows ESP
    // surfaces a bogus "Microsoft" entry next to the real "Windows" one.
    if (lower.startsWith(QLatin1String("/efi/microsoft/")))
        return false;

    // /EFI/<vendor>/<loader>.efi — a loader with no vendor dir is not an OS.
    const QString vendorLower = lower.section('/', 2, 2);
    if (vendorLower.isEmpty() || vendorLower.endsWith(QLatin1String(".efi")))
        return false;
    // Mirror scanEspRoot()'s skip list: rEFInd itself, the removable-media
    // fallback loader, and utility/driver dirs are not distinct OS installs.
    static const QStringList skipDirs = {
        QStringLiteral("boot"), QStringLiteral("refind"), QStringLiteral("keys"),
        QStringLiteral("fonts"), QStringLiteral("memtest86"), QStringLiteral("shellx64"),
    };
    if (skipDirs.contains(vendorLower) || vendorLower.startsWith(QLatin1String("drivers"))
        || vendorLower.startsWith(QLatin1String("tools")))
        return false;

    entry.displayName = displayNameForVendorDir(loaderPath.section('/', 2, 2));
    entry.menuName = entry.displayName;
    entry.loaderPath = loaderPath;
    return true;
}

bool OSDetector::espRootUnreadable(const QString &rootPath)
{
    if (rootPath.isEmpty())
        return false;
    const QFileInfo efiDir(rootPath + QStringLiteral("/EFI"));
    if (efiDir.exists())
        return !efiDir.isReadable();
    // EFI/ may be invisible only because the mount point itself cannot be
    // traversed, which from here is indistinguishable from an ESP that
    // genuinely has no EFI/ directory -- so check the mount point too.
    const QFileInfo root(rootPath);
    return root.exists() && !root.isReadable();
}

bool OSDetector::isRunningSystemEsp(const Partition &p)
{
    // The running OS's ESP is the one mounted at its EFI location. The Steam
    // Deck mounts its ESP at /esp, so check that first. On Windows these
    // paths never match a drive-letter mount point, so this is false there
    // (where a running-distro name is unavailable anyway).
    return p.mountPoint == QLatin1String("/esp")
           || p.mountPoint == QLatin1String("/boot")
           || p.mountPoint == QLatin1String("/boot/efi")
           || p.mountPoint == QLatin1String("/efi");
}

QList<BootEntry> OSDetector::detect()
{
    cachedPartitions = listPartitions();
    cacheValid = true;

    // Scan every reachable ESP (not just one), tagging each discovered entry
    // with its ESP's volume so the generated stanza boots regardless of which
    // ESP rEFInd itself lives on. Unreachable ESPs (e.g. an unmounted Windows
    // ESP on Linux) are still picked up by the label/removable rules in
    // assembleEntries().
    QList<BootEntry> mounted;
    for (const Partition &p : cachedPartitions) {
        if (!isEsp(p))
            continue;
        bool release = false;
        const QString root = espScanRoot(p, release);
        if (root.isEmpty())
            continue;
        const QString runningName = isRunningSystemEsp(p) ? runningOsName() : QString();
        const QString volume = espVolumeId(p);
        QList<BootEntry> here = scanEspRoot(root, runningName);
        // Detection runs unprivileged, so a root-only ESP mount reads as empty
        // rather than as an error -- on the Steam Deck that silently hid both
        // SteamOS and Windows, leaving only label-matched media like Ventoy.
        // The firmware's boot variables are world-readable, so recover the
        // entries from there instead of showing nothing.
        // Prefer a cached elevated scan when the user has run one: it sees the
        // whole EFI/ tree, including loaders with no firmware boot entry.
        // Otherwise fall back to the boot variables, which cost no password.
        if (here.isEmpty() && espRootUnreadable(root)) {
            here = deepScanEntriesForEsp(p);
            if (here.isEmpty())
                here = firmwareEntriesForEsp(p);
            if (here.isEmpty())
                qWarning("ESP at %s is unreadable and no entries could be recovered from "
                         "the firmware; run the GUI's Deep Scan for a privileged scan",
                         qUtf8Printable(root));
        }
        for (BootEntry e : here) {
            if (e.volume.isEmpty())
                e.volume = volume;
            // Skip an exact duplicate (same loader on the same volume); a
            // display-name clash across different volumes is disambiguated so
            // both stay selectable.
            bool duplicate = false;
            bool nameClash = false;
            for (const BootEntry &existing : mounted) {
                if (existing.loaderPath == e.loaderPath && existing.volume == e.volume) {
                    duplicate = true;
                    break;
                }
                if (existing.displayName == e.displayName)
                    nameClash = true;
            }
            if (duplicate)
                continue;
            if (nameClash) {
                const QString tag = !p.label.isEmpty() ? p.label : e.volume.left(8);
                e.displayName = e.displayName + " (" + tag + ")";
            }
            mounted.append(e);
        }
        if (release)
            releaseEspRoot(root);
    }
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
