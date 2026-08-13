// espops — shared privileged-operation logic for the rEFInd GUI and its
// root/scheduled-task helper binary (see NATIVE_HELPER_DESIGN.md).
//
// This file is part of the parity-locked espops/ set: keep it byte-identical
// between the rEFInd_GUI and SteamDeck_rEFInd repos. Repo-specific values
// live in espconstants.h, which is NOT parity-locked.
//
// loadoption.{h,cpp} is the single EFI_LOAD_OPTION / device-path parser used
// on both platforms. Platform shims feed it raw Boot#### variable bytes:
// Linux reads /sys/firmware/efi/efivars (strip the 4-byte attributes prefix
// with fromEfivarsPayload), Windows reads GetFirmwareEnvironmentVariableW
// output as-is. It deliberately parses bytes, not efibootmgr text — the
// "efibootmgr >= 18 appends a tab + device path after the label" class of
// regex bugs cannot exist here.

#ifndef ESPOPS_LOADOPTION_H
#define ESPOPS_LOADOPTION_H

#include <QByteArray>
#include <QList>
#include <QString>

namespace EspOps {

// One parsed EFI_LOAD_OPTION (UEFI spec 2.x, "Boot####" variables).
struct LoadOption
{
    bool valid = false;       // false: buffer was not a plausible load option
    quint32 attributes = 0;   // LOAD_OPTION_* flags
    QString description;      // the label shown by firmware ("rEFInd", ...)

    // From the first GPT hard-drive media node in the device path, if any.
    // Lowercase canonical text form ("c12a7328-..."), empty when the entry
    // has no GPT hard-drive node (network boot, MBR disk, ...).
    QString gptPartitionGuid;
    quint32 partitionNumber = 0; // 1-based, 0 when no hard-drive node

    // From the first file-path media node, backslash-separated as stored in
    // NVRAM (e.g. "\\EFI\\refind\\refind_x64.efi"); empty when absent.
    QString loaderPath;

    // Bytes after the device-path list. Windows' {bootmgr} entry carries a
    // "WINDOWS" blob here and must never be rewritten or deleted.
    QByteArray optionalData;

    bool active() const { return valid && (attributes & 0x1); }
    bool hasWindowsOptionalData() const
    {
        return optionalData.contains(QByteArrayLiteral("WINDOWS"));
    }
};

// Parse one Boot#### variable's payload. Never throws; malformed or
// truncated input yields .valid == false. Trailing garbage after a
// well-formed option is tolerated (it becomes optionalData).
LoadOption parseLoadOption(const QByteArray &raw);

// Parse a BootOrder variable's payload (little-endian UINT16 array). A
// trailing odd byte is ignored.
QList<quint16> parseBootOrder(const QByteArray &raw);

// "Boot0001"-style variable name for a boot number.
QString formatBootNum(quint16 num);

// Strip the 4-byte attributes prefix efivarfs prepends to every variable
// read from /sys/firmware/efi/efivars. Returns an empty array when the
// input is too short to carry the prefix.
QByteArray fromEfivarsPayload(const QByteArray &raw);

// True when a loader path is rEFInd's own binary. Mirrors the scripts'
// (case-insensitive, contains) match \EFI\refind\refind[^\]*.efi — the
// [^\] guard keeps drivers under \EFI\refind\drivers_x64\ from matching.
// Forward slashes are accepted and treated as backslashes.
bool loaderLooksLikeRefind(const QString &loaderPath);

} // namespace EspOps

#endif // ESPOPS_LOADOPTION_H
