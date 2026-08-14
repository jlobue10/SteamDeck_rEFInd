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
#include "espops/randomize.h"
#include "espops/themesinstall.h"
#include "espops/userio.h"
#include "espops/wintasks.h"

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
#ifdef Q_OS_WIN
                 "  bootnext              set NVRAM BootNext to the rEFInd entry\n"
#endif
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

// Shared prologue of the two sudo-gated install subcommands: require root
// (the sources to install live in the invoking user's home; sudo sets
// SUDO_USER, and the sudoers rule only exists for regular users), resolve
// the ESP that actually boots rEFInd, and make it writable. `finalNoun` is
// "config" or "themes" for the resolution-failure hint.
int runEspInstall(const char *finalNoun,
                  int (*action)(EspOps::UserFiles &, const EspOps::SudoUser &,
                                const QString &refindDir, QStringList *lines))
{
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
                    "Install rEFInd first, then install the %s.\n",
                    finalNoun);
        return 3;
    }
    resolver.makeWritable(target.refindDir);

    EspOps::ForkUserFiles files(user);
    QStringList lines;
    const int code = action(files, user, target.refindDir, &lines);
    printLines(lines);
    if (code == 0) {
        std::printf("(chosen as %s)\n", target.how.toLocal8Bit().constData());
        // Flush to the ESP before the resolver's temporary mounts go away.
        ::sync();
    }
    return code;
}

int runInstallConfig()
{
    return runEspInstall(
        "config",
        [](EspOps::UserFiles &files, const EspOps::SudoUser &user,
           const QString &refindDir, QStringList *lines) -> int {
            const QString srcDir = user.home + QLatin1String("/.local/")
                + QLatin1String(EspOps::kDataDirName) + QLatin1String("/GUI");
            const EspOps::InstallOutcome o =
                EspOps::installConfigSet(files, srcDir, refindDir);
            *lines = o.lines;
            return o.exitCode;
        });
}

int runInstallThemes()
{
    return runEspInstall(
        "themes",
        [](EspOps::UserFiles &files, const EspOps::SudoUser &user,
           const QString &refindDir, QStringList *lines) -> int {
            const QString srcDir = user.home + QLatin1String("/.local/")
                + QLatin1String(EspOps::kDataDirName) + QLatin1String("/themes");
            const EspOps::InstallOutcome o =
                EspOps::installThemeSet(files, srcDir, refindDir);
            *lines = o.lines;
            return o.exitCode;
        });
}

#endif // Q_OS_UNIX

// Cosmetic boot/logon services (systemd units on Linux, Scheduled Tasks on
// Windows): content and environment problems — including "no ESP found" —
// warn on stderr and exit 0; a red failed unit would be worse than the
// symptom. Nonzero is reserved for internal errors inside the payload.
int runRandomize(int (*payload)(const QString &refindDir, QStringList *))
{
    const char *tag = EspOps::kProductName;
#ifdef Q_OS_UNIX
    if (geteuid() != 0) {
        std::fprintf(stderr, "%s: the randomizer must run as root; nothing to do.\n", tag);
        return 0;
    }
#endif
    EspOps::EspResolver resolver;
    EspOps::RefindTarget target;
    if (!resolver.resolve(&target)) {
        std::fprintf(stderr, "%s: no ESP with rEFInd on it; nothing to update.\n", tag);
        return 0;
    }
    resolver.makeWritable(target.refindDir);
    QStringList warnings;
    const int code = payload(target.refindDir, &warnings);
    for (const QString &w : warnings)
        std::fprintf(stderr, "%s: %s\n", tag, w.toLocal8Bit().constData());
#ifdef Q_OS_UNIX
    // Flush to the ESP before the resolver's temporary mounts go away.
    ::sync();
#endif
    return code;
}

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
    if (std::strcmp(sub, "install-themes") == 0) {
#ifdef Q_OS_UNIX
        return runInstallThemes();
#else
        // Windows never routes install-themes through the helper: the GUI
        // is already elevated and runs espops in-process.
        return notPorted(sub);
#endif
    }
    if (std::strcmp(sub, "randomize-background") == 0)
        return runRandomize(EspOps::randomizeBackground);
    if (std::strcmp(sub, "randomize-theme") == 0)
        return runRandomize(EspOps::randomizeTheme);
#ifdef Q_OS_WIN
    if (std::strcmp(sub, "bootnext") == 0) {
        // At-logon Scheduled Task payload; cosmetic-service semantics.
        QStringList warnings;
        const int code = EspOps::bootNextToRefind(&warnings);
        for (const QString &w : warnings)
            std::fprintf(stderr, "%s: %s\n", EspOps::kProductName,
                         w.toLocal8Bit().constData());
        return code;
    }
#endif

    return usage(stderr);
}
