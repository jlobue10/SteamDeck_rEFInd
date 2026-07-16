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
        "    label=[string]$v.FileSystemLabel;"
        "    volpath=[string](@($_.AccessPaths) | Where-Object { $_ -like '\\\\?\\Volume*' } | Select-Object -First 1)"
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
        p.volumePath = obj.value(QLatin1String("volpath")).toString();
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
    // Administrator, which the Windows build always has).
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
        return {};
    }
    // A letterless non-system ESP (e.g. a Linux distro's ESP seen from
    // Windows) is mounted on a temporary directory via its \\?\Volume{guid}
    // name -- mountvol /S only handles the system ESP, and a directory mount
    // point doesn't consume a drive letter.
    if (!p.volumePath.isEmpty()) {
        const QString dir = QDir::toNativeSeparators(
            QDir::tempPath() + QStringLiteral("/refind-esp-scan-") + p.path);
        if (QDir().mkpath(dir)) {
            bool ok = false;
            runCommand(QStringLiteral("mountvol"), {dir, p.volumePath}, &ok);
            if (ok) {
                release = true;
                return dir;
            }
            QDir().rmdir(dir);
        }
    }
    return {};
}

void OSDetector::releaseEspRoot(const QString &root)
{
    runCommand(QStringLiteral("mountvol"), {root, QStringLiteral("/D")});
    // Directory mount points (anything longer than "Z:") were created by
    // espScanRoot and are removed once unmounted.
    if (root.length() > 2)
        QDir().rmdir(root);
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

bool OSDetector::isXboxAlly()
{
    bool ok = false;
    const QString product = runPowerShell(
        QStringLiteral("(Get-CimInstance -ClassName Win32_BaseBoard).Product"), &ok);
    if (!ok)
        return false;
    const QString board = product.trimmed();
    // RC73XA = ROG Xbox Ally X, RC73YA = ROG Xbox Ally (prefix match to allow
    // for board revision suffixes).
    return board.startsWith(QLatin1String("RC73XA")) || board.startsWith(QLatin1String("RC73YA"));
}

QSize OSDetector::nativePanelResolution()
{
    // Preferred timing (first 18-byte DTD, bytes 54-71) of the panel's EDID,
    // fetched via WMI. The internal display is preferred over external
    // outputs: VideoOutputTechnology 0x80000000 = INTERNAL, 11 = embedded
    // DisplayPort (eDP), 6 = LVDS.
    static const QString script = QStringLiteral(
        "$c=@(Get-CimInstance -Namespace root/wmi -ClassName WmiMonitorConnectionParams"
        " -ErrorAction SilentlyContinue | Where-Object { $_.Active });"
        "$p=$c | Where-Object { $_.VideoOutputTechnology -in @(2147483648,11,6) } | Select-Object -First 1;"
        "if(-not $p){$p=$c | Select-Object -First 1};"
        "if($p){"
        "  $m=Get-CimInstance -Namespace root/wmi -ClassName WmiMonitorDescriptorMethods"
        "   -ErrorAction SilentlyContinue | Where-Object { $_.InstanceName -eq $p.InstanceName } | Select-Object -First 1;"
        "  if($m){"
        "    $r=$m | Invoke-CimMethod -MethodName WmiGetMonitorRawEEdidV1Block -Arguments @{BlockId=[byte]0};"
        "    $e=$r.BlockContent;"
        "    if($e -and $e.Count -ge 72){"
        "      $h=$e[56]+(($e[58] -band 0xF0)*16);"
        "      $v=$e[59]+(($e[61] -band 0xF0)*16);"
        "      if($h -gt 0 -and $v -gt 0){('{0}x{1}' -f $h,$v)}"
        "    }"
        "  }"
        "}");
    bool ok = false;
    const QString out = runPowerShell(script, &ok).trimmed();
    if (!ok)
        return {};
    const QStringList parts = out.split('x');
    if (parts.size() != 2)
        return {};
    const int w = parts.at(0).toInt();
    const int h = parts.at(1).toInt();
    return (w > 0 && h > 0) ? QSize(w, h) : QSize();
}

QStringList OSDetector::runningOsIds()
{
    return {}; // no os-release on Windows; the static vendor-dir map decides
}

QString OSDetector::runningOsName()
{
    return {};
}
