#ifndef OSDETECT_H
#define OSDETECT_H

#include <QList>
#include <QMetaType>
#include <QSize>
#include <QString>
#include <QStringList>

// One bootable OS discovered on (or declared for) an EFI System Partition.
struct BootEntry {
    QString displayName;                  // name shown in the GUI combo boxes
    QString menuName;                     // menuentry title written to refind.conf
    QString loaderPath;                   // loader path on the volume, e.g. /EFI/fedora/shimx64.efi
    QString volume;                       // optional volume (label or partition GUID) for cross-volume entries
    bool supportsFirmwareBootnum = false; // SteamOS: can boot via firmware_bootnum instead of a loader
};

Q_DECLARE_METATYPE(BootEntry)

// Detection is split across three translation units:
//   osdetect_common.cpp - platform-neutral ESP scan and entry assembly
//   osdetect_linux.cpp  - lsblk/sysfs/os-release backend
//   osdetect_win.cpp    - PowerShell CIM/mountvol backend
class OSDetector
{
public:
    // Scans block devices and the system ESP for installed OSes/bootloaders.
    QList<BootEntry> detect();

    static bool isLegionGo();
    static bool isLegionGo2();
    // ROG Xbox Ally (RC73YA) or ROG Xbox Ally X (RC73XA).
    static bool isXboxAlly();
    // Native (preferred) resolution of the built-in panel from EDID/DRM,
    // preferring an internal display (eDP/LVDS/DSI) over external outputs so
    // a docked handheld still reports its own screen. Invalid when unknown.
    static QSize nativePanelResolution();
    // Partition GUID of the first ESP on removable media (SD card or USB), for
    // the static "Windows (SD)"/"Windows (USB)" combo fallbacks.
    QString removableEspPartUuid(bool sdCard);
    static QString runCommand(const QString &program, const QStringList &args, bool *ok = nullptr);

private:
    struct Partition {
        QString path;        // /dev/... on Linux, "disk<N>p<M>" on Windows (informational)
        QString partType;    // GPT type GUID (lowercase, no braces) or "0xef" for MBR ESPs
        QString label;
        QString partUuid;    // PARTUUID / partition GUID (lowercase, no braces)
        QString mountPoint;  // mount point / drive letter root; non-empty also for the in-use system ESP
        QString transport;   // "usb", "sd", "mmc", "nvme", ... (lowercase)
        QString volumePath;  // Windows only: "\\?\Volume{guid}\" name for mountvol; empty on Linux
        bool removable = false;
    };

    // Shared implementation (osdetect_common.cpp)
    // runningDistroName, when non-empty, renames a generic systemd-boot loader
    // on this ESP to the running distro (so CachyOS shows as "CachyOS").
    QList<BootEntry> scanEspRoot(const QString &rootPath, const QString &runningDistroName);
    QList<BootEntry> assembleEntries(const QList<Partition> &partitions, QList<BootEntry> mounted);
    static bool isEsp(const Partition &p);
    // Volume token for cross-ESP entries: partition GUID (preferred) or label.
    static QString espVolumeId(const Partition &p);
    // True for the ESP that belongs to the running OS (its EFI/ mount).
    static bool isRunningSystemEsp(const Partition &p);
    static QString displayNameForVendorDir(const QString &dirName);

    // Platform backends (osdetect_linux.cpp / osdetect_win.cpp)
    QList<Partition> listPartitions();
    // Partition snapshot reused between detect() and removableEspPartUuid()
    // (enumeration shells out and is slow, especially on Windows).
    QList<Partition> cachedPartitions;
    bool cacheValid = false;
    // Path under which ESP `p`'s EFI/ tree is reachable ("" if unreachable),
    // mounting it if needed. Sets release=true when releaseEspRoot() must be
    // called after scanning.
    QString espScanRoot(const Partition &p, bool &release);
    void releaseEspRoot(const QString &root);
    static QStringList runningOsIds();  // os-release ID/ID_LIKE; empty on Windows
    static QString runningOsName();     // os-release NAME; empty on Windows
};

#endif // OSDETECT_H
