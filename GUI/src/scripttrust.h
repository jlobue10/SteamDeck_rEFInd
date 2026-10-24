#ifndef SCRIPTTRUST_H
#define SCRIPTTRUST_H

#include <QString>
#include <QStringList>

#include <functional>

// Decides whether a privileged helper script shipped next to the executable
// may be run, and says why not when it may not. The elevated GUI runs these
// scripts, so they are trusted only from a location standard users cannot
// modify (Program Files on Windows). Pure over the paths it is given so the
// decision is testable; path resolution is injected.
namespace ScriptTrust {

struct Verdict
{
    QString scriptPath; // native-independent path of the script; empty when refused
    QString detail;     // user-facing reason for a refusal; empty when trusted
    bool trusted() const { return !scriptPath.isEmpty(); }
};

// Resolves a path to its canonical form, or an empty string when it does not
// exist. The default expands 8.3 short names on Windows before canonicalizing:
// Qt's canonicalFilePath() keeps "PROGRA~1" as is, which would never compare
// equal to the known folder "Program Files".
using Canonicalize = std::function<QString(const QString &)>;
QString canonicalPath(const QString &path);

#ifdef Q_OS_WIN
// Expands 8.3 short segments in a path (GetLongPathName). Returns the input
// unchanged when the path cannot be resolved.
QString longPath(const QString &path);
#endif

// `appDir` is the executable's directory, `scriptName` the file expected in
// its "windows" subdirectory, `protectedRoots` the directories standard users
// cannot write to (empty entries are ignored).
Verdict evaluate(const QString &appDir, const QString &scriptName,
                 const QStringList &protectedRoots,
                 const Canonicalize &canonicalize = canonicalPath);

} // namespace ScriptTrust

#endif // SCRIPTTRUST_H
