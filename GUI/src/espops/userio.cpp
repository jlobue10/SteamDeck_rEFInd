// Parity-locked between rEFInd_GUI and SteamDeck_rEFInd — see loadoption.h.

#include "userio.h"

#include <QBuffer>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <functional>
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

namespace {

bool nameIsClean(const QByteArray &name)
{
    if (name.isEmpty() || name == "." || name == "..")
        return false;
    for (const char c : name) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f)
            return false;
    }
    return true;
}

} // namespace

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

QStringList DirectUserFiles::listSubdirs(const QString &path)
{
    QStringList out;
    const QDir d(path);
    const QStringList names =
        d.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QString &n : names) {
        if (nameIsClean(n.toLocal8Bit()))
            out << n;
    }
    return out;
}

bool DirectUserFiles::listFilesRecursive(const QString &dir,
                                         QList<UserFileEntry> *out)
{
    if (!QFileInfo(dir).isDir())
        return false;
    QDirIterator it(dir, QDir::Files | QDir::NoSymLinks | QDir::Hidden,
                    QDirIterator::Subdirectories);
    const QString prefix = QDir(dir).absolutePath() + QLatin1Char('/');
    while (it.hasNext()) {
        const QString p = it.next();
        const QFileInfo fi = it.fileInfo();
        if (!fi.isFile() || fi.isSymLink())
            continue;
        QString rel = QFileInfo(p).absoluteFilePath();
        if (!rel.startsWith(prefix))
            continue;
        rel = rel.mid(prefix.size());
        if (!nameIsClean(rel.toLocal8Bit().replace('/', '_')))
            continue;
        out->append({rel, fi.size()});
    }
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

// Child-side write that survives EINTR and short writes.
bool writeAll(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        const ssize_t w = write(fd, buf + off, len - off);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        off += size_t(w);
    }
    return true;
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

// Child-side recursive walk: emit "size\trelpath\n" for every regular file
// under base. Symlinks and unclean names are skipped; depth is capped.
bool walkAsUser(int outFd, const QByteArray &base, const QByteArray &rel,
                int depth)
{
    if (depth > 32)
        return true; // silently stop descending; a real tree is ~3 deep
    const QByteArray full = rel.isEmpty() ? base : base + '/' + rel;
    DIR *d = opendir(full.constData());
    if (!d)
        return rel.isEmpty() ? false : true; // unreadable subdir: skip
    struct dirent *e;
    bool ok = true;
    while (ok && (e = readdir(d)) != nullptr) {
        const QByteArray name(e->d_name);
        if (!nameIsClean(name))
            continue;
        const QByteArray childRel = rel.isEmpty() ? name : rel + '/' + name;
        struct stat st;
        if (lstat((base + '/' + childRel).constData(), &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            ok = walkAsUser(outFd, base, childRel, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            char head[32];
            const int len = snprintf(head, sizeof(head), "%lld\t",
                                     static_cast<long long>(st.st_size));
            ok = len > 0 && writeAll(outFd, head, size_t(len))
                && writeAll(outFd, childRel.constData(), size_t(childRel.size()))
                && writeAll(outFd, "\n", 1);
        }
        // symlinks and special files: skipped
    }
    closedir(d);
    return ok;
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

// The shared fork harness: run `childFn(writeFd)` as the user, stream its
// output into `dest`. Returns the byte count, or -1 on any failure
// (including a nonzero child exit, overflow, or deadline).
qint64 runAsUser(const SudoUser &user, const std::function<int(int)> &childFn,
                 QIODevice *dest, qint64 maxBytes)
{
    int fds[2];
    if (pipe(fds) != 0)
        return -1;
    const pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0) {
        close(fds[0]);
        if (!dropToUser(user))
            _exit(1);
        _exit(childFn(fds[1]));
    }
    close(fds[1]);
    const qint64 total = drainPipe(fds[0], dest, maxBytes);
    close(fds[0]);
    if (total < 0) {
        kill(pid, SIGKILL);
        reapOk(pid);
        return -1;
    }
    if (!reapOk(pid))
        return -1;
    return total;
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
    const QByteArray p = QFile::encodeName(path);
    QBuffer text;
    text.open(QIODevice::WriteOnly);
    const qint64 got = runAsUser(
        user_,
        [&p](int outFd) -> int {
            struct stat st;
            const int fd = openRegularAsUser(p, &st);
            if (fd < 0)
                return 1;
            close(fd);
            char buf[32];
            const int len = snprintf(buf, sizeof(buf), "%lld",
                                     static_cast<long long>(st.st_size));
            return (len > 0 && writeAll(outFd, buf, size_t(len))) ? 0 : 1;
        },
        &text, 32);
    if (got < 0)
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
    const QByteArray p = QFile::encodeName(path);
    const qint64 total = runAsUser(
        user_,
        [&p](int outFd) -> int {
            struct stat st;
            const int fd = openRegularAsUser(p, &st);
            if (fd < 0)
                return 1;
            char buf[1 << 16];
            for (;;) {
                const ssize_t n = ::read(fd, buf, sizeof(buf));
                if (n < 0) {
                    if (errno == EINTR)
                        continue;
                    return 1;
                }
                if (n == 0)
                    return 0;
                if (!writeAll(outFd, buf, size_t(n)))
                    return 1;
            }
        },
        dest, maxBytes);
    if (total < 0)
        return false;
    if (written)
        *written = total;
    return true;
}

QStringList ForkUserFiles::listSubdirs(const QString &path)
{
    const QByteArray p = QFile::encodeName(path);
    QBuffer text;
    text.open(QIODevice::WriteOnly);
    const qint64 got = runAsUser(
        user_,
        [&p](int outFd) -> int {
            DIR *d = opendir(p.constData());
            if (!d)
                return 1;
            struct dirent *e;
            bool ok = true;
            while (ok && (e = readdir(d)) != nullptr) {
                const QByteArray name(e->d_name);
                if (!nameIsClean(name))
                    continue;
                struct stat st;
                if (lstat((p + '/' + name).constData(), &st) != 0
                    || !S_ISDIR(st.st_mode))
                    continue;
                ok = writeAll(outFd, name.constData(), size_t(name.size()))
                    && writeAll(outFd, "\n", 1);
            }
            closedir(d);
            return ok ? 0 : 1;
        },
        &text, 1 << 20);
    QStringList out;
    if (got < 0)
        return out;
    const QList<QByteArray> lines = text.buffer().split('\n');
    for (const QByteArray &line : lines) {
        if (!line.isEmpty())
            out << QFile::decodeName(line);
    }
    return out;
}

bool ForkUserFiles::listFilesRecursive(const QString &dir,
                                       QList<UserFileEntry> *out)
{
    const QByteArray base = QFile::encodeName(dir);
    QBuffer text;
    text.open(QIODevice::WriteOnly);
    const qint64 got = runAsUser(
        user_,
        [&base](int outFd) -> int {
            return walkAsUser(outFd, base, QByteArray(), 0) ? 0 : 1;
        },
        &text, 16 << 20);
    if (got < 0)
        return false;
    const QList<QByteArray> lines = text.buffer().split('\n');
    for (const QByteArray &line : lines) {
        if (line.isEmpty())
            continue;
        const int tab = line.indexOf('\t');
        if (tab <= 0)
            return false;
        bool ok = false;
        const qint64 sz = line.left(tab).toLongLong(&ok);
        if (!ok || sz < 0)
            return false;
        out->append({QFile::decodeName(line.mid(tab + 1)), sz});
    }
    return true;
}

#endif // Q_OS_UNIX

} // namespace EspOps
