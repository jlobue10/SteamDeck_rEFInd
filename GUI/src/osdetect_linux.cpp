#include "osdetect.h"

#include "platform.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <functional>

QList<OSDetector::Partition> OSDetector::listPartitions()
{
    bool ok = false;
    // NAME must be in the column list: without it lsblk can't render its device
    // tree and emits partitions as a flat top-level list instead of nesting
    // them under the disk (seen on util-linux 2.42). We parse recursively below
    // so either shape works, but requesting NAME keeps the nested form too.
    // MOUNTPOINTS needs util-linux >= 2.37; fall back to MOUNTPOINT on older systems.
    QString out = runCommand(QStringLiteral("lsblk"),
                             {QStringLiteral("-J"), QStringLiteral("-o"),
                              QStringLiteral("NAME,PATH,TYPE,PARTTYPE,LABEL,PARTUUID,MOUNTPOINTS,RM,TRAN")}, &ok);
    if (!ok) {
        out = runCommand(QStringLiteral("lsblk"),
                         {QStringLiteral("-J"), QStringLiteral("-o"),
                          QStringLiteral("NAME,PATH,TYPE,PARTTYPE,LABEL,PARTUUID,MOUNTPOINT,RM,TRAN")}, &ok);
    }
    QList<Partition> partitions;
    if (!ok)
        return partitions;

    const QJsonDocument doc = QJsonDocument::fromJson(out.toUtf8());
    const QJsonArray blockdevices = doc.object().value(QStringLiteral("blockdevices")).toArray();

    auto readString = [](const QJsonObject &obj, const char *key) {
        const QJsonValue v = obj.value(QLatin1String(key));
        return v.isString() ? v.toString() : QString();
    };
    auto readBool = [](const QJsonObject &obj, const char *key) {
        const QJsonValue v = obj.value(QLatin1String(key));
        if (v.isBool())
            return v.toBool();
        return v.toString() == QLatin1String("1");
    };
    auto readMountPoint = [](const QJsonObject &obj) {
        const QJsonValue multi = obj.value(QLatin1String("mountpoints"));
        if (multi.isArray()) {
            const QJsonArray arr = multi.toArray();
            for (const QJsonValue &v : arr) {
                if (v.isString() && !v.toString().isEmpty())
                    return v.toString();
            }
            return QString();
        }
        const QJsonValue single = obj.value(QLatin1String("mountpoint"));
        return single.isString() ? single.toString() : QString();
    };

    // Walk the device tree, collecting every "part" node wherever it sits
    // (nested under a disk, or flat at the top level). transport/removable are
    // inherited from the enclosing disk when the partition doesn't carry them.
    std::function<void(const QJsonObject &, const QString &, bool)> walk =
        [&](const QJsonObject &node, const QString &inheritedTransport, bool inheritedRemovable) {
            QString transport = readString(node, "tran");
            if (transport.isEmpty())
                transport = inheritedTransport;
            const bool removable = readBool(node, "rm") || inheritedRemovable;
            if (readString(node, "type") == QLatin1String("part")) {
                Partition p;
                p.path = readString(node, "path");
                p.partType = readString(node, "parttype").toLower();
                p.label = readString(node, "label");
                p.partUuid = readString(node, "partuuid").toLower();
                p.mountPoint = readMountPoint(node);
                p.transport = transport.toLower();
                p.removable = removable;
                partitions.append(p);
            }
            const QJsonArray children = node.value(QStringLiteral("children")).toArray();
            for (const QJsonValue &childVal : children)
                walk(childVal.toObject(), transport, removable);
        };
    for (const QJsonValue &devVal : blockdevices)
        walk(devVal.toObject(), QString(), false);
    return partitions;
}

QString OSDetector::espScanRoot(const Partition &p, bool &release)
{
    release = false;
    // On Linux an ESP is scannable only where it is already mounted; mounting
    // an unmounted ESP would need root, which detection deliberately avoids.
    // Unmounted ESPs fall through to the label/removable rules instead.
    return p.mountPoint;
}

void OSDetector::releaseEspRoot(const QString &root)
{
    Q_UNUSED(root); // nothing is mounted by espScanRoot() on Linux
}

QList<BootEntry> OSDetector::firmwareEntriesForEsp(const Partition &p)
{
    QList<BootEntry> entries;
    // Entries are matched to this ESP by partition GUID, so without one there
    // is nothing to anchor them to.
    if (p.partUuid.isEmpty())
        return entries;

    bool ok = false;
    const QString out = runCommand(QStringLiteral("efibootmgr"), {QStringLiteral("-v")}, &ok);
    if (!ok)
        return entries;

    // Boot0000* SteamOS\tHD(1,GPT,<guid>,0x800,0x100000)/\EFI\steamos\steamcl.efi
    //
    // Windows appends an optional-data blob straight after the loader path
    // ("...bootmgfw.efi57494e444f5753..."), so stop at the first ".efi" rather
    // than running to end of line. efibootmgr < 18 renders the path as
    // /File(\EFI\...\loader.efi) instead of a bare /\EFI\... — accept both.
    static const QRegularExpression entryRe(
        QStringLiteral("HD\\(\\d+,GPT,([0-9a-f-]{36}),[^)]*\\)/(?:File\\()?(\\\\[^\\s)]*?\\.efi)"),
        QRegularExpression::CaseInsensitiveOption);

    const QStringList lines = out.split('\n');
    for (const QString &line : lines) {
        const QRegularExpressionMatch m = entryRe.match(line);
        if (!m.hasMatch() || m.captured(1).toLower() != p.partUuid)
            continue;

        QString loader = m.captured(2);
        loader.replace('\\', '/'); // \EFI\steamos\steamcl.efi -> /EFI/steamos/steamcl.efi

        // Name entries the way a filesystem scan would, rather than from the
        // NVRAM label -- those are user-editable and often renamed.
        BootEntry e;
        if (classifyLoaderPath(loader, e))
            entries.append(e);
    }
    return entries;
}

QList<BootEntry> OSDetector::deepScanEntriesForEsp(const Partition &p)
{
    QList<BootEntry> entries;
    if (p.partUuid.isEmpty())
        return entries;

    // Written by scripts/scan_esp.sh, which the user runs on demand from the
    // GUI. Sections are keyed by partition GUID:
    //   [b9beb192-116a-4bd2-8f09-5c703af03a5f]
    //   /EFI/steamos/steamcl.efi
    QFile f(Platform::dataDir() + QStringLiteral("/GUI/esp_scan.conf"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return entries;

    // Mirror scanEspRoot()'s per-vendor choice: one entry per OS, preferring
    // shim (secure boot), then GRUB, then systemd-boot. The scan lists every
    // .efi it finds, so without this a distro shipping both shimx64.efi and
    // grubx64.efi would surface twice and detect()'s name-clash rule would
    // rename the second copy to "Fedora (esp)".
    static const QStringList loaderPreference = {
        QStringLiteral("shimx64.efi"), QStringLiteral("grubx64.efi"),
        QStringLiteral("systemd-bootx64.efi"),
    };
    auto rank = [](const QString &loaderPath) {
        const int i = loaderPreference.indexOf(loaderPath.section('/', -1).toLower());
        return i < 0 ? loaderPreference.size() : i;
    };

    bool inSection = false;
    const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        if (line.startsWith('[') && line.endsWith(']')) {
            inSection = line.mid(1, line.size() - 2).toLower() == p.partUuid;
            continue;
        }
        if (!inSection || !line.startsWith('/'))
            continue;
        BootEntry e;
        if (!classifyLoaderPath(line, e))
            continue;
        int existing = -1;
        for (int i = 0; i < entries.size(); ++i) {
            if (entries[i].displayName == e.displayName) {
                existing = i;
                break;
            }
        }
        if (existing < 0)
            entries.append(e);
        else if (rank(e.loaderPath) < rank(entries[existing].loaderPath))
            entries[existing] = e;
    }
    return entries;
}

static QString readDmiId(const char *name)
{
    QFile f(QStringLiteral("/sys/devices/virtual/dmi/id/") + QLatin1String(name));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

bool OSDetector::isLegionGo()
{
    // The Legion Go 2 reports the same board name, so exclude it here.
    return readDmiId("board_name") == QStringLiteral("LNVNB161216") && !isLegionGo2();
}

bool OSDetector::isLegionGo2()
{
    // Machine-type product names, as matched by the kernel's pmc quirk list.
    const QString product = readDmiId("product_name");
    return product == QStringLiteral("83N0") || product == QStringLiteral("83N1");
}

bool OSDetector::isXboxAlly()
{
    const QString board = readDmiId("board_name");
    // RC73XA = ROG Xbox Ally X, RC73YA = ROG Xbox Ally (prefix match to allow
    // for board revision suffixes).
    return board.startsWith(QLatin1String("RC73XA")) || board.startsWith(QLatin1String("RC73YA"));
}

QSize OSDetector::nativePanelResolution()
{
    // The first line of a connected DRM connector's modes list is its
    // preferred (native) mode. Internal panels (eDP/LVDS/DSI) are checked
    // first so a docked handheld still reports its built-in screen.
    const QDir drm(QStringLiteral("/sys/class/drm"));
    const QStringList connectors = drm.entryList({QStringLiteral("card*-*")},
                                                 QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    static const QStringList internalPrefixes = {
        QStringLiteral("eDP"), QStringLiteral("LVDS"), QStringLiteral("DSI"),
    };
    static const QRegularExpression modeRe(QStringLiteral("^(\\d+)x(\\d+)"));
    for (int pass = 0; pass < 2; ++pass) {
        for (const QString &name : connectors) {
            const QString connector = name.section('-', 1, -1); // "card1-eDP-1" -> "eDP-1"
            bool internal = false;
            for (const QString &prefix : internalPrefixes) {
                if (connector.startsWith(prefix, Qt::CaseInsensitive)) {
                    internal = true;
                    break;
                }
            }
            if ((pass == 0) != internal)
                continue;
            QFile status(drm.filePath(name) + "/status");
            if (!status.open(QIODevice::ReadOnly | QIODevice::Text)
                || QString::fromUtf8(status.readAll()).trimmed() != QLatin1String("connected"))
                continue;
            QFile modes(drm.filePath(name) + "/modes");
            if (!modes.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            const QRegularExpressionMatch m =
                modeRe.match(QString::fromUtf8(modes.readLine()).trimmed());
            if (m.hasMatch())
                return QSize(m.captured(1).toInt(), m.captured(2).toInt());
        }
    }
    return {};
}

QStringList OSDetector::runningOsIds()
{
    QStringList ids;
    QFile f(QStringLiteral("/etc/os-release"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return ids;
    const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("ID=")) || line.startsWith(QLatin1String("ID_LIKE="))) {
            QString value = line.section('=', 1);
            value.remove('"');
            ids += value.toLower().split(' ', Qt::SkipEmptyParts);
        }
    }
    return ids;
}

QString OSDetector::runningOsName()
{
    QFile f(QStringLiteral("/etc/os-release"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("NAME="))) {
            QString name = line.section('=', 1);
            name.remove('"');
            // "Nobara Linux" -> "Nobara", "Fedora Linux" -> "Fedora"
            if (name.endsWith(QLatin1String(" Linux")))
                name.chop(6);
            return name.trimmed();
        }
    }
    return {};
}
