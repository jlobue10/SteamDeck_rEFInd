#include "osdetect.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// All system queries go through Windows PowerShell (always present on
// Windows 10/11) with -Command passed as a single QProcess argument.
static QString runPowerShell(const QString &command, bool *ok = nullptr)
{
    return OSDetector::runCommand(QStringLiteral("powershell.exe"),
                                  {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                                   QStringLiteral("Bypass"), QStringLiteral("-Command"), command},
                                  ok);
}

static QString normalizeGuid(QString guid)
{
    guid = guid.trimmed().toLower();
    guid.remove('{');
    guid.remove('}');
    return guid;
}

QList<OSDetector::Partition> OSDetector::listPartitions()
{
    static const QString script = QStringLiteral(
        "$ErrorActionPreference='SilentlyContinue';"
        "Get-Partition | ForEach-Object {"
        "  $d = Get-Disk -Number $_.DiskNumber;"
        "  $v = $_ | Get-Volume;"
        "  [pscustomobject]@{"
        "    disk=$_.DiskNumber; part=$_.PartitionNumber;"
        "    gpttype=[string]$_.GptType; mbrtype=[int]$_.MbrType;"
        "    guid=[string]$_.Guid; letter=[string]$_.DriveLetter;"
        "    issystem=[bool]$_.IsSystem; bustype=[string]$d.BusType;"
        "    removable=[bool]($d.BusType -in @('USB','SD','MMC'));"
        "    label=[string]$v.FileSystemLabel"
        "  }"
        "} | ConvertTo-Json -Compress");

    QList<Partition> partitions;
    bool ok = false;
    const QString out = runPowerShell(script, &ok);
    if (!ok || out.trimmed().isEmpty())
        return partitions;

    const QJsonDocument doc = QJsonDocument::fromJson(out.toUtf8());
    QJsonArray items;
    if (doc.isArray())
        items = doc.array();
    else if (doc.isObject())
        items.append(doc.object()); // single partition serializes as an object

    for (const QJsonValue &val : items) {
        const QJsonObject obj = val.toObject();
        Partition p;
        p.path = QStringLiteral("disk%1p%2")
                     .arg(obj.value(QLatin1String("disk")).toInt())
                     .arg(obj.value(QLatin1String("part")).toInt());
        const QString gptType = normalizeGuid(obj.value(QLatin1String("gpttype")).toString());
        if (!gptType.isEmpty())
            p.partType = gptType;
        else if (obj.value(QLatin1String("mbrtype")).toInt() == 0xEF)
            p.partType = QStringLiteral("0xef");
        p.label = obj.value(QLatin1String("label")).toString();
        p.partUuid = normalizeGuid(obj.value(QLatin1String("guid")).toString());
        // Letterless partitions serialize DriveLetter as NUL ("\0"), so
        // require an actual ASCII letter rather than just a non-empty string.
        const QString letter = obj.value(QLatin1String("letter")).toString().trimmed();
        if (letter.length() == 1 && letter.at(0).isLetter())
            p.mountPoint = letter.toUpper() + QLatin1Char(':');
        else if (obj.value(QLatin1String("issystem")).toBool())
            p.mountPoint = QStringLiteral("(system)"); // in use by Windows; not "free" for cross-volume logic
        p.transport = obj.value(QLatin1String("bustype")).toString().toLower();
        p.removable = obj.value(QLatin1String("removable")).toBool();
        partitions.append(p);
    }
    return partitions;
}

QString OSDetector::espScanRoot(const Partition &p, bool &release)
{
    release = false;
    // An ESP that already has a drive letter is scanned directly.
    if (!p.mountPoint.isEmpty() && p.mountPoint != QLatin1String("(system)"))
        return p.mountPoint;
    // The letterless system ESP is mounted on a free letter (requires
    // Administrator, which the Windows build always has). Non-system ESPs
    // without a letter can't be mounted this way (mountvol /S targets only the
    // system ESP), so they fall through to the label/removable rules.
    if (p.mountPoint == QLatin1String("(system)")) {
        for (char letter = 'Z'; letter >= 'T'; --letter) {
            const QString drive = QString(QLatin1Char(letter)) + QLatin1Char(':');
            if (QDir(drive + "/").exists())
                continue;
            bool ok = false;
            runCommand(QStringLiteral("mountvol"), {drive, QStringLiteral("/S")}, &ok);
            if (ok) {
                release = true;
                return drive;
            }
        }
    }
    return {};
}

void OSDetector::releaseEspRoot(const QString &root)
{
    runCommand(QStringLiteral("mountvol"), {root, QStringLiteral("/D")});
}

bool OSDetector::isLegionGo()
{
    bool ok = false;
    const QString product = runPowerShell(
        QStringLiteral("(Get-CimInstance -ClassName Win32_BaseBoard).Product"), &ok);
    // The Legion Go 2 reports the same baseboard product, so exclude it here.
    return ok && product.trimmed() == QStringLiteral("LNVNB161216") && !isLegionGo2();
}

bool OSDetector::isLegionGo2()
{
    bool ok = false;
    // SMBIOS product name (machine type), e.g. "83E1" on the original Legion Go.
    const QString model = runPowerShell(
        QStringLiteral("(Get-CimInstance -ClassName Win32_ComputerSystem).Model"), &ok);
    if (!ok)
        return false;
    const QString mt = model.trimmed();
    return mt == QStringLiteral("83N0") || mt == QStringLiteral("83N1");
}

QStringList OSDetector::runningOsIds()
{
    return {}; // no os-release on Windows; the static vendor-dir map decides
}

QString OSDetector::runningOsName()
{
    return {};
}
