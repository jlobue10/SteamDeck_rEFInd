#include "osdetect.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
