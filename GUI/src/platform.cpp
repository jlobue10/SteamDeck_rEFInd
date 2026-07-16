#include "platform.h"

#include <QDir>
#include <QProcess>
#include <QProcessEnvironment>

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

int installConfig()
{
    return QProcess::execute(QStringLiteral("powershell.exe"),
                             {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                              QStringLiteral("Bypass"), QStringLiteral("-File"),
                              QDir::toNativeSeparators(dataDir() + "/windows/install_config_from_GUI.ps1")});
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

int installConfig()
{
    // The script handles its own privilege (zenity password prompt) and shows
    // its own success/error dialogs, so launch it detached.
    const bool ok = QProcess::startDetached(QStringLiteral("bash"),
                                            {dataDir() + "/scripts/install_config_from_GUI.sh"});
    return ok ? 0 : -1;
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
