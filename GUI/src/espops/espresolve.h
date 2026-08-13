// espops — see loadoption.h for the parity rules (byte-identical between
// rEFInd_GUI and SteamDeck_rEFInd; repo specifics live in espconstants.h).
//
// Resolution of the ESP that actually boots rEFInd, ported from the root
// helper scripts' resolve_refind_dir() / Get-RefindNvramEsp
// (NATIVE_HELPER_DESIGN.md §2.4): the NVRAM rEFInd entry's partition first
// (verified to still contain EFI/refind/refind*.efi, so a stale entry can't
// shadow the live install), then any ESP containing rEFInd, then the
// running system's ESP. Linux implementation only (espresolve_linux.cpp) —
// it mounts unmounted candidates and therefore needs root; the Windows
// counterpart arrives with the in-process GUI port.

#ifndef ESPOPS_ESPRESOLVE_H
#define ESPOPS_ESPRESOLVE_H

#include <QString>
#include <QStringList>

namespace EspOps {

struct RefindTarget
{
    QString refindDir; // "<mountpoint>/EFI/refind"
    QString how;       // provenance, e.g. "the ESP in the firmware's rEFInd boot entry (/dev/sda1)"
};

// Owns its read-only probe mounts: every ESP it has to mount to inspect is
// mounted ro,nosuid,nodev,noexec on a private temp directory and unmounted
// by the destructor — so keep the resolver alive until all writes to the
// resolved target are finished (and synced).
class EspResolver
{
public:
    EspResolver() = default;
    ~EspResolver();
    EspResolver(const EspResolver &) = delete;
    EspResolver &operator=(const EspResolver &) = delete;

    // Fills *out and returns true when an ESP was found. Requires root.
    bool resolve(RefindTarget *out);

    // Remounts the filesystem under `path` read-write IF it is one of this
    // resolver's own probe mounts; filesystems the system already had
    // mounted are left exactly as they were (same rule as the scripts'
    // esp_make_writable).
    void makeWritable(const QString &path);

private:
    QString ensureMounted(const QString &devicePath);
    QStringList probeMounts_;
};

// The partition GUID (lowercase) of the ESP the firmware's rEFInd entry
// points at, or an empty string. BootOrder-ordered entries are considered
// first, then the rest; a \EFI\refind\refind*.efi loader path wins over the
// exact "rEFInd" label tier. Reads /sys/firmware/efi/efivars directly.
QString refindEspGuidFromNvram();

} // namespace EspOps

#endif // ESPOPS_ESPRESOLVE_H
