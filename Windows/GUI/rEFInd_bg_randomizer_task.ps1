#Requires -RunAsAdministrator
# Enables/disables the boot-background randomizer as a Windows Scheduled Task
# (the counterpart of rEFInd_bg_randomizer.service on Linux).
param(
    [switch]$Enable,
    [switch]$Disable
)
$ErrorActionPreference = 'Stop'

$taskName = 'rEFInd_bg_randomizer'
$dataDir = Join-Path $env:LOCALAPPDATA 'SteamDeck_rEFInd'
$script = Join-Path $dataDir 'windows\rEFInd_bg_randomizer.ps1'
$logFile = Join-Path $dataDir 'rEFInd_bg_randomizer.log'

if ($Enable) {
    if (-not (Test-Path $script)) {
        throw "Randomizer script not found: $script"
    }
    $action = New-ScheduledTaskAction -Execute 'powershell.exe' `
        -Argument "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$script`""
    $trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
    $principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -RunLevel Highest -LogonType Interactive
    # Without explicit settings a scheduled task refuses to start on battery
    # power, which on a handheld means it would almost never run at logon.
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
        -StartWhenAvailable -MultipleInstances IgnoreNew -ExecutionTimeLimit (New-TimeSpan -Minutes 5)
    Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger `
        -Principal $principal -Settings $settings -Force | Out-Null
    Write-Host 'Background randomizer task enabled (runs at each logon). Running it once now...'
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
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
    Write-Host 'Background randomizer task disabled.'
} else {
    Write-Host 'Usage: rEFInd_bg_randomizer_task.ps1 -Enable | -Disable'
}
