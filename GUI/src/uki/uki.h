// Unified Kernel Image support for manual boot stanzas.
//
// rEFInd_GUI generates `scanfor manual` only: the four Boot Option slots map
// one-to-one onto menuentry stanzas, which is what lets the GUI control the
// boot menu's order, icons and default. UKIs don't fit a fixed `loader` line
// on their own -- systemd's kernel-install names each one
// <entry-token>-<kernel-version>.efi in /EFI/Linux and deletes old ones as
// kernels rotate -- so a UKI slot is a *family* (one entry token) instead of
// a file, and its stanza is re-pointed at the family's current images:
//
//   # rEFInd_GUI-uki token=<entry-token>
//   menuentry "Fedora Linux" {
//   	icon /EFI/refind/os_icon1.png
//   	volume "<partition GUID>"
//   	loader /EFI/Linux/<token>-<newest>.efi
//   	graphics on
//   	submenuentry "Boot <older version>" {
//   		loader /EFI/Linux/<token>-<older>.efi
//   	}
//   }
//
// The GUI writes that shape at Create Config; `rEFInd_GUI_helper uki-refresh`
// (run by the kernel-install plugin after every kernel add/remove, and after
// Install Config) rewrites only the marked stanzas' loader/submenuentry
// lines against what is on the ESP now. Everything else in refind.conf --
// order, icons, default_selection -- is left byte-for-byte alone.
//
// Pure QtCore and platform-neutral (the Linux-only helper runner lives in
// ukirefresh.cpp). Also linked by the parity-locked osdetect_* files, so the
// SteamDeck_rEFInd copy of those needs this module too.

#ifndef UKI_UKI_H
#define UKI_UKI_H

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace Uki {

// One UKI file in /EFI/Linux.
struct Image
{
    QString loaderPath;    // ESP-relative, e.g. "/EFI/Linux/<fileName>"
    QString fileName;      // as on disk, e.g. "ec4d...a7c5-7.2.10-200.fc44.x86_64.efi"
    QString token;         // kernel-install entry token ("ec4d...a7c5", "fedora", "arch-linux")
    QString kernelVersion; // from the .uname section, else parsed from the file name; may be empty
    QString osName;        // .osrel NAME (falls back to PRETTY_NAME, then ID); may be empty
    QString osId;          // .osrel ID; may be empty
};

// All images of one entry token on one ESP, newest first.
struct Family
{
    QString token;
    QString osName;
    QString osId;
    QStringList loaders;  // ESP-relative paths, "/EFI/Linux/<file>"
    QStringList versions; // parallel to loaders; an entry may be empty
};

// Entry token of UKIs named by kernel version alone ("6.19.10-300.fc44.x86_64.efi":
// images built with an empty machine-id, e.g. Fedora's UKI cloud image).
extern const char kNoToken[];

// The marker comment written on the line before a UKI family's menuentry.
// `osId` (the .osrel ID, may be empty) lets uki-refresh follow the family
// when its entry token changes -- a machine-id reset, or a tokenless image
// replaced by the first kernel-install'ed one.
QString markerLine(const QString &token, const QString &osId = QString());

// Reads the .osrel and .uname PE sections of a UKI. Bounded reads only (the
// file is ~100 MB of kernel+initrd); false when `path` is not a PE image.
// Missing sections leave the corresponding fields empty.
bool readImageInfo(const QString &path, QString *osName, QString *osId, QString *kernelVersion);

// Splits a /EFI/Linux file name into entry token and kernel version.
// `knownVersion` (from .uname) wins when the name ends in "-<knownVersion>";
// otherwise a 32-hex machine-id token, a stem that starts with a digit (a
// bare kernel version: token kNoToken), then "<token>-<digit>...", then the
// whole stem as the token with an empty version. A kernel-install
// boot-counting suffix ("+3", "+2-1") is ignored. False for names that are
// not a usable loader (not *.efi, empty stem, or characters that would need
// quoting in refind.conf).
bool splitFileName(const QString &fileName, const QString &knownVersion,
                   QString *token, QString *version);

// RPM-style version ordering: <0, 0 or >0. Numeric segments compare
// numerically, alphabetic ones lexically, numeric beats alphabetic, and a
// version with extra segments beats its prefix (7.2.10 > 7.2.9 > 7.2).
int compareVersions(const QString &a, const QString &b);

// Scans <espRoot>/EFI/Linux and groups its UKIs by entry token, families
// in token order, each family's images newest first. `readSections` false
// skips opening the files (names only) -- for unit tests and callers that
// can only list the directory.
QList<Family> scanFamilies(const QString &espRoot, bool readSections = true);

// Groups already-classified images (e.g. from a deep-scan listing that has
// paths but no file contents) the same way scanFamilies() does. Images need
// loaderPath, fileName and token; the rest may be empty.
QList<Family> groupImages(const QList<Image> &images);

// Classifies one ESP-relative /EFI/Linux/<file>.efi path from its name alone
// (no file access). False when it is not a UKI path splitFileName() accepts.
bool imageFromLoaderPath(const QString &loaderPath, Image *image);

// True for a token that is a 32-hex systemd machine-id.
bool isMachineIdToken(const QString &token);

// The loader + submenuentry lines of a family's stanza, tab-indented, each
// line newline-terminated. Older images become submenuentries titled
// "Boot <version>" (or "Boot <file name>" when the version is unknown).
QString renderLoaderLines(const QStringList &loaders, const QStringList &versions);

// Rewrites every marked UKI stanza in `conf` against `lookup(volume)`, the
// UKI families on the stanza's volume now. The family is the marker's
// token; when that token is gone, the one family with the marker's OS id
// (or, without an OS id, the only family on the volume) -- its marker is
// then updated to the new token. No match leaves the stanza exactly as it
// was, with a note. Only the marker, the stanza's depth-1 `loader` line and
// its submenuentry blocks are replaced; every other byte is preserved.
// *changed reports whether the text differs from the input.
using FamilyLookup = std::function<QList<Family>(const QString &volume)>;
QString refreshConf(const QString &conf, const FamilyLookup &lookup, bool *changed,
                    QStringList *notes);

// The entry tokens (in order) and their stanza volumes referenced by marked
// stanzas in `conf`. Lets callers find out which ESPs they need to look at.
struct MarkedStanza
{
    QString token;
    QString osId;     // from the marker; may be empty
    QString volume;   // unquoted value of the stanza's volume line; empty if none
    QString loader;   // its current depth-1 loader; empty if none
};
QList<MarkedStanza> markedStanzas(const QString &conf);

// --- Kernels that are no longer installed --------------------------------
//
// kernel-install is supposed to delete a UKI when its kernel is removed, but
// Fedora's kernel package removes with --entry-type=type1 and
// 90-uki-copy.install then skips UKIs entirely, leaving the image (and, for
// us, a submenu entry that boots a kernel without modules) behind. This
// machine's own images are therefore checked against the installed kernels.

// True when kernel `version` is installed under `modulesRoot`
// (/usr/lib/modules/<version> with its package-owned modules.builtin or
// modules.order). An empty version counts as installed (nothing to check).
bool kernelInstalled(const QString &version,
                     const QString &modulesRoot = QStringLiteral("/usr/lib/modules"));

// True when `modulesRoot` looks like a populated kernel tree at all; when it
// does not, nothing is pruned.
bool modulesTreeUsable(const QString &modulesRoot = QStringLiteral("/usr/lib/modules"));

// Whether a family is the running system's own kernels: its entry token is
// this machine's machine-id, or it is tokenless with this OS's os-release ID.
bool isThisMachinesFamily(const QString &token, const QString &osId, const QString &machineId,
                          const QString &runningOsId);

// Drops the images whose kernel `installed()` rejects, newest-first order
// kept, but never the last one. Returns the dropped loader paths.
QStringList dropUninstalledKernels(QStringList *loaders, QStringList *versions,
                                   const std::function<bool(const QString &version)> &installed);

// --- Boot priority after kernel updates ---------------------------------
//
// Fedora's uki-direct gives every newly installed UKI its own firmware boot
// entry: kernel-install sets BootNext to it, and after that boot succeeds
// kernel-bootcfg-boot-successful.service moves it to the front of
// BootOrder -- so each kernel update silently demotes rEFInd and the
// firmware boots the UKI directly. The kernel-install plugin hands the
// priority back to rEFInd right after uki-direct's own plugin has run.

// One Boot#### variable, as far as the decision below needs it.
struct BootVar
{
    quint16 num = 0;
    QString loaderPath;     // as stored, e.g. "\\EFI\\fedora\\shimx64.efi"
    QByteArray optionalData;
    bool active = false;
};

// The UKI a load option boots, "/EFI/Linux/<file>.efi", or empty: either its
// loader is an /EFI/Linux image, or (uki-direct) its loader is shim with the
// UKI path as UTF-16LE optional data.
QString ukiBootedBy(const BootVar &var);

struct PriorityPlan
{
    bool clearBootNext = false;
    QList<quint16> bootOrder; // empty: leave BootOrder alone
    quint16 refindNum = 0;
    bool haveRefind = false;
};

// What to change so rEFInd boots next and stays first. BootNext is cleared
// only when it points at a UKI entry; rEFInd's entry moves to the front of
// BootOrder only when a UKI entry sits ahead of it -- anything else the
// user deliberately put first is left alone, as is an rEFInd entry that is
// not in BootOrder at all.
PriorityPlan planRefindPriority(const QList<quint16> &bootOrder, int bootNext,
                                const QList<BootVar> &vars, bool (*isRefindLoader)(const QString &));

} // namespace Uki

#endif // UKI_UKI_H
