// espops — see loadoption.h for the parity rules (byte-identical between
// rEFInd_GUI and SteamDeck_rEFInd; repo specifics live in espconstants.h).
//
// User-context file access for the root helper. The invariant ported from
// the scripts (NATIVE_HELPER_DESIGN.md §2.5): root NEVER opens anything
// under the invoking user's home — a symlink/hardlink under ~/.local could
// otherwise exfiltrate root-readable files onto the (world-readable when
// temp-mounted) ESP. The scripts used `runuser -u "$SUDO_USER" -- cat`;
// ForkUserFiles is the fd-handoff equivalent: fork, drop to the user
// (initgroups → setgid → setuid), open and stream in the child. The
// interface is abstract so unit tests (and the Windows in-process path,
// where the GUI already runs as the user) can substitute a direct reader.

#ifndef ESPOPS_USERIO_H
#define ESPOPS_USERIO_H

#include <QIODevice>
#include <QString>

namespace EspOps {

class UserFiles
{
public:
    virtual ~UserFiles() = default;

    // True when `path` is an existing REGULAR file readable in the user's
    // context; *size receives its byte count (pass nullptr if unwanted).
    virtual bool statRegular(const QString &path, qint64 *size) = 0;

    // Stream `path` into `dest` (already open for writing) in the user's
    // context. Fails when the file is not regular, exceeds maxBytes, or
    // cannot be read/written completely. *written (optional) receives the
    // byte count on success.
    virtual bool read(const QString &path, QIODevice *dest, qint64 maxBytes,
                      qint64 *written) = 0;
};

// Direct implementation for contexts that already run AS the user (unit
// tests, the elevated Windows GUI reading its own %LOCALAPPDATA%).
class DirectUserFiles : public UserFiles
{
public:
    bool statRegular(const QString &path, qint64 *size) override;
    bool read(const QString &path, QIODevice *dest, qint64 maxBytes,
              qint64 *written) override;
};

#ifdef Q_OS_UNIX

struct SudoUser
{
    bool valid = false;
    QString name;
    QString home;
    unsigned uid = 0;
    unsigned gid = 0;
};

// Resolve SUDO_USER via getpwnam. Invalid when unset, unknown, root, or
// the home directory is missing — the same rejections as the scripts'
// prologue (their exit code 2 belongs to the caller).
SudoUser sudoUserFromEnvironment();

// fork/setuid implementation; requires root (the privilege drop must
// succeed, and a failed drop aborts the child rather than reading as root).
class ForkUserFiles : public UserFiles
{
public:
    explicit ForkUserFiles(const SudoUser &user) : user_(user) {}
    bool statRegular(const QString &path, qint64 *size) override;
    bool read(const QString &path, QIODevice *dest, qint64 maxBytes,
              qint64 *written) override;

private:
    SudoUser user_;
};

#endif // Q_OS_UNIX

} // namespace EspOps

#endif // ESPOPS_USERIO_H
