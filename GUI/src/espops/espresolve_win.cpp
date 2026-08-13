// Parity-locked between rEFInd_GUI and SteamDeck_rEFInd — see loadoption.h.
//
// Windows implementation of the rEFInd-ESP resolution: the C++ port of
// windows/uefi_refind.ps1's Get-RefindNvramEsp (NATIVE_HELPER_DESIGN.md
// §2.4). Boot#### variables are read directly via
// GetFirmwareEnvironmentVariableW (locale-independent; needs
// SeSystemEnvironmentPrivilege, which the elevated process can enable),
// walked BootOrder-first and then the conventional Boot0000–Boot00FF range
// (so an entry dropped from BootOrder is still found), matching by GPT
// hard-drive node + \EFI\refind\refind*.efi loader path through the shared
// byte parser. Volume enumeration is native (FindFirstVolumeW +
// IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS + IOCTL_DISK_GET_DRIVE_LAYOUT_EX)
// instead of PowerShell; letterless volumes are mounted on a private temp
// DIRECTORY via SetVolumeMountPointW — the same no-drive-letter trick
// osdetect_win.cpp uses through mountvol — and unmounted by the destructor.
//
// NOTE: authored against the validated PowerShell behavior; compile and
// exercise on a real Windows machine (MSYS2 UCRT64) before shipping.

#include "espconstants.h"
#include "espresolve.h"
#include "loadoption.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUuid>

#include <qt_windows.h>
#include <winioctl.h>

namespace EspOps {

namespace {

const wchar_t kGlobalGuidW[] = L"{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}";
const char kEspTypeGuid[] = "c12a7328-f81f-11d2-ba4b-00a0c93ec93b";

bool enableSystemEnvironmentPrivilege()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return false;
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = LookupPrivilegeValueW(nullptr, SE_SYSTEM_ENVIRONMENT_NAME,
                                    &tp.Privileges[0].Luid)
        && AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr)
        && GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    return ok;
}

} // namespace

// Shared NVRAM plumbing, also used by wintasks.cpp (bootnext).
QByteArray winReadEfiVar(const QString &name)
{
    static const bool privileged = enableSystemEnvironmentPrivilege();
    if (!privileged)
        return QByteArray();
    QByteArray buf(8192, 0);
    const DWORD got = GetFirmwareEnvironmentVariableW(
        reinterpret_cast<const wchar_t *>(name.utf16()), kGlobalGuidW,
        buf.data(), DWORD(buf.size()));
    if (got == 0)
        return QByteArray();
    buf.truncate(int(got));
    return buf;
}

// BootNext-style writes only: espops never rewrites or deletes a Boot####
// entry (the installers own that, with the WINDOWS-blob rule).
bool winWriteEfiVar(const QString &name, const QByteArray &value)
{
    static const bool privileged = enableSystemEnvironmentPrivilege();
    if (!privileged)
        return false;
    return SetFirmwareEnvironmentVariableW(
               reinterpret_cast<const wchar_t *>(name.utf16()), kGlobalGuidW,
               const_cast<char *>(value.constData()), DWORD(value.size()))
        != 0;
}

namespace {

QString guidToString(const GUID &g)
{
    return QUuid(g).toString(QUuid::WithoutBraces).toLower();
}

struct VolumeInfo
{
    QString volumeName;       // "\\\\?\\Volume{guid}\\" (trailing backslash)
    QString gptPartitionGuid; // lowercase, empty for non-GPT
    QString gptPartitionType; // lowercase, empty for non-GPT
};

// Map every volume to its GPT partition GUID/type by resolving the volume's
// disk extent against the disk's partition table.
QList<VolumeInfo> enumerateVolumes()
{
    QList<VolumeInfo> out;
    wchar_t name[MAX_PATH];
    HANDLE find = FindFirstVolumeW(name, MAX_PATH);
    if (find == INVALID_HANDLE_VALUE)
        return out;
    do {
        VolumeInfo vi;
        vi.volumeName = QString::fromWCharArray(name);
        // Open without the trailing backslash to address the volume object.
        QString devPath = vi.volumeName;
        if (devPath.endsWith(QLatin1Char('\\')))
            devPath.chop(1);
        HANDLE vol = CreateFileW(
            reinterpret_cast<const wchar_t *>(devPath.utf16()), 0,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
            nullptr);
        if (vol == INVALID_HANDLE_VALUE) {
            out.append(vi);
            continue;
        }
        VOLUME_DISK_EXTENTS extents;
        DWORD bytes = 0;
        if (DeviceIoControl(vol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                            nullptr, 0, &extents, sizeof(extents), &bytes,
                            nullptr)
            && extents.NumberOfDiskExtents == 1) {
            const DWORD disk = extents.Extents[0].DiskNumber;
            const LONGLONG start = extents.Extents[0].StartingOffset.QuadPart;
            const QString diskPath =
                QStringLiteral("\\\\.\\PhysicalDrive%1").arg(disk);
            HANDLE dh = CreateFileW(
                reinterpret_cast<const wchar_t *>(diskPath.utf16()), 0,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                nullptr);
            if (dh != INVALID_HANDLE_VALUE) {
                QByteArray layoutBuf(sizeof(DRIVE_LAYOUT_INFORMATION_EX)
                                         + 255 * sizeof(PARTITION_INFORMATION_EX),
                                     0);
                if (DeviceIoControl(dh, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, nullptr,
                                    0, layoutBuf.data(),
                                    DWORD(layoutBuf.size()), &bytes, nullptr)) {
                    const auto *layout =
                        reinterpret_cast<const DRIVE_LAYOUT_INFORMATION_EX *>(
                            layoutBuf.constData());
                    for (DWORD i = 0; i < layout->PartitionCount; ++i) {
                        const PARTITION_INFORMATION_EX &p =
                            layout->PartitionEntry[i];
                        if (p.PartitionStyle == PARTITION_STYLE_GPT
                            && p.StartingOffset.QuadPart == start) {
                            vi.gptPartitionGuid =
                                guidToString(p.Gpt.PartitionId);
                            vi.gptPartitionType =
                                guidToString(p.Gpt.PartitionType);
                            break;
                        }
                    }
                }
                CloseHandle(dh);
            }
        }
        CloseHandle(vol);
        out.append(vi);
    } while (FindNextVolumeW(find, name, MAX_PATH));
    FindVolumeClose(find);
    return out;
}

// First existing mount point (drive letter or directory) of a volume, or
// empty when it has none.
QString mountPointForVolume(const QString &volumeName)
{
    wchar_t paths[4096];
    DWORD len = 0;
    if (!GetVolumePathNamesForVolumeNameW(
            reinterpret_cast<const wchar_t *>(volumeName.utf16()), paths,
            DWORD(sizeof(paths) / sizeof(paths[0])), &len))
        return QString();
    const QString first = QString::fromWCharArray(paths);
    if (first.isEmpty())
        return QString();
    QString mp = QDir::fromNativeSeparators(first);
    while (mp.endsWith(QLatin1Char('/')))
        mp.chop(1);
    return mp.isEmpty() ? QStringLiteral("/") : mp;
}

bool hasRefind(const QString &root)
{
    const QDir d(root + QLatin1String("/EFI/refind"));
    return !d.entryList({QStringLiteral("refind*.efi")}, QDir::Files).isEmpty();
}

} // namespace

QString refindEspGuidFromNvram()
{
    // BootOrder-listed entries first, then the conventional Boot0000–00FF
    // sweep (an entry dropped from BootOrder is still found) — the same
    // order as uefi_refind.ps1.
    QList<quint16> ids = parseBootOrder(winReadEfiVar(QStringLiteral("BootOrder")));
    for (quint16 i = 0; i <= 0xFF; ++i) {
        if (!ids.contains(i))
            ids << i;
    }
    for (int tier = 0; tier < 2; ++tier) {
        for (quint16 id : ids) {
            const QByteArray raw = winReadEfiVar(formatBootNum(id));
            if (raw.isEmpty())
                continue;
            const LoadOption o = parseLoadOption(raw);
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
    // probeMounts_ holds "volumeName|mountDir" pairs; unmount and remove the
    // private directories in reverse creation order.
    for (int i = probeMounts_.size() - 1; i >= 0; --i) {
        const QStringList parts = probeMounts_.at(i).split(QLatin1Char('|'));
        if (parts.size() != 2)
            continue;
        QString dir = QDir::toNativeSeparators(parts.at(1));
        if (!dir.endsWith(QLatin1Char('\\')))
            dir += QLatin1Char('\\');
        DeleteVolumeMountPointW(reinterpret_cast<const wchar_t *>(dir.utf16()));
        QDir().rmdir(parts.at(1));
    }
}

QString EspResolver::ensureMounted(const QString &volumeName)
{
    const QString existing = mountPointForVolume(volumeName);
    if (!existing.isEmpty())
        return existing;
    // Mount on a private temp DIRECTORY (no drive letter consumed) — the
    // native equivalent of `mountvol <dir> \\?\Volume{guid}\`.
    QTemporaryDir tmp(QDir::tempPath() + QLatin1String("/espops-XXXXXX"));
    if (!tmp.isValid())
        return QString();
    tmp.setAutoRemove(false);
    QString dir = QDir::toNativeSeparators(tmp.path());
    if (!dir.endsWith(QLatin1Char('\\')))
        dir += QLatin1Char('\\');
    if (!SetVolumeMountPointW(
            reinterpret_cast<const wchar_t *>(dir.utf16()),
            reinterpret_cast<const wchar_t *>(volumeName.utf16()))) {
        QDir().rmdir(tmp.path());
        return QString();
    }
    probeMounts_ << volumeName + QLatin1Char('|') + tmp.path();
    return tmp.path();
}

void EspResolver::makeWritable(const QString &path)
{
    // Windows mounts are writable for the elevated process; nothing to do.
    Q_UNUSED(path);
}

bool EspResolver::resolve(RefindTarget *out)
{
    const QList<VolumeInfo> volumes = enumerateVolumes();

    // 1. The ESP the firmware boots rEFInd from — but only when rEFInd is
    //    really there, so a stale NVRAM entry (e.g. an old EFI\refind on the
    //    Windows ESP shadowing the live Linux-side install) falls through.
    const QString guid = refindEspGuidFromNvram();
    if (!guid.isEmpty()) {
        for (const VolumeInfo &vi : volumes) {
            if (vi.gptPartitionGuid != guid)
                continue;
            const QString mp = ensureMounted(vi.volumeName);
            if (!mp.isEmpty() && hasRefind(mp)) {
                out->refindDir = mp + QLatin1String("/EFI/refind");
                out->how = QStringLiteral("the ESP in the firmware's rEFInd "
                                          "boot entry (%1)")
                               .arg(vi.volumeName);
                return true;
            }
        }
    }

    // 2. Any ESP-typed volume that has rEFInd on it.
    for (const VolumeInfo &vi : volumes) {
        if (vi.gptPartitionType != QLatin1String(kEspTypeGuid))
            continue;
        const QString mp = ensureMounted(vi.volumeName);
        if (!mp.isEmpty() && hasRefind(mp)) {
            out->refindDir = mp + QLatin1String("/EFI/refind");
            out->how = QStringLiteral("an ESP containing rEFInd (%1)")
                           .arg(vi.volumeName);
            return true;
        }
    }

    // 3. The first reachable ESP-typed volume, for a first install not yet
    //    booted (the ps1's system-ESP filesystem-scan fallback).
    for (const VolumeInfo &vi : volumes) {
        if (vi.gptPartitionType != QLatin1String(kEspTypeGuid))
            continue;
        const QString mp = ensureMounted(vi.volumeName);
        if (!mp.isEmpty() && QDir(mp + QLatin1String("/EFI")).exists()) {
            out->refindDir = mp + QLatin1String("/EFI/refind");
            out->how = QStringLiteral("the system's ESP (%1)").arg(vi.volumeName);
            return true;
        }
    }

    return false;
}

} // namespace EspOps
