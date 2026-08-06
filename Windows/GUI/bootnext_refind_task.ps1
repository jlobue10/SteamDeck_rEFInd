#Requires -RunAsAdministrator
# Keeps rEFInd first on reboot from Windows -- the counterpart of
# bootnext-refind.service on Linux (the GUI's Sysd On/Off buttons run this on
# Windows). A scheduled task runs this script at each logon with -SetBootNext,
# which writes the UEFI BootNext variable to the rEFInd Boot#### entry via the
# locale-independent helpers in uefi_refind.ps1 -- no bcdedit text parsing.
param(
    [switch]$Enable,
    [switch]$Disable,
    [switch]$SetBootNext
)
$ErrorActionPreference = 'Stop'

$taskName = 'SteamDeck_rEFInd_bootnext'
# The pre-GUI "Dual Boot Fix" zip registered this bcdedit-based task; it does
# the same job, so an enable/disable here supersedes it.
$legacyTaskName = 'rEFInd Boot Sequence'
$dataDir = Join-Path $env:LOCALAPPDATA 'SteamDeck_rEFInd'
$scriptPath = Join-Path $PSScriptRoot 'bootnext_refind_task.ps1'
$logFile = Join-Path $dataDir 'bootnext_refind.log'
$powerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'

. (Join-Path $PSScriptRoot 'uefi_refind.ps1')

function Set-RefindBootNext {
    $entry = Get-RefindBootEntry
    if (-not $entry) {
        throw 'No rEFInd firmware boot entry was found; install rEFInd first.'
    }
    $bootnum = [convert]::ToUInt16($entry.Id, 16)
    $bytes = [byte[]]@($bootnum -band 0xFF, ($bootnum -shr 8) -band 0xFF)
    if (-not (Set-RefindFirmwareVariable 'BootNext' $bytes)) {
        throw ("Writing BootNext failed with Win32 error " +
            [Runtime.InteropServices.Marshal]::GetLastWin32Error() + '.')
    }
    Write-Host "BootNext set to rEFInd entry Boot$($entry.Id)."
}

function Clear-RefindBootNext {
    Enable-RefindFirmwarePrivilege
    if (-not (Get-RefindFirmwareVariable 'BootNext')) {
        Write-Host 'BootNext was not set; nothing to clear.'
        return
    }
    if (Set-RefindFirmwareVariable 'BootNext' $null) {
        Write-Host 'Cleared BootNext.'
    } else {
        Write-Host ("Warning: could not clear BootNext (Win32 error " +
            [Runtime.InteropServices.Marshal]::GetLastWin32Error() + ').')
    }
}

function Register-RefindBootnextTask {
    if (-not (Test-Path $scriptPath)) {
        throw "Bootnext script not found: $scriptPath"
    }
    $action = New-ScheduledTaskAction -Execute $powerShell `
        -Argument "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$scriptPath`" -SetBootNext"
    $trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
    $principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -RunLevel Highest -LogonType Interactive
    # Without explicit settings a scheduled task refuses to start on battery
    # power, which on a handheld means it would almost never run at logon.
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
        -StartWhenAvailable -MultipleInstances IgnoreNew -ExecutionTimeLimit (New-TimeSpan -Minutes 5)
    Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger `
        -Principal $principal -Settings $settings -Force | Out-Null
}

if ($SetBootNext) {
    # Scheduled-task payload: log for the -Enable flow's report, since the
    # hidden window leaves no other trace.
    New-Item -ItemType Directory -Force $dataDir | Out-Null
    try {
        Set-RefindBootNext *>&1 | Tee-Object -FilePath $logFile
    } catch {
        $_ | Out-String | Tee-Object -FilePath $logFile | Write-Host
        exit 1
    }
} elseif ($Enable) {
    Register-RefindBootnextTask
    Unregister-ScheduledTask -TaskName $legacyTaskName -Confirm:$false -ErrorAction SilentlyContinue
    Write-Host 'Bootnext task enabled (sets rEFInd as the next boot at each logon). Running it once now...'
    Start-ScheduledTask -TaskName $taskName
    Start-Sleep -Seconds 1
    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-ScheduledTask -TaskName $taskName).State -eq 'Running' -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
    }
    $result = (Get-ScheduledTaskInfo -TaskName $taskName).LastTaskResult
    if ($result -eq 0) {
        Write-Host 'First run finished successfully.'
    } else {
        Write-Host "First run finished with result code $result."
    }
    if (Test-Path $logFile) {
        Write-Host "--- $logFile ---"
        Get-Content $logFile | ForEach-Object { Write-Host $_ }
    }
} elseif ($Disable) {
    foreach ($name in $taskName,$legacyTaskName) {
        Unregister-ScheduledTask -TaskName $name -Confirm:$false -ErrorAction SilentlyContinue
    }
    Write-Host 'Bootnext task disabled.'
    # Mirror the Linux Sysd Off flow's `efibootmgr -N`: a BootNext already
    # written for the next reboot would otherwise still fire once.
    Clear-RefindBootNext
} else {
    Write-Host 'Usage: bootnext_refind_task.ps1 -Enable | -Disable | -SetBootNext'
}
