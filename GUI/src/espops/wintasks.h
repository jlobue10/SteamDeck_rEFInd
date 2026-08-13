// espops — see loadoption.h for the parity rules (byte-identical between
// rEFInd_GUI and SteamDeck_rEFInd; repo specifics live in espconstants.h).
//
// Windows Scheduled-Task plumbing (NATIVE_HELPER_DESIGN.md §2.7): native
// Task Scheduler COM registration replacing the *_task.ps1 wrappers, and
// the bootnext payload replacing bootnext_refind_task.ps1's script path.
// COM (not schtasks.exe) because the handheld-critical battery settings
// (AllowStartIfOnBatteries etc.) cannot be expressed on the schtasks
// command line. Windows-only; the header is includable everywhere.

#ifndef ESPOPS_WINTASKS_H
#define ESPOPS_WINTASKS_H

#include <QString>
#include <QStringList>

namespace EspOps {

#ifdef Q_OS_WIN

// Register (enable) or delete (disable) an at-logon Scheduled Task named
// `taskName` running `exePath subcommand` elevated in the logged-on user's
// session, with the settings the .ps1 wrappers used: RunLevel Highest,
// interactive logon, start-on-battery allowed, StartWhenAvailable,
// IgnoreNew instances, 5-minute execution limit. Enabling also runs the
// task once (the wrappers did the same so the effect is immediate).
// Deleting a task that does not exist counts as success.
bool setLogonTask(const QString &taskName, const QString &exePath,
                  const QString &subcommand, bool enable);

// The bootnext payload: set NVRAM BootNext to the firmware's rEFInd entry
// (found by loader path first, exact "rEFInd" label second — the same walk
// as ESP resolution). Cosmetic at-logon semantics: "no rEFInd entry" warns
// and returns 0; nonzero is reserved for internal errors. Never modifies
// any Boot#### entry — BootNext only.
int bootNextToRefind(QStringList *warnings);

#endif // Q_OS_WIN

} // namespace EspOps

#endif // ESPOPS_WINTASKS_H
