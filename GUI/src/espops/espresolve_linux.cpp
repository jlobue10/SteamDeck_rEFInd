// Parity-locked between rEFInd_GUI and SteamDeck_rEFInd — see loadoption.h.
//
// Linux implementation of the rEFInd-ESP resolution (needs root: ESPs are
// mounted 0700 root:root, and unmounted candidates get probe-mounted).
// NVRAM is READ natively (efivarfs bytes through the shared load-option
// parser); NVRAM is never WRITTEN here — writes stay with efibootmgr in the
// install/uninstall scripts (NATIVE_HELPER_DESIGN.md §2.6).

#include "espconstants.h"
#include "espresolve.h"
#include "loadoption.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

#include <cstdlib>
#include <sys/mount.h> // only for umount(2) in cleanup; mounting execs mount(8)
#include <unistd.h>

namespace EspOps {

namespace {

const char kEfivars[] = "/sys/firmware/efi/efivars";
const char kGlobalGuid[] = "8be4df61-93ca-11d2-aa0d-00e098032b8c";
const char kEspTypeGuid[] = "c12a7328-f81f-11d2-ba4b-00a0c93ec93b";

QByteArray readEfiVar(const QString &name)
{
    QFile f(QString::fromLatin1(kEfivars) + QLatin1Char('/') + name
            + QLatin1Char('-') + QLatin1String(kGlobalGuid));
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return fromEfivarsPayload(f.readAll());
}

// Absolute path of an external tool, System paths only — never PATH lookup
// (this code runs as root).
QString systemTool(const char *name)
{
    const QString candidates[] = {
        QLatin1String("/usr/bin/") + QLatin1String(name),
        QLatin1String("/usr/sbin/") + QLatin1String(name),
        QLatin1String("/bin/") + QLatin1String(name),
        QLatin1String("/sbin/") + QLatin1String(name),
    };
    for (const QString &c : candidates) {
        if (QFileInfo::exists(c))
            return c;
    }
    return QString();
}

int runTool(const QString &tool, const QStringList &args, QByteArray *stdOut)
{
    if (tool.isEmpty())
        return -1;
    QProcess p;
    p.setProcessChannelMode(QProcess::SeparateChannels);
    p.start(tool, args);
    if (!p.waitForStarted(-1))
        return -1;
    p.closeWriteChannel();
    if (!p.waitForFinished(-1))
        return -1;
    if (stdOut)
        *stdOut = p.readAllStandardOutput();
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
}

// Device path for a partition GUID. /dev/disk/by-partuuid is udev-standard
// on every target distro; lsblk output parsing is kept as the fallback for
// initramfs-less oddballs (same data source the scripts used).
QString deviceForPartUuid(const QString &guid)
{
    const QFileInfo link(QLatin1String("/dev/disk/by-partuuid/") + guid);
    if (link.exists())
        return link.canonicalFilePath();
    QByteArray outBytes;
    if (runTool(systemTool("lsblk"),
                {QStringLiteral("-rno"), QStringLiteral("PATH,PARTUUID")},
                &outBytes)
        != 0)
        return QString();
    const QStringList lines =
        QString::fromLocal8Bit(outBytes).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QStringList cols = line.split(QLatin1Char(' '));
        if (cols.size() >= 2 && cols.at(1).compare(guid, Qt::CaseInsensitive) == 0)
            return cols.at(0);
    }
    return QString();
}

// All ESP-typed partitions (device paths), for the "any ESP containing
// rEFInd" tier.
QStringList espPartitions()
{
    QByteArray outBytes;
    if (runTool(systemTool("lsblk"),
                {QStringLiteral("-rno"), QStringLiteral("PATH,PARTTYPE")},
                &outBytes)
        != 0)
        return {};
    QStringList devices;
    const QStringList lines =
        QString::fromLocal8Bit(outBytes).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QStringList cols = line.split(QLatin1Char(' '));
        if (cols.size() >= 2
            && cols.at(1).compare(QLatin1String(kEspTypeGuid),
                                  Qt::CaseInsensitive)
                == 0)
            devices << cols.at(0);
    }
    return devices;
}

// Existing mountpoint for a device, from /proc/self/mounts — the native
// equivalent of `findmnt -S <dev>`, which is immune to the automount trap
// (the autofs row's source is "systemd-1", not the device).
QString mountPointOf(const QString &devicePath)
{
    const QString canonical = QFileInfo(devicePath).canonicalFilePath();
    QFile mounts(QStringLiteral("/proc/self/mounts"));
    if (!mounts.open(QIODevice::ReadOnly))
        return QString();
    while (!mounts.atEnd()) {
        const QString line = QString::fromLocal8Bit(mounts.readLine());
        const QStringList cols = line.split(QLatin1Char(' '));
        if (cols.size() < 2 || !cols.at(0).startsWith(QLatin1String("/dev/")))
            continue;
        if (QFileInfo(cols.at(0)).canonicalFilePath() == canonical) {
            // Octal escapes (\040 for space) per fstab(5).
            QString mp = cols.at(1);
            static const QRegularExpression esc(QStringLiteral("\\\\([0-7]{3})"));
            QRegularExpressionMatch m;
            while ((m = esc.match(mp)).hasMatch()) {
                mp.replace(m.capturedStart(), m.capturedLength(),
                           QChar(m.captured(1).toInt(nullptr, 8)));
            }
            return mp;
        }
    }
    return QString();
}

bool hasRefind(const QString &mountPoint)
{
    const QDir d(mountPoint + QLatin1String("/EFI/refind"));
    return !d.entryList({QStringLiteral("refind*.efi")}, QDir::Files).isEmpty();
}

} // namespace

QString refindEspGuidFromNvram()
{
    // BootOrder-listed entries first, then the rest in numeric order (the
    // scripts appended entries missing from BootOrder "last, still worth
    // checking").
    QList<quint16> ids = parseBootOrder(readEfiVar(QStringLiteral("BootOrder")));
    QList<quint16> all;
    const QDir efivars{QString::fromLatin1(kEfivars)};
    static const QRegularExpression bootName(
        QStringLiteral("^Boot([0-9A-Fa-f]{4})-") + QLatin1String(kGlobalGuid)
        + QStringLiteral("$"));
    QStringList names = efivars.entryList(QDir::Files);
    names.sort();
    for (const QString &n : names) {
        const QRegularExpressionMatch m = bootName.match(n);
        if (m.hasMatch())
            all << quint16(m.captured(1).toUInt(nullptr, 16));
    }
    for (quint16 id : all) {
        if (!ids.contains(id))
            ids << id;
    }

    // Two tiers, matching the scripts and the PowerShell counterpart: a
    // \EFI\refind\refind*.efi loader path first, then an entry labelled
    // exactly "rEFInd" for firmwares that render the path in a form the
    // strict match misses.
    for (int tier = 0; tier < 2; ++tier) {
        for (quint16 id : ids) {
            const LoadOption o = parseLoadOption(readEfiVar(formatBootNum(id)));
            if (!o.valid || o.gptPartitionGuid.isEmpty())
                continue;
            const bool hit = tier == 0
                ? loaderLooksLikeRefind(o.loaderPath)
                : o.description == QLatin1String("rEFInd");
            if (hit)
                return o.gptPartitionGuid;
        }
    }
    return QString();
}

EspResolver::~EspResolver()
{
    // Unmount in reverse creation order; ignore failures (busy mounts stay,
    // exactly like the scripts' best-effort trap).
    for (int i = probeMounts_.size() - 1; i >= 0; --i) {
        const QByteArray mp = QFile::encodeName(probeMounts_.at(i));
        ::umount(mp.constData());
        ::rmdir(mp.constData());
    }
}

QString EspResolver::ensureMounted(const QString &devicePath)
{
    const QString existing = mountPointOf(devicePath);
    if (!existing.isEmpty())
        return existing;

    QByteArray tmpl = QByteArrayLiteral("/tmp/espops.XXXXXX");
    if (!mkdtemp(tmpl.data()))
        return QString();
    const QString mp = QFile::decodeName(tmpl);
    // Read-only for the probe: this runs as root without a password and
    // mounts EVERY ESP-typed partition just to test whether rEFInd is on
    // it, including whatever removable media is attached. Only the ESP
    // actually chosen as the target gets remounted writable.
    if (runTool(systemTool("mount"),
                {QStringLiteral("-o"), QStringLiteral("ro,nosuid,nodev,noexec"),
                 devicePath, mp},
                nullptr)
        != 0) {
        ::rmdir(tmpl.constData());
        return QString();
    }
    probeMounts_ << mp;
    return mp;
}

void EspResolver::makeWritable(const QString &path)
{
    for (const QString &mp : probeMounts_) {
        if (path == mp || path.startsWith(mp + QLatin1Char('/'))) {
            runTool(systemTool("mount"),
                    {QStringLiteral("-o"), QStringLiteral("remount,rw"), mp},
                    nullptr);
            return;
        }
    }
}

bool EspResolver::resolve(RefindTarget *out)
{
    // 1. The ESP the firmware boots rEFInd from — but only when rEFInd is
    //    really there, so a stale NVRAM entry falls through instead of
    //    winning.
    const QString guid = refindEspGuidFromNvram();
    if (!guid.isEmpty()) {
        const QString dev = deviceForPartUuid(guid);
        if (!dev.isEmpty()) {
            const QString mp = ensureMounted(dev);
            if (!mp.isEmpty() && hasRefind(mp)) {
                out->refindDir = mp + QLatin1String("/EFI/refind");
                out->how = QStringLiteral("the ESP in the firmware's rEFInd "
                                          "boot entry (%1)")
                               .arg(dev);
                return true;
            }
        }
    }

    // 2. Any ESP that has rEFInd on it.
    const QStringList devices = espPartitions();
    for (const QString &dev : devices) {
        const QString mp = ensureMounted(dev);
        if (!mp.isEmpty() && hasRefind(mp)) {
            out->refindDir = mp + QLatin1String("/EFI/refind");
            out->how = QStringLiteral("an ESP containing rEFInd (%1)").arg(dev);
            return true;
        }
    }

    // 3. The running system's ESP, for a first install not yet booted.
    //    QDir::exists on "<mp>/EFI" resolves THROUGH the mountpoint, which
    //    triggers a systemd automount (a plain stat of the mountpoint does
    //    not — AT_NO_AUTOMOUNT since Linux 4.14).
    for (int i = 0; kSystemEspMountpoints[i]; ++i) {
        const QString mp = QString::fromLatin1(kSystemEspMountpoints[i]);
        if (QDir(mp + QLatin1String("/EFI")).exists()) {
            out->refindDir = mp + QLatin1String("/EFI/refind");
            out->how = QStringLiteral("the running system's ESP (%1)").arg(mp);
            return true;
        }
    }

    return false;
}

} // namespace EspOps
