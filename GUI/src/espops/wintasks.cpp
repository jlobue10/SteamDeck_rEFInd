// Parity-locked between rEFInd_GUI and SteamDeck_rEFInd — see loadoption.h.
//
// NOTE: authored against the validated *_task.ps1 behavior; compile and
// exercise on a real Windows machine (MSYS2 UCRT64) before shipping.

#include "wintasks.h"

#ifdef Q_OS_WIN

#include "espconstants.h"
#include "loadoption.h"

#include <QDir>
#include <QSet>

#include <qt_windows.h>
#include <taskschd.h>

namespace EspOps {

// Defined in espresolve_win.cpp (shared NVRAM plumbing).
QByteArray winReadEfiVar(const QString &name);
bool winWriteEfiVar(const QString &name, const QByteArray &value);

namespace {

// Minimal RAII so every early return releases what it took.
struct ComScope
{
    HRESULT hr;
    ComScope() { hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
    ~ComScope()
    {
        if (SUCCEEDED(hr))
            CoUninitialize();
    }
};

template <typename T> struct ComPtr
{
    T *p = nullptr;
    ~ComPtr()
    {
        if (p)
            p->Release();
    }
    T **operator&() { return &p; }
    T *operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

struct Bstr
{
    BSTR b;
    explicit Bstr(const QString &s)
        : b(SysAllocString(reinterpret_cast<const wchar_t *>(s.utf16())))
    {
    }
    ~Bstr() { SysFreeString(b); }
    operator BSTR() const { return b; }
};

} // namespace

bool setLogonTask(const QString &taskName, const QString &exePath,
                  const QString &subcommand, bool enable)
{
    ComScope com;
    if (FAILED(com.hr) && com.hr != RPC_E_CHANGED_MODE)
        return false;

    ComPtr<ITaskService> service;
    if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr,
                                CLSCTX_INPROC_SERVER, IID_ITaskService,
                                reinterpret_cast<void **>(&service))))
        return false;
    VARIANT empty;
    VariantInit(&empty);
    if (FAILED(service->Connect(empty, empty, empty, empty)))
        return false;
    ComPtr<ITaskFolder> root;
    {
        Bstr rootPath(QStringLiteral("\\"));
        if (FAILED(service->GetFolder(rootPath, &root)))
            return false;
    }

    Bstr name(taskName);
    if (!enable) {
        const HRESULT hr = root->DeleteTask(name, 0);
        return SUCCEEDED(hr)
            || hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }

    ComPtr<ITaskDefinition> task;
    if (FAILED(service->NewTask(0, &task)))
        return false;

    {
        ComPtr<IPrincipal> principal;
        if (FAILED(task->get_Principal(&principal)))
            return false;
        principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
        principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
    }
    {
        ComPtr<ITaskSettings> settings;
        if (FAILED(task->get_Settings(&settings)))
            return false;
        // Without these a task refuses to start on battery power, which on
        // a handheld means it would almost never run at logon.
        settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
        settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
        settings->put_StartWhenAvailable(VARIANT_TRUE);
        settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);
        Bstr limit(QStringLiteral("PT5M"));
        settings->put_ExecutionTimeLimit(limit);
    }
    {
        ComPtr<ITriggerCollection> triggers;
        if (FAILED(task->get_Triggers(&triggers)))
            return false;
        ComPtr<ITrigger> trigger;
        if (FAILED(triggers->Create(TASK_TRIGGER_LOGON, &trigger)))
            return false;
    }
    {
        ComPtr<IActionCollection> actions;
        if (FAILED(task->get_Actions(&actions)))
            return false;
        ComPtr<IAction> action;
        if (FAILED(actions->Create(TASK_ACTION_EXEC, &action)))
            return false;
        ComPtr<IExecAction> exec;
        if (FAILED(action->QueryInterface(IID_IExecAction,
                                          reinterpret_cast<void **>(&exec))))
            return false;
        Bstr path(QDir::toNativeSeparators(exePath));
        Bstr args(subcommand);
        exec->put_Path(path);
        exec->put_Arguments(args);
    }

    ComPtr<IRegisteredTask> registered;
    {
        VARIANT nobody; // VT_EMPTY serves for userId, password, and sddl
        VariantInit(&nobody);
        if (FAILED(root->RegisterTaskDefinition(
                name, task.p, TASK_CREATE_OR_UPDATE, nobody, nobody,
                TASK_LOGON_INTERACTIVE_TOKEN, nobody, &registered)))
            return false;
    }
    // Run once now so enabling has an immediate effect, like the wrappers'
    // Start-ScheduledTask; a failed immediate run is not a failed enable.
    {
        VARIANT nothing;
        VariantInit(&nothing);
        ComPtr<IRunningTask> running;
        registered->Run(nothing, &running);
    }
    return true;
}

bool logonTaskExists(const QString &taskName)
{
    ComScope com;
    if (FAILED(com.hr) && com.hr != RPC_E_CHANGED_MODE)
        return false;
    ComPtr<ITaskService> service;
    if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr,
                                CLSCTX_INPROC_SERVER, IID_ITaskService,
                                reinterpret_cast<void **>(&service))))
        return false;
    VARIANT empty;
    VariantInit(&empty);
    if (FAILED(service->Connect(empty, empty, empty, empty)))
        return false;
    ComPtr<ITaskFolder> root;
    {
        Bstr rootPath(QStringLiteral("\\"));
        if (FAILED(service->GetFolder(rootPath, &root)))
            return false;
    }
    ComPtr<IRegisteredTask> task;
    Bstr name(taskName);
    return SUCCEEDED(root->GetTask(name, &task)) && task;
}

int migrateLogonTasks(const QString &helperExe, QStringList *warnings)
{
    int migrated = 0;
    const QString product = QString::fromLatin1(kProductName);
    QSet<QString> done; // names the legacy pass already re-pointed

    // Tasks registered by a previous version under an older name: recreate
    // under the current name first, then drop the old one. Names handled
    // here are skipped by the current-name pass below.
    for (const LegacyLogonTask *t = kLegacyLogonTasks; t->oldName; ++t) {
        const QString oldName = QString::fromLatin1(t->oldName);
        if (!logonTaskExists(oldName))
            continue;
        const QString newName = product + QLatin1String(t->newNameSuffix);
        if (!setLogonTask(newName, helperExe,
                          QString::fromLatin1(t->subcommand), true)) {
            if (warnings)
                warnings->append(
                    QStringLiteral("could not re-register '%1' as '%2'; "
                                   "re-enable it in the app")
                        .arg(oldName, newName));
            continue;
        }
        if (!setLogonTask(oldName, helperExe,
                          QString::fromLatin1(t->subcommand), false)
            && warnings) {
            warnings->append(
                QStringLiteral("could not remove the superseded task '%1'")
                    .arg(oldName));
        }
        done.insert(newName);
        ++migrated;
    }

    // Current-name tasks whose action still points at a .ps1 wrapper that no
    // longer ships: CREATE_OR_UPDATE re-points them at the helper.
    for (const LogonTaskSpec *t = kLogonTasks; t->nameSuffix; ++t) {
        const QString name = product + QLatin1String(t->nameSuffix);
        if (done.contains(name) || !logonTaskExists(name))
            continue;
        if (!setLogonTask(name, helperExe, QString::fromLatin1(t->subcommand),
                          true)) {
            if (warnings)
                warnings->append(
                    QStringLiteral("could not re-point '%1' at the helper; "
                                   "re-enable it in the app")
                        .arg(name));
            continue;
        }
        ++migrated;
    }
    return migrated;
}

int bootNextToRefind(QStringList *warnings)
{
    // BootOrder-listed entries first, then the conventional sweep; loader
    // path tier before the exact-label tier — the same walk as ESP
    // resolution, but returning the boot NUMBER.
    QList<quint16> ids = parseBootOrder(winReadEfiVar(QStringLiteral("BootOrder")));
    for (quint16 i = 0; i <= 0xFF; ++i) {
        if (!ids.contains(i))
            ids << i;
    }
    for (int tier = 0; tier < 2; ++tier) {
        for (quint16 id : ids) {
            const QByteArray raw = winReadEfiVar(formatBootNum(id));
            if (raw.isEmpty())
                continue;
            const LoadOption o = parseLoadOption(raw);
            if (!o.valid)
                continue;
            const bool hit = tier == 0
                ? loaderLooksLikeRefind(o.loaderPath)
                : o.description == QLatin1String("rEFInd");
            if (!hit)
                continue;
            QByteArray next(2, 0);
            next[0] = char(id & 0xff);
            next[1] = char(id >> 8);
            if (!winWriteEfiVar(QStringLiteral("BootNext"), next)) {
                if (warnings)
                    warnings->append(QStringLiteral(
                        "could not write BootNext; firmware boot order is "
                        "unchanged"));
                return 1;
            }
            return 0;
        }
    }
    if (warnings)
        warnings->append(QStringLiteral(
            "no rEFInd boot entry found in NVRAM; BootNext not set"));
    return 0;
}

} // namespace EspOps

#endif // Q_OS_WIN
