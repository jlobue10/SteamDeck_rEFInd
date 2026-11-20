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
    // Unified Kernel Images in /EFI/Linux: non-empty for one kernel-install
    // entry-token family. loaderPath is then the newest image and
    // ukiLoaders/ukiVersions list every image newest first; the slot key
    // and the generated stanza follow the family rather than one versioned
    // file, which kernel-install deletes as kernels rotate (see uki/uki.h).
    QString ukiToken;
    QStringList ukiLoaders;
    QStringList ukiVersions;
    QString ukiOsId;  // .osrel ID when the images were readable; may be empty
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
    // EFI binary-name suffix of the running CPU: "x64", "aa64" (ARM64) or
    // "ia32". Vendor loaders, the removable-media fallback and shim's
    // BOOT<ARCH>.CSV all carry it (shimx64.efi vs shimaa64.efi); a firmware
    // only runs its own architecture's images, so detection looks for the
    // native names alone.
    static QString efiArchSuffix();
    // shim, GRUB, systemd-boot file names for this architecture, in the
    // order scanEspRoot() prefers them.
    static QStringList vendorLoaderCandidates();

private:
    // Tests set this to scan fixtures of another architecture; empty = native.
    static QString efiArchSuffixOverride;
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
    // True when an ESP is mounted but its contents cannot be read by this
    // (unprivileged) process, so a scan returning nothing means "couldn't
    // look" rather than "nothing there". systemd-gpt-auto-generator mounts
    // ESPs dmask=0077 root:root, which is what the Steam Deck's /esp gets.
    static bool espRootUnreadable(const QString &rootPath);
    // Maps an ESP-relative loader path (/EFI/steamos/steamcl.efi) onto a named
    // entry the way scanEspRoot() would, so every fallback path below names
    // things identically. False for paths that aren't a bootable OS (rEFInd
    // itself, the /EFI/BOOT removable fallback loader).
    static bool classifyLoaderPath(const QString &loaderPath, BootEntry &entry);
    // Entries recovered from the firmware's boot variables, for ESPs whose
    // EFI/ tree is unreadable. Empty on Windows (see osdetect_win.cpp).
    QList<BootEntry> firmwareEntriesForEsp(const Partition &p);
    // The `efibootmgr -v` parsing behind firmwareEntriesForEsp(), for the
    // entries whose HD() node names partition GUID `partUuid` (lowercase).
    static QList<BootEntry> parseEfibootmgrEntries(const QString &out, const QString &partUuid);
    // Entries from the cache written by scripts/scan_esp.sh, an opt-in
    // elevated scan that sees loaders with no firmware boot entry. Empty on
    // Windows and when the user has never run it.
    QList<BootEntry> deepScanEntriesForEsp(const Partition &p);
    // Volume token for cross-ESP entries: partition GUID (preferred) or label.
    static QString espVolumeId(const Partition &p);
    // True for the ESP that belongs to the running OS (its EFI/ mount).
    static bool isRunningSystemEsp(const Partition &p);
    static QString displayNameForVendorDir(const QString &dirName);
    // Names a UKI family: its .osrel NAME, else the running distro when the
    // token is this machine's machine-id, else a known distro token, else
    // "Linux".
    static QString displayNameForUkiToken(const QString &token, const QString &osName,
                                          const QString &runningDistroName);
    // Folds per-file UKI entries (deep-scan/firmware recovery) into one
    // entry per entry token, newest image as the loader.
    static QList<BootEntry> mergeUkiEntries(const QList<BootEntry> &entries);
    // On the running system's ESP, names the running OS's own names-only
    // UKI families after it and records its os-release ID (osdetect_common).
    static void adoptRunningOsForUkis(QList<BootEntry> &entries);

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
