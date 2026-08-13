// The privileged helper binary (see NATIVE_HELPER_DESIGN.md §2.2).
//
// Parity-locked between rEFInd_GUI and SteamDeck_rEFInd: keep this file
// byte-identical between the repos; repo-specific values come from
// espconstants.h.
//
// Linux: installed root-owned under /etc (kHelperEtcPath); the GUI invokes
// `sudo -n <helper> install-config|install-themes` (exact-argument NOPASSWD
// sudoers lines) and the systemd randomizer units ExecStart the randomize-*
// subcommands. Windows: ships beside the GUI under Program Files; the GUI
// runs espops in-process (it is already elevated) and only the Scheduled
// Tasks invoke this binary.
//
// Exit-code contract (the GUI keys its dialogs off these; matches the
// scripts this binary replaces): 0 success, 2 no/invalid invoking user,
// 3 no rEFInd ESP, 4 read-only target, 5 staging/space failure, 6 no source
// config / no themes. randomize-* subcommands exit 0 on content/environment
// problems (cosmetic boot-time services must not produce red failed units);
// nonzero from them means an internal error. 64 is reserved for usage
// errors and for subcommands not yet ported in a development build.
//
// Everything printed here is captured by the GUI and shown in its result
// dialog — keep the output short and human-readable (English on purpose,
// status quo of the scripts; see NATIVE_HELPER_DESIGN.md §5 on i18n).

#include "espops/configinstall.h"
#include "espops/espconstants.h"
#include "espops/espresolve.h"
#include "espops/userio.h"

#include <QCoreApplication>

#include <cstdio>
#include <cstring>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

#ifndef ESPOPS_APP_VERSION
#error "ESPOPS_APP_VERSION must be defined by the build (from project VERSION)"
#endif

namespace {

int usage(FILE *to)
{
    std::fprintf(to,
                 "%s helper %s\n"
                 "usage: helper <subcommand>\n"
                 "  install-config        install the generated refind.conf set onto the ESP\n"
                 "  install-themes        install the staged theme trees onto the ESP\n"
                 "  randomize-background  pick a random boot background (boot/logon service)\n"
                 "  randomize-theme       pick a random installed theme (boot/logon service)\n"
                 "  --version             print the helper version and exit\n",
                 EspOps::kProductName, ESPOPS_APP_VERSION);
    return 64;
}

int notPorted(const char *sub)
{
    // Development-build placeholder: the subcommand surface and version
    // handshake exist before all ported logic does, so a mismatched GUI can
    // already give a precise "reinstall" diagnosis instead of a generic
    // failure.
    std::fprintf(stderr,
                 "%s: the '%s' subcommand is not implemented in this development build.\n",
                 EspOps::kProductName, sub);
    return 64;
}

void printLines(const QStringList &lines)
{
    for (const QString &line : lines)
        std::printf("%s\n", line.toLocal8Bit().constData());
}

#ifdef Q_OS_UNIX

int runInstallConfig()
{
    // The config to install lives in the invoking user's home (sudo sets
    // SUDO_USER; the sudoers rule only exists for regular users).
    if (geteuid() != 0) {
        std::printf("This helper must run as root (the %s GUI launches it via sudo).\n",
                    EspOps::kProductName);
        return 2;
    }
    const EspOps::SudoUser user = EspOps::sudoUserFromEnvironment();
    if (!user.valid) {
        std::printf("This helper is meant to be launched by the %s GUI via sudo.\n",
                    EspOps::kProductName);
        return 2;
    }

    EspOps::EspResolver resolver;
    EspOps::RefindTarget target;
    if (!resolver.resolve(&target)) {
        std::printf("No EFI System Partition with rEFInd on it could be found, "
                    "and no system ESP is mounted.\n"
                    "Install rEFInd first, then install the config.\n");
        return 3;
    }
    resolver.makeWritable(target.refindDir);

    EspOps::ForkUserFiles files(user);
    const QString srcDir = user.home + QLatin1String("/.local/")
        + QLatin1String(EspOps::kDataDirName) + QLatin1String("/GUI");
    const EspOps::InstallOutcome outcome =
        EspOps::installConfigSet(files, srcDir, target.refindDir);

    printLines(outcome.lines);
    if (outcome.exitCode == 0) {
        std::printf("(chosen as %s)\n", target.how.toLocal8Bit().constData());
        // Flush to the ESP before the resolver's temporary mounts go away.
        ::sync();
    }
    return outcome.exitCode;
}

#endif // Q_OS_UNIX

} // namespace

int main(int argc, char *argv[])
{
    if (argc < 2)
        return usage(stderr);

    const char *sub = argv[1];

    if (std::strcmp(sub, "--version") == 0 || std::strcmp(sub, "version") == 0) {
        // The GUI's version handshake (NATIVE_HELPER_DESIGN.md §2.3) parses
        // exactly this single line; keep the format stable.
        std::printf("%s\n", ESPOPS_APP_VERSION);
        return 0;
    }
    if (std::strcmp(sub, "--help") == 0 || std::strcmp(sub, "help") == 0)
        return usage(stdout), 0;

    if (argc != 2)
        return usage(stderr);

    // QProcess (used by the ESP resolver for mount/lsblk) wants an
    // application object; keep it below the trivial subcommands above.
    QCoreApplication app(argc, argv);

    if (std::strcmp(sub, "install-config") == 0) {
#ifdef Q_OS_UNIX
        return runInstallConfig();
#else
        // Windows never routes install-config through the helper: the GUI
        // is already elevated and runs espops in-process.
        return notPorted(sub);
#endif
    }
    if (std::strcmp(sub, "install-themes") == 0)
        return notPorted(sub);
    if (std::strcmp(sub, "randomize-background") == 0)
        return notPorted(sub);
    if (std::strcmp(sub, "randomize-theme") == 0)
        return notPorted(sub);
#ifdef Q_OS_WIN
    if (std::strcmp(sub, "bootnext") == 0)
        return notPorted(sub);
#endif

    return usage(stderr);
}
