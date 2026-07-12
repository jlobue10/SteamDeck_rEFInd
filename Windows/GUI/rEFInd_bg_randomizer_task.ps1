#Requires -RunAsAdministrator
# Enables/disables the boot-background randomizer as a Windows Scheduled Task
# (the counterpart of rEFInd_bg_randomizer.service on Linux).
param(
    [switch]$Enable,
    [switch]$Disable
)
$ErrorActionPreference = 'Stop'

$taskName = 'rEFInd_bg_randomizer'
$script = Join-Path $env:LOCALAPPDATA 'SteamDeck_rEFInd\windows\rEFInd_bg_randomizer.ps1'

if ($Enable) {
    if (-not (Test-Path $script)) {
        throw "Randomizer script not found: $script"
    }
    $action = New-ScheduledTaskAction -Execute 'powershell.exe' `
        -Argument "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$script`""
    $trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
    $principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -RunLevel Highest -LogonType Interactive
    Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Principal $principal -Force | Out-Null
    Start-ScheduledTask -TaskName $taskName
    Write-Host "Background randomizer task enabled (runs at logon)."
} elseif ($Disable) {
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
    Write-Host 'Background randomizer task disabled.'
} else {
    Write-Host 'Usage: rEFInd_bg_randomizer_task.ps1 -Enable | -Disable'
}
