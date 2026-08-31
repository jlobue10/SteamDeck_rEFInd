// Parity-locked between rEFInd_GUI and SteamDeck_rEFInd — see loadoption.h.

#include "randomize.h"
#include "espconstants.h"
#include "userio.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QTemporaryFile>

#include <algorithm>
#include <cstdio>
#include <random>

#ifdef Q_OS_UNIX
#include <pwd.h>
#include <sys/stat.h>
#endif

namespace EspOps {

namespace {

const char kIncludeLine[] = "include themes/active_theme.conf";
const qint64 kMaxBackgroundBytes = 33554432; // 32 MiB, matching the scripts
const int kMaxBackgroundAttempts = 3;
const char kPngSignature[] = "\x89PNG\r\n\x1a\n";

void warn(QStringList *warnings, const QString &line)
{
    if (warnings)
        warnings->append(line);
}

QByteArray readWhole(const QString &path, qint64 cap)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.read(cap);
}

// Publish `content` as <dir>/<name> through a same-directory staging file
// (never truncate the live file in place), sweeping stale staging files of
// the same shape first.
bool publishStaged(const QString &dir, const QString &name,
                   const QByteArray &content)
{
    {
        const QDir d(dir);
        const QStringList stale = d.entryList(
            {QStringLiteral(".%1.*").arg(name)}, QDir::Files | QDir::Hidden);
        for (const QString &s : stale)
            QFile::remove(d.filePath(s));
    }
    QString stageName;
    {
        // Scoped: QTemporaryFile keeps its native handle open even after
        // close() (name reservation), and Windows cannot rename a file with
        // an open handle — destruct before publishing.
        QTemporaryFile stage(dir + QStringLiteral("/.%1.XXXXXX").arg(name));
        stage.setAutoRemove(false);
        if (!stage.open())
            return false;
        stageName = stage.fileName();
        if (stage.write(content) != content.size() || !stage.flush()) {
            stage.remove();
            return false;
        }
        stage.close();
    }
    if (!publishRename(stageName, dir + QLatin1Char('/') + name)) {
        QFile::remove(stageName);
        return false;
    }
    return true;
}

} // namespace

int randomizeTheme(const QString &refindDir, QStringList *warnings)
{
    // Nothing to do when the live refind.conf does not include
    // active_theme.conf — theming off is the normal state, so this exits
    // silently (no warning line).
    const QByteArray conf =
        readWhole(refindDir + QLatin1String("/refind.conf"), 4 << 20);
    if (!conf.contains(kIncludeLine))
        return 0;

    const QString themesDir = refindDir + QLatin1String("/themes");
    const QString activePath = themesDir + QLatin1String("/active_theme.conf");

    // Candidates: every installed themes/<name>/theme.conf (root-owned ESP
    // content — no user-writable input anywhere in this subcommand).
    QStringList candidates;
    const QStringList names = QDir(themesDir).entryList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QString &n : names) {
        const QString confPath = themesDir + QLatin1Char('/') + n
            + QLatin1String("/theme.conf");
        const QFileInfo fi(confPath);
        if (fi.isFile() && fi.size() > 0)
            candidates << confPath;
    }
    if (candidates.isEmpty()) {
        warn(warnings,
             QStringLiteral("no themes found under %1; keeping the current "
                            "theme")
                 .arg(themesDir));
        return 0;
    }

    // With more than one available, avoid re-picking the one that is
    // already active.
    QStringList pickFrom = candidates;
    if (candidates.size() > 1 && QFileInfo::exists(activePath)) {
        const QByteArray active = readWhole(activePath, 4 << 20);
        QStringList fresh;
        for (const QString &c : candidates) {
            if (readWhole(c, 4 << 20) != active)
                fresh << c;
        }
        if (!fresh.isEmpty())
            pickFrom = fresh;
    }

    const QString pick =
        pickFrom.at(int(QRandomGenerator::global()->bounded(quint32(pickFrom.size()))));
    const QByteArray content = readWhole(pick, 4 << 20);
    if (content.isEmpty()) {
        warn(warnings,
             QStringLiteral("could not read %1; keeping the current theme")
                 .arg(pick));
        return 0;
    }
    if (!publishStaged(themesDir, QStringLiteral("active_theme.conf"), content))
        return 1; // internal error: staging/rename on a resolved, writable ESP
    return 0;
}

#ifdef Q_OS_UNIX

namespace {

// Read the backgrounds-directory pointer file: root-owned, not a symlink,
// not writable by group/other, no special mode bits, exactly one line
// holding an absolute path. Read as DATA — never sourced/executed.
QString loadBackgroundDir(QStringList *warnings)
{
    const QString pointer = QString::fromLatin1(kBackgroundDirPointer);
    struct stat st;
    if (::lstat(QFile::encodeName(pointer).constData(), &st) != 0)
        return QString(); // absent: nothing configured, no warning
    const auto malformed = [&]() {
        warn(warnings,
             QStringLiteral("ignoring unsafe or malformed %1").arg(pointer));
        return QString();
    };
    if (!S_ISREG(st.st_mode) || st.st_uid != 0 || st.st_gid != 0)
        return malformed();
    if ((st.st_mode & 07777) & 07022) // setuid/setgid/sticky/group-w/other-w
        return malformed();
    QFile f(pointer);
    if (!f.open(QIODevice::ReadOnly))
        return malformed();
    const QList<QByteArray> lines = f.read(4096).split('\n');
    // One path line, optionally one trailing newline.
    if (lines.isEmpty() || lines.first().isEmpty()
        || (lines.size() > 1 && !lines.at(1).isEmpty())
        || lines.size() > 2)
        return malformed();
    const QString dir = QFile::decodeName(lines.first());
    if (!dir.startsWith(QLatin1Char('/')))
        return malformed();
    return dir;
}

} // namespace

int randomizeBackground(const QString &refindDir, QStringList *warnings)
{
    const QString bgDir = loadBackgroundDir(warnings);
    if (bgDir.isEmpty())
        return 0;

    // The unit is root, but the input directory belongs to the desktop
    // user: select and read the file in the directory owner's unprivileged
    // context (the fd-handoff equivalent of the scripts' runuser).
    // lstat (not stat): a stat() here follows a symlink, so a user could point
    // the final path component at another local user's directory and have us
    // drop to that user and stream their PNGs onto the shared ESP. Refuse a
    // symlinked or non-directory target instead.
    struct stat st;
    if (::lstat(QFile::encodeName(bgDir).constData(), &st) != 0)
        return 0;
    if (S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) {
        warn(warnings,
             QStringLiteral("refusing a background directory that is a symlink "
                            "or not a directory"));
        return 0;
    }
    if (st.st_uid == 0) {
        warn(warnings,
             QStringLiteral("refusing a background directory without a "
                            "non-root owner"));
        return 0;
    }
    const struct passwd *pw = getpwuid(st.st_uid);
    if (!pw || !pw->pw_name || pw->pw_uid == 0) {
        warn(warnings,
             QStringLiteral("could not resolve a safe account for the "
                            "background directory"));
        return 0;
    }
    SudoUser owner;
    owner.valid = true;
    owner.name = QString::fromLocal8Bit(pw->pw_name);
    owner.home = QFile::decodeName(pw->pw_dir);
    owner.uid = unsigned(pw->pw_uid);
    owner.gid = unsigned(pw->pw_gid);
    ForkUserFiles user(owner);

    // Top-level *.png files only (the scripts' find -maxdepth 1 -iname).
    QList<UserFileEntry> entries;
    if (!user.listFilesRecursive(bgDir, &entries)) {
        warn(warnings,
             QStringLiteral("could not enumerate background images safely; "
                            "keeping the current background"));
        return 0;
    }
    QStringList candidates;
    for (const UserFileEntry &e : entries) {
        if (!e.relPath.contains(QLatin1Char('/'))
            && e.relPath.endsWith(QLatin1String(".png"), Qt::CaseInsensitive))
            candidates << e.relPath;
    }
    if (candidates.isEmpty())
        return 0;

    std::shuffle(candidates.begin(), candidates.end(),
                 std::mt19937(QRandomGenerator::global()->generate()));

    // A single bad file (renamed JPEG, oversized, unreadable) should not
    // spoil the boot: walk the shuffled candidates and take the first one
    // that validates, bounded so a directory full of junk still finishes
    // quickly.
    QByteArray staged;
    const int attempts = qMin(kMaxBackgroundAttempts, int(candidates.size()));
    for (int i = 0; i < attempts; ++i) {
        const QString candidate = bgDir + QLatin1Char('/') + candidates.at(i);
        QBuffer buf(&staged);
        staged.clear();
        buf.open(QIODevice::WriteOnly);
        qint64 written = 0;
        if (!user.read(candidate, &buf, kMaxBackgroundBytes, &written)
            || written >= kMaxBackgroundBytes) {
            warn(warnings,
                 QStringLiteral("could not read '%1' safely; trying another")
                     .arg(candidate));
            staged.clear();
            continue;
        }
        if (!staged.startsWith(QByteArray(kPngSignature, 8))) {
            warn(warnings,
                 QStringLiteral("'%1' is not a valid PNG file; trying another")
                     .arg(candidate));
            staged.clear();
            continue;
        }
        break;
    }
    if (staged.isEmpty()) {
        warn(warnings,
             QStringLiteral("no usable background image found; keeping the "
                            "current background"));
        return 0;
    }

    if (!QFileInfo(refindDir).isDir())
        return 0;
    if (!publishStaged(refindDir, QStringLiteral("background.png"), staged))
        return 1; // internal error: staging/rename on a resolved, writable ESP
    return 0;
}

#else // Windows: the Scheduled Task runs elevated IN the logged-on user's
      // session, so the backgrounds live in that user's own %LOCALAPPDATA%
      // and are read directly — no pointer file, no privilege drop (the
      // same trust model as rEFInd_bg_randomizer.ps1, which this replaces).

int randomizeBackground(const QString &refindDir, QStringList *warnings)
{
    QString base = QString::fromLocal8Bit(qgetenv("LOCALAPPDATA"));
    if (base.isEmpty())
        base = QDir::homePath() + QLatin1String("/AppData/Local");
    const QString bgDir = QDir::fromNativeSeparators(base) + QLatin1Char('/')
        + QLatin1String(kDataDirName) + QLatin1String("/backgrounds");
    if (!QFileInfo(bgDir).isDir())
        return 0;

    DirectUserFiles user;
    QList<UserFileEntry> entries;
    if (!user.listFilesRecursive(bgDir, &entries)) {
        warn(warnings,
             QStringLiteral("could not enumerate background images; keeping "
                            "the current background"));
        return 0;
    }
    QStringList candidates;
    for (const UserFileEntry &e : entries) {
        if (!e.relPath.contains(QLatin1Char('/'))
            && e.relPath.endsWith(QLatin1String(".png"), Qt::CaseInsensitive))
            candidates << e.relPath;
    }
    if (candidates.isEmpty())
        return 0;

    std::shuffle(candidates.begin(), candidates.end(),
                 std::mt19937(QRandomGenerator::global()->generate()));

    QByteArray staged;
    const int attempts = qMin(kMaxBackgroundAttempts, int(candidates.size()));
    for (int i = 0; i < attempts; ++i) {
        const QString candidate = bgDir + QLatin1Char('/') + candidates.at(i);
        QBuffer buf(&staged);
        staged.clear();
        buf.open(QIODevice::WriteOnly);
        qint64 written = 0;
        if (!user.read(candidate, &buf, kMaxBackgroundBytes, &written)
            || written >= kMaxBackgroundBytes) {
            warn(warnings,
                 QStringLiteral("could not read '%1'; trying another")
                     .arg(candidate));
            staged.clear();
            continue;
        }
        if (!staged.startsWith(QByteArray(kPngSignature, 8))) {
            warn(warnings,
                 QStringLiteral("'%1' is not a valid PNG file; trying another")
                     .arg(candidate));
            staged.clear();
            continue;
        }
        break;
    }
    if (staged.isEmpty()) {
        warn(warnings,
             QStringLiteral("no usable background image found; keeping the "
                            "current background"));
        return 0;
    }

    if (!QFileInfo(refindDir).isDir())
        return 0;
    if (!publishStaged(refindDir, QStringLiteral("background.png"), staged))
        return 1;
    return 0;
}

#endif

} // namespace EspOps
