// Parity-locked between rEFInd_GUI and SteamDeck_rEFInd — see loadoption.h.
//
// Behavior is a contract-first port of the root themes install scripts;
// message texts are kept close to the scripts' so users and issue reports
// see familiar diagnostics. Change behavior only in both repos together.

#include "themesinstall.h"
#include "espconstants.h"
#include "userio.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QTemporaryDir>

#include <cstdio>

namespace EspOps {

namespace {

const qint64 kMaxThemeFileBytes = Q_INT64_C(64) * 1024 * 1024;

// A relative path is publishable only if no component can escape the
// staging/theme directory. The scripts got this from GNU tar's refusal of
// absolute and ".." members; the native port checks explicitly.
bool relPathSafe(const QString &rel)
{
    if (rel.isEmpty() || rel.startsWith(QLatin1Char('/'))
        || rel.contains(QLatin1Char('\\')))
        return false;
    const QStringList parts = rel.split(QLatin1Char('/'));
    for (const QString &p : parts) {
        if (p.isEmpty() || p == QLatin1String(".") || p == QLatin1String(".."))
            return false;
    }
    return true;
}

bool renamePath(const QString &from, const QString &to)
{
    return std::rename(QFile::encodeName(from).constData(),
                       QFile::encodeName(to).constData())
        == 0;
}

} // namespace

InstallOutcome installThemeSet(UserFiles &user, const QString &srcThemesDir,
                               const QString &refindDir)
{
    InstallOutcome out;
    const auto fail = [&](int code, const QStringList &lines) {
        out.exitCode = code;
        out.lines += lines;
        return out;
    };

    const QString target = refindDir + QLatin1String("/themes");

    // Enumerate the themes to install in the invoking user's context (root
    // never reads under the user's home): a theme is a first-level,
    // non-hidden directory whose theme.conf is a non-empty regular file.
    QStringList themes;
    const QStringList dirs = user.listSubdirs(srcThemesDir);
    for (const QString &name : dirs) {
        if (name.startsWith(QLatin1Char('.')))
            continue;
        qint64 confSize = 0;
        if (user.statRegular(srcThemesDir + QLatin1Char('/') + name
                                 + QLatin1String("/theme.conf"),
                             &confSize)
            && confSize > 0)
            themes << name;
    }
    if (themes.isEmpty()) {
        return fail(6,
                    {QStringLiteral("No themes were found in %1.").arg(srcThemesDir),
                     QStringLiteral("Re-run the GUI installer (%1) to stage the "
                                    "shipped themes.")
                         .arg(QLatin1String(kInstallerName))});
    }

    // Enumerate every theme's files up front: the listing feeds both the
    // free-space check (the scripts' `du -sk` as the user) and the copy.
    struct ThemePlan
    {
        QString name;
        QList<UserFileEntry> files;
    };
    QList<ThemePlan> plans;
    qint64 neededBytes = 0;
    for (const QString &name : themes) {
        ThemePlan plan;
        plan.name = name;
        if (!user.listFilesRecursive(srcThemesDir + QLatin1Char('/') + name,
                                     &plan.files)) {
            return fail(5,
                        {QStringLiteral("Failed while reading the themes in %1.")
                             .arg(srcThemesDir)});
        }
        for (const UserFileEntry &e : plan.files)
            neededBytes += e.size;
        plans.append(plan);
    }

    if (!QDir().mkpath(target)) {
        return fail(4,
                    {QStringLiteral("Could not create %1 -- the EFI System "
                                    "Partition may be mounted read-only.")
                         .arg(target)});
    }

    // Free-space check before anything is written: the source size (as
    // measured by the user) plus headroom for the staging copy that briefly
    // coexists with the live tree.
    const qint64 neededKb = neededBytes / 1024;
    const qint64 availKb = QStorageInfo(target).bytesAvailable() / 1024;
    if (availKb < neededKb + 4096) {
        return fail(5,
                    {QStringLiteral("Not enough free space on the EFI System "
                                    "Partition: need about %1 MB, %2 KB "
                                    "available.")
                         .arg(neededKb / 1024 + 5)
                         .arg(availKb),
                     QStringLiteral("Nothing was changed.")});
    }

    // Best-effort sweep of staging/backup directories an earlier
    // interrupted run left behind (no cleanup survives SIGKILL or power
    // loss). Only the exact shapes created below are matched.
    {
        const QDir t(target);
        const QStringList stale =
            t.entryList({QStringLiteral(".install-staging-*"),
                         QStringLiteral(".old-*")},
                        QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
        for (const QString &s : stale)
            QDir(t.filePath(s)).removeRecursively();
    }

    // The whole copy lands in a private staging directory on the target
    // filesystem first; QTemporaryDir removes whatever remains of it on
    // every exit path (the scripts' EXIT trap).
    QTemporaryDir staging(target + QLatin1String("/.install-staging-XXXXXX"));
    if (!staging.isValid()) {
        return fail(5,
                    {QStringLiteral("Could not create a staging directory in %1 "
                                    "-- the ESP may be full or read-only.")
                         .arg(target)});
    }

    const QStringList copyFailure = {
        QStringLiteral("Failed while copying the themes to %1 -- the ESP may "
                       "be full.")
            .arg(target),
        QStringLiteral("The live themes were not changed."),
    };
    for (const ThemePlan &plan : plans) {
        const QString stageRoot = staging.path() + QLatin1Char('/') + plan.name;
        for (const UserFileEntry &e : plan.files) {
            if (!relPathSafe(e.relPath))
                return fail(5, copyFailure);
            const QString dest = stageRoot + QLatin1Char('/') + e.relPath;
            if (!QDir().mkpath(QFileInfo(dest).absolutePath()))
                return fail(5, copyFailure);
            QFile f(dest);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return fail(5, copyFailure);
            if (!user.read(srcThemesDir + QLatin1Char('/') + plan.name
                               + QLatin1Char('/') + e.relPath,
                           &f, kMaxThemeFileBytes, nullptr)
                || !f.flush())
                return fail(5, copyFailure);
        }
    }

    // Swap each staged theme directory into place. The live tree is only
    // touched after the full staging copy above succeeded.
    int installed = 0;
    for (const ThemePlan &plan : plans) {
        const QString staged = staging.path() + QLatin1Char('/') + plan.name;
        const QString live = target + QLatin1Char('/') + plan.name;
        const QString old = target + QLatin1String("/.old-") + plan.name;
        if (!QFileInfo(staged).isDir()) {
            return fail(5,
                        {QStringLiteral("Staged copy of '%1' is missing; the "
                                        "live themes were not changed further.")
                             .arg(plan.name)});
        }
        QDir(old).removeRecursively();
        if (QFileInfo::exists(live) && !renamePath(live, old)) {
            return fail(5,
                        {QStringLiteral("Could not replace the existing theme "
                                        "'%1'.")
                             .arg(plan.name)});
        }
        if (!renamePath(staged, live)) {
            // Put the previous version back so the theme is not lost
            // entirely.
            renamePath(old, live);
            return fail(5,
                        {QStringLiteral("Failed while publishing the theme "
                                        "'%1'.")
                             .arg(plan.name)});
        }
        QDir(old).removeRecursively();
        ++installed;
    }

    out.exitCode = 0;
    out.lines << QStringLiteral("Installed %1 theme(s) to %2")
                     .arg(installed)
                     .arg(target);
    return out;
}

} // namespace EspOps
