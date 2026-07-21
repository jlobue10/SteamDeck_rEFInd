#include "platform.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

#ifdef Q_OS_WIN
#include <qt_windows.h> // CREATE_NO_WINDOW
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

static bool runScriptInWindow(const QString &scriptPath, const QStringList &scriptArgs = {})
{
    QStringList args = {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                        QStringLiteral("Bypass"), QStringLiteral("-NoExit"),
                        QStringLiteral("-File"), QDir::toNativeSeparators(scriptPath)};
    args += scriptArgs;
    return QProcess::startDetached(QStringLiteral("powershell.exe"), args);
}

bool runInstallerScript(const QString &installSource)
{
    Q_UNUSED(installSource); // only the SourceForge download exists on Windows
    return runScriptInWindow(dataDir() + "/windows/install_rEFInd.ps1");
}

int installConfig(QString *output)
{
    // Synchronous, window-less run with the output captured: the script's
    // console used to flash open and vanish, leaving no trace of whether the
    // install worked. The caller shows the result dialog from *output.
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
    proc.start(QStringLiteral("powershell.exe"),
               {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                QStringLiteral("Bypass"), QStringLiteral("-File"),
                QDir::toNativeSeparators(dataDir() + "/windows/install_config_from_GUI.ps1")});
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

bool installConfigShowsOwnDialogs()
{
    return false;
}

bool installConfigScriptTrusted(QString *detail)
{
    // The .ps1 scripts are Authenticode-signed at release time, and signing
    // rewrites the file, so a hash embedded at build time could never match.
    if (detail)
        detail->clear();
    return true;
}

bool setBackgroundRandomizer(bool enable)
{
    return runScriptInWindow(dataDir() + "/windows/rEFInd_bg_randomizer_task.ps1",
                             {enable ? QStringLiteral("-Enable") : QStringLiteral("-Disable")});
}

bool setBootnextService(bool)
{
    return false; // no systemd on Windows; the Sysd buttons are disabled there
}

bool systemdFeaturesAvailable()
{
    return false;
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

bool runInstallerScript(const QString &installSource)
{
    const QString script = dataDir()
        + (installSource == QLatin1String("Sourceforge")
               ? QStringLiteral("/scripts/sourceforge_install.sh")
               : QStringLiteral("/scripts/pacman_install.sh"));
    return QProcess::startDetached(QStringLiteral("xterm"), {QStringLiteral("-e"), script});
}

static const char kRootConfigScript[] = "/etc/SteamDeck_rEFInd/install_config_from_GUI.sh";

// True when install-GUI.sh has set up the passwordless path: the root-owned
// script exists and the sudoers rule in /etc/sudoers.d lets this user run it
// without a password (`sudo -n -l <cmd>` exits 0 exactly then, without ever
// prompting).
static bool passwordlessConfigInstallReady()
{
    if (!QFile::exists(QLatin1String(kRootConfigScript)))
        return false;
    QProcess proc;
    proc.start(QStringLiteral("sudo"),
               {QStringLiteral("-n"), QStringLiteral("-l"), QLatin1String(kRootConfigScript)});
    if (!proc.waitForStarted())
        return false;
    proc.waitForFinished(-1);
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

int installConfig(QString *output)
{
    if (passwordlessConfigInstallReady()) {
        // Allowed without a password by the sudoers rule; -n keeps the GUI
        // from hanging on a prompt if the rule vanished since the check. Run
        // synchronously with the output captured — the caller presents the
        // result dialog from *output, exactly like the Windows build.
        QProcess proc;
        proc.setProcessChannelMode(QProcess::MergedChannels);
        proc.start(QStringLiteral("sudo"),
                   {QStringLiteral("-n"), QLatin1String(kRootConfigScript)});
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
    // Fallback when the rule isn't installed (the GUI installer was never
    // re-run on this system): the staged script handles its own privilege
    // (zenity password prompt) and shows its own success/error dialogs, so
    // launch it detached.
    if (output)
        output->clear();
    const bool ok = QProcess::startDetached(QStringLiteral("bash"),
                                            {dataDir() + "/scripts/install_config_from_GUI.sh"});
    return ok ? 0 : -1;
}

bool installConfigShowsOwnDialogs()
{
    // Only the zenity fallback owns its dialogs; on the passwordless path the
    // GUI shows the captured output itself.
    return !passwordlessConfigInstallReady();
}

static bool matchesShippedScript(const QString &diskPath, const QString &resourcePath)
{
    QFile ref(resourcePath);
    QFile onDisk(diskPath);
    if (!ref.open(QIODevice::ReadOnly) || !onDisk.open(QIODevice::ReadOnly))
        return false;
    return QCryptographicHash::hash(onDisk.readAll(), QCryptographicHash::Sha256)
        == QCryptographicHash::hash(ref.readAll(), QCryptographicHash::Sha256);
}

bool installConfigScriptTrusted(QString *detail)
{
    if (passwordlessConfigInstallReady()) {
        // The /etc copy is root-owned, so nobody unprivileged can have edited
        // it — this catches a stale copy from another GUI version before it
        // runs with root privileges. It carries no install-time placeholders
        // (the invoking user is resolved from SUDO_USER at runtime), so the
        // comparison is a straight byte-for-byte hash against the embedded
        // reference.
        if (!matchesShippedScript(QLatin1String(kRootConfigScript),
                                  QStringLiteral(":/install_config_from_GUI_root.sh"))) {
            if (detail)
                *detail = QLatin1String(kRootConfigScript);
            return false;
        }
        if (detail)
            detail->clear();
        return true;
    }
    // Zenity fallback path: install_config_from_GUI.sh pipes the user's sudo
    // password into a root shell and sources lib_esp_target.sh into it, so
    // refuse to launch unless both hash identically to the copies this build
    // shipped (embedded as Qt resources at build time). Neither file has
    // install-time placeholders, so the comparison is a straight
    // byte-for-byte hash.
    const QString scripts[] = {
        QStringLiteral("install_config_from_GUI.sh"),
        QStringLiteral("lib_esp_target.sh"),
    };
    for (const QString &name : scripts) {
        const QString diskPath = dataDir() + "/scripts/" + name;
        if (!matchesShippedScript(diskPath, QStringLiteral(":/") + name)) {
            if (detail)
                *detail = diskPath;
            return false;
        }
    }
    if (detail)
        detail->clear();
    return true;
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

bool setBootnextService(bool enable)
{
    if (enable) {
        return systemctlInXterm(QStringLiteral(
            "sudo systemctl enable --now bootnext-refind.service && "
            "sudo systemctl status bootnext-refind.service; exec bash"));
    }
    return systemctlInXterm(QStringLiteral(
        "sudo systemctl disable --now bootnext-refind.service && sudo efibootmgr -N && "
        "sudo systemctl status bootnext-refind.service; exec bash"));
}

bool systemdFeaturesAvailable()
{
    return true;
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
