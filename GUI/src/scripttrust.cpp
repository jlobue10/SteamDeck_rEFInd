#include "scripttrust.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace ScriptTrust {

namespace {

bool isBelow(const QString &path, const QString &root)
{
    const QString cleanPath = QDir::cleanPath(path);
    QString cleanRoot = QDir::cleanPath(root);
    if (!cleanRoot.endsWith('/'))
        cleanRoot += '/';
    return cleanPath.startsWith(cleanRoot, Qt::CaseInsensitive);
}

QString tr(const char *text)
{
    return QCoreApplication::translate("ScriptTrust", text);
}

} // namespace

#ifdef Q_OS_WIN
QString longPath(const QString &path)
{
    const QString native = QDir::toNativeSeparators(path);
    const std::wstring wide = native.toStdWString();
    DWORD length = GetLongPathNameW(wide.c_str(), nullptr, 0);
    if (length == 0)
        return path;
    std::wstring buffer(length, L'\0');
    length = GetLongPathNameW(wide.c_str(), buffer.data(), length);
    if (length == 0 || length >= buffer.size())
        return path;
    buffer.resize(length);
    return QDir::fromNativeSeparators(QString::fromStdWString(buffer));
}
#endif

QString canonicalPath(const QString &path)
{
#ifdef Q_OS_WIN
    return QFileInfo(longPath(path)).canonicalFilePath();
#else
    return QFileInfo(path).canonicalFilePath();
#endif
}

Verdict evaluate(const QString &appDir, const QString &scriptName,
                 const QStringList &protectedRoots, const Canonicalize &canonicalize)
{
    Verdict verdict;
    const QString app = canonicalize(appDir);
    if (appDir.isEmpty() || app.isEmpty()) {
        verdict.detail = tr("The application directory could not be resolved (%1).")
                             .arg(QDir::toNativeSeparators(appDir));
        return verdict;
    }

    const QString expected = app + QLatin1String("/windows/") + scriptName;
    const QString script = canonicalize(expected);
    if (script.isEmpty()) {
        verdict.detail = tr("The script %1 is missing from %2. Reinstall the application to restore it.")
                             .arg(scriptName, QDir::toNativeSeparators(app + QLatin1String("/windows")));
        return verdict;
    }
    if (!isBelow(script, app)) {
        verdict.detail = tr("The script %1 resolves to %2, outside the application directory, "
                            "so it is not run.")
                             .arg(scriptName, QDir::toNativeSeparators(script));
        return verdict;
    }

    QStringList roots;
    for (const QString &root : protectedRoots) {
        if (root.isEmpty())
            continue;
        QString canonicalRoot = canonicalize(root);
        if (canonicalRoot.isEmpty())
            canonicalRoot = QDir::fromNativeSeparators(root);
        roots << canonicalRoot;
        if (isBelow(app, canonicalRoot)) {
            verdict.scriptPath = script;
            return verdict;
        }
    }
    verdict.detail = tr("The application is running from %1, which is not inside a protected program "
                        "directory (%2). The installer script runs with administrator rights, so it "
                        "is only started from a location standard users cannot modify. Reinstall "
                        "with the setup program to its default location.")
                         .arg(QDir::toNativeSeparators(app),
                              roots.isEmpty() ? tr("none found")
                                              : QDir::toNativeSeparators(roots.join(QLatin1String("; "))));
    return verdict;
}

} // namespace ScriptTrust
