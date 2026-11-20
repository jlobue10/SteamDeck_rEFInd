#include "osdetect.h"
#include "uki/uki.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QSysInfo>

#include <algorithm>

// GPT partition type GUID for EFI System Partitions (lowercase, no braces).
static const QString ESP_PARTTYPE_GPT = QStringLiteral("c12a7328-f81f-11d2-ba4b-00a0c93ec93b");
// MBR partition type for ESPs.
static const QString ESP_PARTTYPE_MBR = QStringLiteral("0xef");

QString OSDetector::efiArchSuffixOverride;

QString OSDetector::efiArchSuffix()
{
    if (!efiArchSuffixOverride.isEmpty())
        return efiArchSuffixOverride;
    const QString cpu = QSysInfo::currentCpuArchitecture();
    if (cpu == QLatin1String("arm64") || cpu == QLatin1String("aarch64"))
        return QStringLiteral("aa64");
    if (cpu == QLatin1String("i386") || cpu == QLatin1String("i686"))
        return QStringLiteral("ia32");
    return QStringLiteral("x64");
}

QStringList OSDetector::vendorLoaderCandidates()
{
    // Prefer shim (secure boot), then GRUB, then systemd-boot.
    const QString sfx = efiArchSuffix();
    return {QStringLiteral("shim") + sfx + QStringLiteral(".efi"),
            QStringLiteral("grub") + sfx + QStringLiteral(".efi"),
            QStringLiteral("systemd-boot") + sfx + QStringLiteral(".efi")};
}

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

// Fedora's uki-direct boots UKIs through shim and rewrites the vendor dir's
// BOOTX64.CSV (UTF-16 "loader,title,args,comment" lines) so every line
// launches an \EFI\Linux image. Its shim then has no OS of its own: run
// bare, it chainloads a GRUB with no config. Such a dir is skipped -- the
// UKI family is the entry -- unless GRUB really is configured there.
static bool isUkiLauncherDir(const QDir &vendorDir)
{
    if (QFile::exists(vendorDir.filePath(QStringLiteral("grub.cfg"))))
        return false;
    // Only this architecture's shim CSV (BOOTX64.CSV, BOOTAA64.CSV): on
    // x86-64 the ia32 one beside it keeps the stock GRUB line on a
    // uki-direct install.
    const QString csvName = QStringLiteral("BOOT") + OSDetector::efiArchSuffix().toUpper()
                            + QStringLiteral(".CSV");
    bool sawCsv = false;
    for (const QString &csv : vendorDir.entryList(QDir::Files)) {
        if (csv.compare(csvName, Qt::CaseInsensitive) != 0)
            continue;
        QFile f(vendorDir.filePath(csv));
        if (!f.open(QIODevice::ReadOnly))
            return false;
        const QByteArray raw = f.read(64 * 1024);
        QString text = raw.startsWith("\xff\xfe")
                           ? QString::fromUtf16(reinterpret_cast<const char16_t *>(raw.constData() + 2),
                                                (raw.size() - 2) / 2)
                           : QString::fromUtf8(raw);
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            const QString t = line.trimmed();
            if (t.isEmpty())
                continue;
            sawCsv = true;
            const QString args = t.section(QLatin1Char(','), 2, 2).trimmed();
            if (!args.startsWith(QLatin1String("\\EFI\\Linux\\"), Qt::CaseInsensitive))
                return false;
        }
    }
    return sawCsv;
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
        QStringLiteral("shellaa64"), QStringLiteral("shellia32"),
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
        // Generic Linux vendor dir: prefer shim (secure boot), then GRUB, then
        // systemd-boot, under this architecture's names.
        const QStringList loaderCandidates = vendorLoaderCandidates();
        QString loaderFile;
        for (const QString &candidate : loaderCandidates) {
            if (QFile::exists(sub.filePath(candidate))) {
                loaderFile = candidate;
                break;
            }
        }
        if (loaderFile.isEmpty())
            continue;
        if (isUkiLauncherDir(sub))
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

    // Unified Kernel Images in /EFI/Linux (kernel-install layout=uki, or any
    // ukify/mkinitcpio UKI there): one entry per entry-token family, so a
    // slot survives kernel updates. The stanza is written with every image
    // of the family and re-pointed by the kernel-install plugin.
    for (const Uki::Family &fam : Uki::scanFamilies(rootPath)) {
        BootEntry e;
        e.displayName = displayNameForUkiToken(fam.token, fam.osName, runningDistroName);
        e.loaderPath = fam.loaders.first();
        e.ukiToken = fam.token;
        e.ukiLoaders = fam.loaders;
        e.ukiVersions = fam.versions;
        e.ukiOsId = fam.osId;
        // Two families of one OS (e.g. arch-linux and arch-linux-lts) or a
        // family named like a vendor-dir loader on this ESP: tell them apart.
        const auto clashes = [&e](const BootEntry &other) {
            return other.displayName == e.displayName;
        };
        if (std::any_of(linuxEntries.cbegin(), linuxEntries.cend(), clashes))
            e.displayName += QStringLiteral(" (") + (Uki::isMachineIdToken(fam.token)
                                                         ? QStringLiteral("UKI")
                                                         : fam.token)
                + QLatin1Char(')');
        e.menuName = e.displayName;
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

    const QString sfx = efiArchSuffix();
    for (const Partition &p : partitions) {
        // Removable media recognized by well-known labels, ESP-typed or not.
        if (p.label.compare(QLatin1String("BATOCERA"), Qt::CaseInsensitive) == 0) {
            addUnique({QStringLiteral("Batocera"), QStringLiteral("Batocera"),
                       QStringLiteral("/EFI/BOOT/boot") + sfx + QStringLiteral(".efi"),
                       QStringLiteral("BATOCERA"), false});
            continue;
        }
        if (p.label.compare(QLatin1String("VTOYEFI"), Qt::CaseInsensitive) == 0) {
            addUnique({QStringLiteral("Ventoy"), QStringLiteral("Ventoy"),
                       QStringLiteral("/EFI/BOOT/grub") + sfx + QStringLiteral("_real.efi"),
                       QStringLiteral("VTOYEFI"), false});
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

    // /EFI/Linux/<token>-<version>.efi — a Unified Kernel Image. Named from
    // the file name alone (recovery paths have no file contents); detect()
    // folds the images of one token into a single entry.
    Uki::Image uki;
    if (Uki::imageFromLoaderPath(loaderPath, &uki)) {
        entry.displayName = displayNameForUkiToken(uki.token, QString(), QString());
        entry.menuName = entry.displayName;
        entry.loaderPath = loaderPath;
        entry.ukiToken = uki.token;
        entry.ukiLoaders = QStringList{loaderPath};
        entry.ukiVersions = QStringList{uki.kernelVersion};
        return true;
    }
    // Anything else directly in /EFI/Linux is a UKI name that can't be used
    // unquoted in refind.conf, not a vendor dir called "Linux".
    if (lower.startsWith(QLatin1String("/efi/linux/")) && lower.count(QLatin1Char('/')) == 3)
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
        QStringLiteral("shellaa64"), QStringLiteral("shellia32"),
    };
    if (skipDirs.contains(vendorLower) || vendorLower.startsWith(QLatin1String("drivers"))
        || vendorLower.startsWith(QLatin1String("tools")))
        return false;

    entry.displayName = displayNameForVendorDir(loaderPath.section('/', 2, 2));
    entry.menuName = entry.displayName;
    entry.loaderPath = loaderPath;
    return true;
}

QString OSDetector::displayNameForUkiToken(const QString &token, const QString &osName,
                                          const QString &runningDistroName)
{
    if (!osName.isEmpty()) {
        // Same normalization as runningOsName() ("Fedora Linux" -> "Fedora"),
        // so a family is named alike whether its .osrel was readable or the
        // running distro's name stood in for it.
        QString name = osName.trimmed();
        if (name.endsWith(QLatin1String(" Linux")) && name.size() > 6)
            name.chop(6);
        return name.trimmed();
    }
    if (Uki::isMachineIdToken(token)) {
        // kernel-install's default entry token is /etc/machine-id: on this
        // machine's own images that names the running distro.
        QFile id(QStringLiteral("/etc/machine-id"));
        if (id.open(QIODevice::ReadOnly | QIODevice::Text)
            && QString::fromLatin1(id.readLine()).trimmed().compare(token, Qt::CaseInsensitive) == 0) {
            const QString running = !runningDistroName.isEmpty() ? runningDistroName : runningOsName();
            if (!running.isEmpty())
                return running;
        }
        return QStringLiteral("Linux");
    }
    const QString lower = token.toLower();
    if (knownDistroNames().contains(lower))
        return knownDistroNames().value(lower);
    return QStringLiteral("Linux");
}

QList<BootEntry> OSDetector::mergeUkiEntries(const QList<BootEntry> &entries)
{
    QList<BootEntry> out;
    for (const BootEntry &e : entries) {
        if (e.ukiToken.isEmpty()) {
            out.append(e);
            continue;
        }
        BootEntry *family = nullptr;
        for (BootEntry &existing : out) {
            if (existing.ukiToken == e.ukiToken && existing.volume == e.volume) {
                family = &existing;
                break;
            }
        }
        if (!family) {
            out.append(e);
            continue;
        }
        for (int i = 0; i < e.ukiLoaders.size(); ++i) {
            if (family->ukiLoaders.contains(e.ukiLoaders.at(i)))
                continue;
            family->ukiLoaders.append(e.ukiLoaders.at(i));
            family->ukiVersions.append(i < e.ukiVersions.size() ? e.ukiVersions.at(i) : QString());
        }
    }
    // Newest first within each family; the loader follows the newest image.
    for (BootEntry &e : out) {
        if (e.ukiToken.isEmpty() || e.ukiLoaders.size() < 2)
            continue;
        QList<int> order;
        for (int i = 0; i < e.ukiLoaders.size(); ++i)
            order.append(i);
        std::sort(order.begin(), order.end(), [&e](int a, int b) {
            const int c = Uki::compareVersions(e.ukiVersions.value(a), e.ukiVersions.value(b));
            if (c != 0)
                return c > 0;
            return e.ukiLoaders.at(a) > e.ukiLoaders.at(b);
        });
        QStringList loaders, versions;
        for (int i : order) {
            loaders.append(e.ukiLoaders.at(i));
            versions.append(e.ukiVersions.value(i));
        }
        e.ukiLoaders = loaders;
        e.ukiVersions = versions;
        e.loaderPath = loaders.first();
    }
    return out;
}

void OSDetector::adoptRunningOsForUkis(QList<BootEntry> &entries)
{
    // Names-only UKI entries (unreadable ESP: no .osrel) on the running
    // system's own ESP: a tokenless image, or one whose token is this
    // machine's machine-id, is the running OS's kernel. Recording its os-release
    // ID in the stanza marker lets uki-refresh follow the family when the
    // token changes.
    QString id, name;
    QFile f(QStringLiteral("/etc/os-release"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        for (const QString &line : QString::fromUtf8(f.readAll()).split('\n')) {
            if (line.startsWith(QLatin1String("ID=")))
                id = line.mid(3).remove('"').remove('\'').trimmed();
        }
    }
    name = runningOsName();
    QString machineId;
    QFile mid(QStringLiteral("/etc/machine-id"));
    if (mid.open(QIODevice::ReadOnly | QIODevice::Text))
        machineId = QString::fromLatin1(mid.readLine()).trimmed();
    for (BootEntry &e : entries) {
        if (e.ukiToken.isEmpty() || !e.ukiOsId.isEmpty())
            continue;
        const bool ours = e.ukiToken == QLatin1String(Uki::kNoToken)
                          || (!machineId.isEmpty()
                              && e.ukiToken.compare(machineId, Qt::CaseInsensitive) == 0);
        if (!ours)
            continue;
        e.ukiOsId = id;
        if (!name.isEmpty() && e.displayName == QLatin1String("Linux")) {
            e.displayName = name;
            e.menuName = name;
        }
    }
    // The tokenless images and the machine-id family are then one install
    // (as Uki::groupImages() decides from .osrel when it can read them):
    // fold the tokenless ones in as older kernels.
    int bare = -1, own = -1;
    for (int i = 0; i < entries.size(); ++i) {
        if (entries.at(i).ukiToken == QLatin1String(Uki::kNoToken))
            bare = i;
        else if (!machineId.isEmpty()
                 && entries.at(i).ukiToken.compare(machineId, Qt::CaseInsensitive) == 0)
            own = i;
    }
    if (bare >= 0 && own >= 0) {
        entries[own].ukiLoaders += entries.at(bare).ukiLoaders;
        entries[own].ukiVersions += entries.at(bare).ukiVersions;
        entries.removeAt(bare);
        entries = mergeUkiEntries(entries); // re-sorts the family newest first
    }

    // This machine's kernels whose /usr/lib/modules tree is gone stay out of
    // the menu, exactly as uki-refresh decides (Fedora's type1 kernel removal
    // leaves their UKIs on the ESP).
    if (Uki::modulesTreeUsable()) {
        for (BootEntry &e : entries) {
            if (e.ukiToken.isEmpty()
                || !Uki::isThisMachinesFamily(e.ukiToken, e.ukiOsId, machineId, id))
                continue;
            Uki::dropUninstalledKernels(&e.ukiLoaders, &e.ukiVersions,
                                        [](const QString &v) { return Uki::kernelInstalled(v); });
            e.loaderPath = e.ukiLoaders.value(0, e.loaderPath);
        }
    }
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
        const QString volume = espVolumeId(p);
        QList<BootEntry> here;
        if (root.isEmpty()) {
            // Unmounted ESP with no scan root (e.g. a systemd automount that
            // wasn't established yet -- SteamOS mounts /esp that way -- or an
            // ESP the running system simply never mounts). Skipping it hid
            // every OS on it; recover its entries from the deep-scan cache or
            // the firmware's boot variables instead, keyed by partition GUID.
            // The label/removable rules in assembleEntries() still cover
            // whatever this can't see (their haveOnVolume() guard prevents
            // duplicates).
            here = deepScanEntriesForEsp(p);
            if (here.isEmpty())
                here = firmwareEntriesForEsp(p);
        } else {
            const QString runningName = isRunningSystemEsp(p) ? runningOsName() : QString();
            here = scanEspRoot(root, runningName);
            // Detection runs unprivileged, so a root-only ESP mount reads as
            // empty rather than as an error -- on the Steam Deck that silently
            // hid both SteamOS and Windows, leaving only label-matched media
            // like Ventoy. The firmware's boot variables are world-readable,
            // so recover the entries from there instead of showing nothing.
            // Prefer a cached elevated scan when the user has run one: it sees
            // the whole EFI/ tree, including loaders with no firmware boot
            // entry. Otherwise fall back to the boot variables, which cost no
            // password.
            if (here.isEmpty() && espRootUnreadable(root)) {
                here = deepScanEntriesForEsp(p);
                if (here.isEmpty())
                    here = firmwareEntriesForEsp(p);
                if (here.isEmpty())
                    qWarning("ESP at %s is unreadable and no entries could be recovered "
                             "from the firmware; run the GUI's Deep Scan for a "
                             "privileged scan",
                             qUtf8Printable(root));
            }
        }
        // Recovery paths list one entry per UKI file; fold each entry-token
        // family into one slottable entry (a no-op for scanEspRoot()'s).
        here = mergeUkiEntries(here);
        if (isRunningSystemEsp(p))
            adoptRunningOsForUkis(here);
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
