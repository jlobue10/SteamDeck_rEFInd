#include "platform.h"
#include "espops/espconstants.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

#ifdef Q_OS_WIN
#include <qt_windows.h> // CREATE_NO_WINDOW
#include <shlobj.h>     // SHGetKnownFolderPath
#else
#include <sys/stat.h> // ::stat, to trigger systemd ESP automounts
#endif

namespace Platform {

#ifdef Q_OS_WIN

QString dataDir()
{
    QString base = QProcessEnvironment::systemEnvironment().value(QStringLiteral("LOCALAPPDATA"));
    if (base.isEmpty())
        base = QDir::homePath() + "/AppData/Local";
    return QDir::fromNativeSeparators(base) + "/SteamDeck_rEFInd";
}

static bool copySeedDir(const QString &source, const QString &destination)
{
    const QDir sourceDir(source);
    const QFileInfo destinationInfo(destination);
    if (!sourceDir.exists()
        || (destinationInfo.exists() && !destinationInfo.isDir())
        || !QDir().mkpath(destination)) {
        return false;
    }

    for (const QFileInfo &entry : sourceDir.entryInfoList(
             QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)) {
        const QString target = destination + "/" + entry.fileName();
        const QFileInfo targetInfo(target);
        if (entry.isDir()) {
            if (targetInfo.exists() && !targetInfo.isDir())
                return false;
            if (!copySeedDir(entry.filePath(), target))
                return false;
        } else {
            // Seed assets are defaults. Add files introduced by an update,
            // but preserve every file the user already has.
            if (targetInfo.exists()) {
                if (!targetInfo.isFile())
                    return false;
                continue;
            }
            if (!QFile::copy(entry.filePath(), target))
                return false;
        }
    }
    return true;
}

void prepareDataDir()
{
    const QString root = dataDir();
    const QString shipped = QCoreApplication::applicationDirPath();
    QDir().mkpath(root + "/GUI");
    const QString seedConfig = shipped + "/GUI/refind.conf";
    const QString userConfig = root + "/GUI/refind.conf";
    if (!QFileInfo::exists(userConfig) && QFileInfo::exists(seedConfig))
        QFile::copy(seedConfig, userConfig);
    copySeedDir(shipped + "/icons", root + "/icons");
    copySeedDir(shipped + "/backgrounds", root + "/backgrounds");
    copySeedDir(shipped + "/themes", root + "/themes");
}

static bool isBelow(const QString &path, const QString &root)
{
    const QString cleanPath = QDir::cleanPath(path);
    QString cleanRoot = QDir::cleanPath(root);
    if (!cleanRoot.endsWith('/'))
        cleanRoot += '/';
    return cleanPath.startsWith(cleanRoot, Qt::CaseInsensitive);
}

static QString knownFolderPath(REFKNOWNFOLDERID folderId)
{
    PWSTR nativePath = nullptr;
    if (FAILED(SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &nativePath)))
        return {};
    const QString path = QDir::fromNativeSeparators(QString::fromWCharArray(nativePath));
    CoTaskMemFree(nativePath);
    return path;
}

static QString trustedScriptPath(const QString &name)
{
    const QString appDir = QFileInfo(QCoreApplication::applicationDirPath()).canonicalFilePath();
    const QString script = QFileInfo(appDir + "/windows/" + name).canonicalFilePath();
    if (appDir.isEmpty() || script.isEmpty() || !isBelow(script, appDir))
        return {};

    const QStringList protectedRoots = {
        knownFolderPath(FOLDERID_ProgramFiles),
        knownFolderPath(FOLDERID_ProgramFilesX64),
        knownFolderPath(FOLDERID_ProgramFilesX86)
    };
    for (const QString &root : protectedRoots) {
        if (!root.isEmpty() && isBelow(appDir, QDir::fromNativeSeparators(root)))
            return script;
    }
    return {};
}

QString windowsSystemExecutable(const QString &relativePath)
{
    QString systemDirectory(MAX_PATH, Qt::Uninitialized);
    UINT length = GetSystemDirectoryW(
        reinterpret_cast<LPWSTR>(systemDirectory.data()),
        static_cast<UINT>(systemDirectory.size()));
    if (length == 0)
        return {};
    if (length >= static_cast<UINT>(systemDirectory.size())) {
        systemDirectory.resize(static_cast<int>(length) + 1);
        length = GetSystemDirectoryW(
            reinterpret_cast<LPWSTR>(systemDirectory.data()),
            static_cast<UINT>(systemDirectory.size()));
        if (length == 0 || length >= static_cast<UINT>(systemDirectory.size()))
            return {};
    }
    systemDirectory.resize(static_cast<int>(length));
    return QDir::toNativeSeparators(
        QDir(QDir::fromNativeSeparators(systemDirectory)).filePath(relativePath));
}

static bool runScriptInWindow(const QString &scriptName, const QStringList &scriptArgs = {})
{
    const QString scriptPath = trustedScriptPath(scriptName);
    const QString powershell = windowsSystemExecutable(
        QStringLiteral("WindowsPowerShell/v1.0/powershell.exe"));
    if (scriptPath.isEmpty() || powershell.isEmpty())
        return false;
    QStringList args = {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                        QStringLiteral("Bypass"), QStringLiteral("-NoExit"),
                        QStringLiteral("-File"), QDir::toNativeSeparators(scriptPath)};
    args += scriptArgs;
    return QProcess::startDetached(powershell, args);
}

bool runInstallerScript(const QString &installSource)
{
    Q_UNUSED(installSource); // only the SourceForge download exists on Windows
    return runScriptInWindow(QStringLiteral("install_rEFInd.ps1"));
}

// Synchronous, window-less run of a trusted helper script with the output
// captured: the scripts' consoles used to flash open and vanish, leaving no
// trace of whether the install worked. The caller shows the result dialog
// from *output.
static int runTrustedScriptCaptured(const QString &scriptName, QString *output)
{
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
    const QString script = trustedScriptPath(scriptName);
    const QString powershell = windowsSystemExecutable(
        QStringLiteral("WindowsPowerShell/v1.0/powershell.exe"));
    if (script.isEmpty() || powershell.isEmpty()) {
        if (output)
            *output = QCoreApplication::translate(
                "Platform", "The privileged helper is not installed under Program Files.");
        return -1;
    }
    proc.start(powershell,
               {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                QStringLiteral("Bypass"), QStringLiteral("-File"),
                QDir::toNativeSeparators(script)});
    if (!proc.waitForStarted()) {
        if (output)
            *output = QCoreApplication::translate("Platform",
                                                  "powershell.exe could not be started.");
        return -1;
    }
    proc.waitForFinished(-1);
    if (output)
        *output = QString::fromLocal8Bit(proc.readAll());
    return proc.exitStatus() == QProcess::NormalExit ? proc.exitCode() : -1;
}

int installConfig(QString *output)
{
    return runTrustedScriptCaptured(QStringLiteral("install_config_from_GUI.ps1"), output);
}

int installThemes(QString *output)
{
    return runTrustedScriptCaptured(QStringLiteral("install_themes_from_GUI.ps1"), output);
}

bool installConfigShowsOwnDialogs()
{
    return false;
}

bool installConfigScriptTrusted(QString *detail)
{
    const QString script = trustedScriptPath(QStringLiteral("install_config_from_GUI.ps1"));
    if (detail)
        *detail = script.isEmpty()
            ? QCoreApplication::applicationDirPath() + "/windows/install_config_from_GUI.ps1"
            : script;
    return !script.isEmpty();
}

bool installThemesScriptTrusted(QString *detail)
{
    // Same trust rule as the config installer: the script must resolve under
    // the protected Program Files installation (Authenticode signing rewrites
    // the .ps1 files, so a build-time hash can't be used on Windows).
    const QString script = trustedScriptPath(QStringLiteral("install_themes_from_GUI.ps1"));
    if (detail)
        *detail = script.isEmpty()
            ? QCoreApplication::applicationDirPath() + "/windows/install_themes_from_GUI.ps1"
            : script;
    return !script.isEmpty();
}

bool setBackgroundRandomizer(bool enable)
{
    return runScriptInWindow(QStringLiteral("rEFInd_bg_randomizer_task.ps1"),
                             {enable ? QStringLiteral("-Enable") : QStringLiteral("-Disable")});
}

bool setThemeRandomizer(bool enable)
{
    return runScriptInWindow(QStringLiteral("rEFInd_theme_randomizer_task.ps1"),
                             {enable ? QStringLiteral("-Enable") : QStringLiteral("-Disable")});
}

bool setBootnextService(bool enable)
{
    // The Windows counterpart of bootnext-refind.service: a scheduled task
    // that sets UEFI BootNext to the rEFInd entry at each logon.
    return runScriptInWindow(QStringLiteral("bootnext_refind_task.ps1"),
                             {enable ? QStringLiteral("-Enable") : QStringLiteral("-Disable")});
}

int runEspDeepScan()
{
    return -1; // the elevated Windows build scans ESPs directly
}

bool espDeepScanUseful()
{
    return false;
}

bool firmwareBootnumSupported()
{
    return false;
}

bool preferWindowsAsDefault()
{
    return true;
}

QStringList installSourceOptions()
{
    return {QStringLiteral("Sourceforge")};
}

#else // Linux (SteamOS)

QString dataDir()
{
    return QDir::homePath() + "/.local/SteamDeck_rEFInd";
}

void prepareDataDir()
{
}

bool runInstallerScript(const QString &installSource)
{
    const QString script = dataDir()
        + (installSource == QLatin1String("Sourceforge")
               ? QStringLiteral("/scripts/sourceforge_install.sh")
               : QStringLiteral("/scripts/pacman_install.sh"));
    return QProcess::startDetached(QStringLiteral("xterm"), {QStringLiteral("-e"), script});
}

// True when install-GUI.sh has set up the passwordless path for the given
// helper subcommand: the sudoers rule in /etc/sudoers.d lets this user run
// exactly that argument vector without a password (`sudo -n -l <cmd>
// <arg>` exits 0 exactly then, without ever prompting).
static bool passwordlessHelperReady(const QString &subcommand)
{
    if (!QFile::exists(QLatin1String(EspOps::kHelperEtcPath)))
        return false;
    QProcess proc;
    proc.start(QStringLiteral("sudo"),
               {QStringLiteral("-n"), QStringLiteral("-l"),
                QLatin1String(EspOps::kHelperEtcPath), subcommand});
    if (!proc.waitForStarted())
        return false;
    proc.waitForFinished(-1);
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

// Runs one subcommand of the root-owned /etc helper binary
// (NATIVE_HELPER_DESIGN.md). Allowed without a password by the sudoers
// rule; -n keeps the GUI from hanging on a prompt if the rule vanished
// since the trust check. Run synchronously with the output captured — the
// caller presents the result dialog from *output, exactly like the Windows
// build. (The old zenity password fallback is gone: a machine without the
// sudoers rule gets the precise "re-run install-GUI.sh" dialog from the
// trust check instead.)
static int runRootHelperCaptured(const QString &subcommand, QString *output)
{
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(QStringLiteral("sudo"),
               {QStringLiteral("-n"),
                QLatin1String(EspOps::kHelperEtcPath), subcommand});
    if (!proc.waitForStarted()) {
        if (output)
            *output = QCoreApplication::translate("Platform",
                                                  "sudo could not be started.");
        return -1;
    }
    proc.waitForFinished(-1);
    if (output)
        *output = QString::fromLocal8Bit(proc.readAll());
    return proc.exitStatus() == QProcess::NormalExit ? proc.exitCode() : -1;
}

int installConfig(QString *output)
{
    return runRootHelperCaptured(QStringLiteral("install-config"), output);
}

bool installConfigShowsOwnDialogs()
{
    // The zenity fallback that owned its dialogs is gone; the GUI always
    // shows the captured output itself, like the Windows build.
    return false;
}

int installThemes(QString *output)
{
    return runRootHelperCaptured(QStringLiteral("install-themes"), output);
}

// The /etc helper runs as root via the passwordless sudoers rule. The
// security boundary is its root ownership (nothing user-writable is ever
// elevated); this handshake is skew detection, not tamper detection — it
// runs `helper --version` unprivileged (the file is world-executable) and
// requires the version this GUI was built with, so a helper left behind by
// an older or newer install-GUI.sh run produces a precise "reinstall"
// dialog instead of a subtle misbehavior. Together with the sudoers probe
// it replaces the pre-4.x SHA-256 script hash-check, which died with the
// on-disk scripts.
static bool helperReady(const QString &subcommand, QString *detail)
{
    const QString helper = QLatin1String(EspOps::kHelperEtcPath);
    QString found = QCoreApplication::translate("Platform", "missing");
    bool versionOk = false;
    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start(helper, {QStringLiteral("--version")});
    if (proc.waitForStarted() && proc.waitForFinished(10000)
        && proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0) {
        const QString version =
            QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
        if (!version.isEmpty())
            found = version;
        versionOk = version == QLatin1String(ESPOPS_APP_VERSION);
    }
    if (!versionOk) {
        if (detail)
            *detail = QCoreApplication::translate(
                          "Platform", "%1 (expected version %2, found: %3)")
                          .arg(helper, QLatin1String(ESPOPS_APP_VERSION), found);
        return false;
    }
    if (!passwordlessHelperReady(subcommand)) {
        if (detail)
            *detail = QCoreApplication::translate(
                          "Platform", "%1 (the passwordless sudo rule for '%2' is not installed)")
                          .arg(helper, subcommand);
        return false;
    }
    if (detail)
        detail->clear();
    return true;
}

bool installConfigScriptTrusted(QString *detail)
{
    return helperReady(QStringLiteral("install-config"), detail);
}

bool installThemesScriptTrusted(QString *detail)
{
    return helperReady(QStringLiteral("install-themes"), detail);
}

static bool systemctlInXterm(const QString &command)
{
    return QProcess::startDetached(QStringLiteral("xterm"),
                                   {QStringLiteral("-e"), QStringLiteral("bash"),
                                    QStringLiteral("-c"), command});
}

bool setBackgroundRandomizer(bool enable)
{
    const QString action = enable ? QStringLiteral("enable") : QStringLiteral("disable");
    return systemctlInXterm(QStringLiteral(
        "sudo systemctl %1 --now rEFInd_bg_randomizer.service && "
        "sudo systemctl status rEFInd_bg_randomizer.service; exec bash").arg(action));
}

bool setThemeRandomizer(bool enable)
{
    const QString action = enable ? QStringLiteral("enable") : QStringLiteral("disable");
    return systemctlInXterm(QStringLiteral(
        "sudo systemctl %1 --now rEFInd_theme_randomizer.service && "
        "sudo systemctl status rEFInd_theme_randomizer.service; exec bash").arg(action));
}

bool setBootnextService(bool enable)
{
    if (enable) {
        return systemctlInXterm(QStringLiteral(
            "sudo systemctl enable --now bootnext-refind.service && "
            "sudo systemctl status bootnext-refind.service; exec bash"));
    }
    // efibootmgr -N fails when BootNext is not set (nothing to delete) --
    // normal whenever the unit was already disabled or had been failing --
    // so it must not short-circuit the status display.
    return systemctlInXterm(QStringLiteral(
        "sudo systemctl disable --now bootnext-refind.service && "
        "{ sudo efibootmgr -N || echo 'BootNext was not set; nothing to clear.'; } && "
        "sudo systemctl status bootnext-refind.service; exec bash"));
}

int runEspDeepScan()
{
    // Blocking, unlike the other script launchers: the caller re-runs detection
    // as soon as this returns, so it has to wait for the cache to be written.
    // The script owns the password prompt and the result dialogs.
    return QProcess::execute(QStringLiteral("bash"),
                             {dataDir() + QStringLiteral("/scripts/scan_esp.sh")});
}

bool espDeepScanUseful()
{
    // Only worth offering when an ESP really is unreadable. Mirrors the check
    // in OSDetector::espRootUnreadable() without pulling detection in here.
    const QStringList mounts = {QStringLiteral("/esp"), QStringLiteral("/boot/efi"),
                                QStringLiteral("/efi"), QStringLiteral("/boot")};
    // Establish any systemd ESP automounts first (SteamOS mounts /esp and
    // /efi that way): stat of "<m>/." resolves through the automount point
    // and triggers the mount, where a plain stat of m does not
    // (AT_NO_AUTOMOUNT). Without this, right after boot the ESP isn't
    // mounted yet and reads as absent, wrongly disabling the button.
    struct stat sb;
    for (const QString &m : mounts)
        (void)::stat(QString(m + QStringLiteral("/.")).toLocal8Bit().constData(), &sb);
    for (const QString &m : mounts) {
        const QFileInfo info(m);
        if (info.exists() && !info.isReadable())
            return true;
    }
    return false;
}

bool firmwareBootnumSupported()
{
    return true;
}

bool preferWindowsAsDefault()
{
    return false; // on the Deck, SteamOS leads
}

QStringList installSourceOptions()
{
    return {QStringLiteral("Pacman"), QStringLiteral("Sourceforge")};
}

#endif

} // namespace Platform
