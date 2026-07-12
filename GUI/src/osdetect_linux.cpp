#include "osdetect.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

QList<OSDetector::Partition> OSDetector::listPartitions()
{
    bool ok = false;
    // MOUNTPOINTS needs util-linux >= 2.37; fall back to MOUNTPOINT on older systems.
    QString out = runCommand(QStringLiteral("lsblk"),
                             {QStringLiteral("-J"), QStringLiteral("-o"),
                              QStringLiteral("PATH,TYPE,PARTTYPE,LABEL,PARTUUID,MOUNTPOINTS,RM,TRAN")}, &ok);
    if (!ok) {
        out = runCommand(QStringLiteral("lsblk"),
                         {QStringLiteral("-J"), QStringLiteral("-o"),
                          QStringLiteral("PATH,TYPE,PARTTYPE,LABEL,PARTUUID,MOUNTPOINT,RM,TRAN")}, &ok);
    }
    QList<Partition> partitions;
    if (!ok)
        return partitions;

    const QJsonDocument doc = QJsonDocument::fromJson(out.toUtf8());
    const QJsonArray disks = doc.object().value(QStringLiteral("blockdevices")).toArray();

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

    for (const QJsonValue &diskVal : disks) {
        const QJsonObject disk = diskVal.toObject();
        const QString diskTransport = readString(disk, "tran");
        const bool diskRemovable = readBool(disk, "rm");
        const QJsonArray children = disk.value(QStringLiteral("children")).toArray();
        for (const QJsonValue &childVal : children) {
            const QJsonObject part = childVal.toObject();
            if (readString(part, "type") != QLatin1String("part"))
                continue;
            Partition p;
            p.path = readString(part, "path");
            p.partType = readString(part, "parttype").toLower();
            p.label = readString(part, "label");
            p.partUuid = readString(part, "partuuid").toLower();
            p.mountPoint = readMountPoint(part);
            p.transport = readString(part, "tran");
            if (p.transport.isEmpty())
                p.transport = diskTransport;
            p.transport = p.transport.toLower();
            p.removable = readBool(part, "rm") || diskRemovable;
            partitions.append(p);
        }
    }
    return partitions;
}

QString OSDetector::acquireEspRoot(const QList<Partition> &partitions, bool &release)
{
    Q_UNUSED(partitions);
    release = false;
    // vfat lookups are case-insensitive, so "/EFI" matches however it is stored.
    // The Steam Deck mounts its ESP at /esp, so check that first.
    const QStringList candidates = {QStringLiteral("/esp"), QStringLiteral("/boot/efi"),
                                    QStringLiteral("/efi"), QStringLiteral("/boot")};
    for (const QString &mp : candidates) {
        if (QDir(mp + "/EFI").exists())
            return mp;
    }
    return {};
}

void OSDetector::releaseEspRoot(const QString &root)
{
    Q_UNUSED(root); // ESP stays mounted on Linux
}

bool OSDetector::isLegionGo()
{
    QFile board(QStringLiteral("/sys/devices/virtual/dmi/id/board_name"));
    if (!board.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    return QString::fromUtf8(board.readAll()).trimmed() == QStringLiteral("LNVNB161216");
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
