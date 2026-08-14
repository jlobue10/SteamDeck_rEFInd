// Parity-locked between rEFInd_GUI and SteamDeck_rEFInd — see loadoption.h.
//
// Behavior is a contract-first port of the root config install scripts;
// message texts are kept close to the scripts' so users and issue reports
// see familiar diagnostics. Change behavior only in both repos together.

#include "configinstall.h"
#include "userio.h"

#include <QDir>
#include <QFile>
#include <QMap>
#include <QTemporaryFile>

#include <cstdio>

namespace EspOps {

namespace {

// The full config set. Order matters twice: the stale sweep visits all of
// them, and the publish loop publishes in this order with refind.conf
// forced last.
const char *const kFiles[] = {
    "refind.conf",
    "background.png",
    "os_icon1.png",
    "os_icon2.png",
    "os_icon3.png",
    "os_icon4.png",
    "active_theme.conf",
};

// Sanity ceiling per source file. The scripts had none (a runaway file
// simply filled the ESP and failed the staged write); the native port
// refuses absurd sizes up front so a bogus multi-GB "refind.conf" can't
// grind through ESP space first. Far above any real config/PNG.
const qint64 kMaxSourceBytes = Q_INT64_C(256) * 1024 * 1024;

} // namespace

QString destDirFor(const QString &fileName, const QString &refindDir)
{
    if (fileName == QLatin1String("active_theme.conf"))
        return refindDir + QLatin1String("/themes");
    return refindDir;
}

InstallOutcome installConfigSet(UserFiles &user, const QString &srcDir,
                                const QString &refindDir)
{
    InstallOutcome out;
    QMap<QString, QString> staged;      // file name -> staging path
    QStringList stagedPaths;            // everything to delete on failure

    const auto fail = [&](int code, const QStringList &lines) {
        for (const QString &p : stagedPaths)
            QFile::remove(p);
        out.exitCode = code;
        out.lines += lines;
        return out;
    };

    if (!QDir().mkpath(refindDir)) {
        return fail(4,
                    {QStringLiteral("Could not create %1 -- the EFI System "
                                    "Partition may be mounted read-only.")
                         .arg(refindDir)});
    }

    // Best-effort sweep of staging files left behind by an earlier
    // interrupted run (no cleanup survives SIGKILL or power loss, and every
    // run mints new random staging names, so leftovers would otherwise
    // accumulate on the small ESP). Only the exact ".<name>.new.<suffix>"
    // shapes created below are matched, so no live file can be touched.
    QStringList sweepNames;
    for (const char *f : kFiles)
        sweepNames << QString::fromLatin1(f);
    sweepNames << QStringLiteral("refind.conf.prev");
    for (const QString &f : sweepNames) {
        QDir d(destDirFor(f, refindDir));
        const QStringList stale = d.entryList(
            {QStringLiteral(".%1.new.*").arg(f)}, QDir::Files | QDir::Hidden);
        for (const QString &s : stale)
            QFile::remove(d.filePath(s));
    }

    // A config is mandatory. Images are optional, but images alone must not
    // count as a successful installation.
    qint64 confSize = 0;
    if (!user.statRegular(srcDir + QLatin1String("/refind.conf"), &confSize)
        || confSize <= 0) {
        return fail(6,
                    {QStringLiteral("No non-empty refind.conf was found in %1.")
                         .arg(srcDir),
                     QStringLiteral("Use Create Config in the GUI first.")});
    }

    int copied = 0;
    for (const char *fc : kFiles) {
        const QString f = QString::fromLatin1(fc);
        // Existence check AND content read both run in the invoking user's
        // context, never as root (see userio.h).
        if (!user.statRegular(srcDir + QLatin1Char('/') + f, nullptr))
            continue;
        const QString destDir = destDirFor(f, refindDir);
        if (!QDir().mkpath(destDir)) {
            return fail(4,
                        {QStringLiteral("Could not create %1 -- the EFI System "
                                        "Partition may be mounted read-only.")
                             .arg(destDir)});
        }
        QTemporaryFile stage(destDir + QStringLiteral("/.%1.new.XXXXXX").arg(f));
        stage.setAutoRemove(false);
        if (!stage.open()) {
            return fail(5,
                        {QStringLiteral("Could not create a staging file in %1 "
                                        "-- the ESP may be full or read-only.")
                             .arg(destDir)});
        }
        stagedPaths << stage.fileName();
        qint64 written = 0;
        if (!user.read(srcDir + QLatin1Char('/') + f, &stage, kMaxSourceBytes,
                       &written)
            || !stage.flush()) {
            return fail(5,
                        {QStringLiteral("Failed while copying %1 to %2 -- the "
                                        "ESP may be full or read-only.")
                             .arg(f, refindDir)});
        }
        if (f == QLatin1String("refind.conf") && written <= 0) {
            return fail(5,
                        {QStringLiteral("The staged refind.conf is empty; the "
                                        "live config was not changed.")});
        }
        stage.close();
        staged.insert(f, stage.fileName());
        ++copied;
    }

    if (!staged.contains(QStringLiteral("refind.conf"))) {
        return fail(6,
                    {QStringLiteral("refind.conf disappeared while it was being "
                                    "staged; the live config was not changed.")});
    }

    // Preserve the rollback config through its own same-directory staging
    // file (a straight copy could be truncated by a full ESP).
    const QString liveConf = refindDir + QLatin1String("/refind.conf");
    if (QFile::exists(liveConf)) {
        QString backupName;
        bool ok = false;
        {
            // Scoped: QTemporaryFile keeps its native handle open even after
            // close() (name reservation), and Windows cannot rename a file
            // with an open handle — destruct before publishing.
            QTemporaryFile backup(
                refindDir + QStringLiteral("/.refind.conf.prev.new.XXXXXX"));
            backup.setAutoRemove(false);
            ok = backup.open();
            if (ok) {
                backupName = backup.fileName();
                stagedPaths << backupName;
                QFile src(liveConf);
                ok = src.open(QIODevice::ReadOnly);
                while (ok && !src.atEnd()) {
                    const QByteArray chunk = src.read(1 << 16);
                    ok = !chunk.isEmpty() || src.atEnd();
                    if (!chunk.isEmpty())
                        ok = backup.write(chunk) == chunk.size() && ok;
                }
                ok = ok && backup.flush();
                backup.close();
            }
        }
        ok = ok
            && publishRename(backupName,
                             refindDir + QLatin1String("/refind.conf.prev"));
        if (!ok) {
            return fail(5,
                        {QStringLiteral("Could not preserve the previous "
                                        "refind.conf; the live config was not "
                                        "changed.")});
        }
    }

    // Publish assets first and refind.conf last (staged, publish-last: the
    // renames stay on the ESP, so failed source or destination writes cannot
    // truncate existing live files).
    for (const char *fc : kFiles) {
        const QString f = QString::fromLatin1(fc);
        if (f == QLatin1String("refind.conf") || !staged.contains(f))
            continue;
        if (!publishRename(staged.value(f), destDirFor(f, refindDir)
                                                + QLatin1Char('/') + f)) {
            return fail(5,
                        {QStringLiteral("Failed while publishing %1; the live "
                                        "config was not changed.")
                             .arg(f)});
        }
        stagedPaths.removeAll(staged.value(f));
    }
    if (!publishRename(staged.value(QStringLiteral("refind.conf")), liveConf)) {
        return fail(5,
                    {QStringLiteral("Failed while publishing refind.conf; the "
                                    "previous config is still active.")});
    }

    out.exitCode = 0;
    out.lines << QStringLiteral("Installed %1 file(s) to %2")
                     .arg(copied)
                     .arg(refindDir);
    return out;
}

} // namespace EspOps
