// Parity-locked between rEFInd_GUI and SteamDeck_rEFInd — see loadoption.h.

#include "userio.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace EspOps {

// --------------------------------------------------------------------------
// DirectUserFiles — for contexts already running as the user.
// --------------------------------------------------------------------------

bool DirectUserFiles::statRegular(const QString &path, qint64 *size)
{
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile())
        return false;
    if (size)
        *size = fi.size();
    return true;
}

bool DirectUserFiles::read(const QString &path, QIODevice *dest,
                           qint64 maxBytes, qint64 *written)
{
    QFile src(path);
    if (!QFileInfo(path).isFile() || !src.open(QIODevice::ReadOnly))
        return false;
    qint64 total = 0;
    while (!src.atEnd()) {
        const QByteArray chunk = src.read(1 << 16);
        if (chunk.isEmpty())
            break;
        total += chunk.size();
        if (total > maxBytes)
            return false;
        if (dest->write(chunk) != chunk.size())
            return false;
    }
    if (src.error() != QFileDevice::NoError)
        return false;
    if (written)
        *written = total;
    return true;
}

#ifdef Q_OS_UNIX

// --------------------------------------------------------------------------
// ForkUserFiles — root drops to the invoking user in a child process.
// --------------------------------------------------------------------------

namespace {

// Wall-clock deadline per operation. The scripts had none for the config
// files (runuser+cat just blocked), but a blocked child here would hang the
// GUI's synchronous output capture; regular-file reads never take this
// long, so a hit means something is genuinely wrong.
const int kDeadlineMs = 60 * 1000;

// Child-side: drop to the user. Order matters — groups first, uid last;
// any failure must abort the child (continuing would read as root).
bool dropToUser(const SudoUser &u)
{
    const QByteArray name = u.name.toLocal8Bit();
    if (initgroups(name.constData(), gid_t(u.gid)) != 0)
        return false;
    if (setgid(gid_t(u.gid)) != 0)
        return false;
    if (setuid(uid_t(u.uid)) != 0)
        return false;
    // Paranoia: the drop must not be reversible.
    return setuid(0) != 0;
}

// Child-side: open `path` as the (already dropped-to) user and require a
// regular file. O_NONBLOCK keeps a FIFO from blocking the open; fstat then
// rejects it — the same effective filter as the scripts' `test -f`, minus
// the TOCTOU window.
int openRegularAsUser(const QByteArray &path, struct stat *st)
{
    const int fd = open(path.constData(),
                        O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
    if (fd < 0)
        return -1;
    if (fstat(fd, st) != 0 || !S_ISREG(st->st_mode)) {
        close(fd);
        return -1;
    }
    // Regular files don't block; drop O_NONBLOCK for the read loop.
    fcntl(fd, F_SETFL, 0);
    return fd;
}

// Parent-side: drain the pipe into dest with a cap and a deadline.
// Returns -1 on failure, else the byte count.
qint64 drainPipe(int fd, QIODevice *dest, qint64 maxBytes)
{
    qint64 total = 0;
    char buf[1 << 16];
    for (;;) {
        struct pollfd p = {fd, POLLIN, 0};
        const int pr = poll(&p, 1, kDeadlineMs);
        if (pr <= 0)
            return -1; // timeout or poll failure
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return total;
        total += n;
        if (total > maxBytes)
            return -1;
        if (dest && dest->write(buf, n) != n)
            return -1;
    }
}

// Reap the child; true only for a clean exit 0.
bool reapOk(pid_t pid)
{
    int status = 0;
    for (;;) {
        if (waitpid(pid, &status, 0) < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
}

} // namespace

SudoUser sudoUserFromEnvironment()
{
    SudoUser u;
    const QByteArray name = qgetenv("SUDO_USER");
    if (name.isEmpty() || name == "root")
        return u;
    errno = 0;
    const struct passwd *pw = getpwnam(name.constData());
    if (!pw || pw->pw_uid == 0)
        return u;
    const QString home = QFile::decodeName(pw->pw_dir);
    if (home.isEmpty() || !QFileInfo(home).isDir())
        return u;
    u.valid = true;
    u.name = QString::fromLocal8Bit(name);
    u.home = home;
    u.uid = unsigned(pw->pw_uid);
    u.gid = unsigned(pw->pw_gid);
    return u;
}

bool ForkUserFiles::statRegular(const QString &path, qint64 *size)
{
    // The child reports the size over the pipe as decimal text; anything
    // other than a clean exit 0 with a parseable number is "no".
    int fds[2];
    if (pipe(fds) != 0)
        return false;
    const pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    if (pid == 0) {
        close(fds[0]);
        if (!dropToUser(user_))
            _exit(1);
        struct stat st;
        const int fd = openRegularAsUser(QFile::encodeName(path), &st);
        if (fd < 0)
            _exit(1);
        close(fd);
        char buf[32];
        const int len = snprintf(buf, sizeof(buf), "%lld",
                                 static_cast<long long>(st.st_size));
        if (len <= 0 || write(fds[1], buf, size_t(len)) != len)
            _exit(1);
        _exit(0);
    }
    close(fds[1]);
    QBuffer text;
    text.open(QIODevice::WriteOnly);
    const qint64 got = drainPipe(fds[0], &text, 32);
    close(fds[0]);
    if (got < 0) {
        kill(pid, SIGKILL);
        reapOk(pid);
        return false;
    }
    if (!reapOk(pid))
        return false;
    bool ok = false;
    const qint64 sz = text.buffer().toLongLong(&ok);
    if (!ok || sz < 0)
        return false;
    if (size)
        *size = sz;
    return true;
}

bool ForkUserFiles::read(const QString &path, QIODevice *dest,
                         qint64 maxBytes, qint64 *written)
{
    int fds[2];
    if (pipe(fds) != 0)
        return false;
    const pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    if (pid == 0) {
        close(fds[0]);
        if (!dropToUser(user_))
            _exit(1);
        struct stat st;
        const int fd = openRegularAsUser(QFile::encodeName(path), &st);
        if (fd < 0)
            _exit(1);
        char buf[1 << 16];
        for (;;) {
            const ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                _exit(1);
            }
            if (n == 0)
                _exit(0);
            ssize_t off = 0;
            while (off < n) {
                const ssize_t w = write(fds[1], buf + off, size_t(n - off));
                if (w < 0) {
                    if (errno == EINTR)
                        continue;
                    _exit(1);
                }
                off += w;
            }
        }
    }
    close(fds[1]);
    const qint64 total = drainPipe(fds[0], dest, maxBytes);
    close(fds[0]);
    if (total < 0) {
        kill(pid, SIGKILL);
        reapOk(pid);
        return false;
    }
    if (!reapOk(pid))
        return false;
    if (written)
        *written = total;
    return true;
}

#endif // Q_OS_UNIX

} // namespace EspOps
