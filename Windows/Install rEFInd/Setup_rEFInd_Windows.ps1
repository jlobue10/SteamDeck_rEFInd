# Setting next boot as rEFInd... Windows PowerShell script (Run as Administrator) for task scheduler
# Credit goes to Reddit user lucidludic for the idea and some code snippets (modified)

# Resolve native tools by absolute System32 path, never by PATH lookup: this
# script runs elevated (and again elevated at every logon via the scheduled
# task below), so a bcdedit.exe/findstr.exe/reg.exe planted in a user-writable
# directory earlier on the machine PATH would otherwise run with our privileges.
$System32 = [Environment]::SystemDirectory
$Bcdedit  = Join-Path $System32 'bcdedit.exe'
$Findstr  = Join-Path $System32 'findstr.exe'
$Reg      = Join-Path $System32 'reg.exe'
$PowerShellExe = Join-Path $System32 'WindowsPowerShell\v1.0\powershell.exe'

$REFIND_IDENT = & $Bcdedit /enum FIRMWARE | Select-String -Pattern 'refind_x64.efi' -Context 2 | & $Findstr "{"
$REFIND_GUID = ($REFIND_IDENT | Select-String "{.*}").Matches.Value

& $Bcdedit /set "{fwbootmgr}" bootsequence "$REFIND_GUID" /addfirst

# Setting this as a Scheduled task to occur at logon

$trigger = New-ScheduledTaskTrigger -AtLogOn
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries

Write-Host -NoNewline "- Setting Boot Priority on logon: "
$action = New-ScheduledTaskAction -Execute $PowerShellExe -Argument "-executionpolicy bypass -file C:\rEFInd_Scripts\Setup_rEFInd_Windows.ps1"

$description = "Modify BCDEdit so rEFInd priority stays on top"
Register-ScheduledTask -TaskName "rEFInd Boot Sequence" -Action $action -Trigger $trigger -RunLevel Highest -Description $description -Settings $settings >> $null -Force

# Graphical boot glitch fix
& $Bcdedit /set "{globalsettings}" highestmode on

# Set UTC Timezone - Dual-Boot time fix
& $Reg add "HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\TimeZoneInformation" /v RealTimeIsUniversal /d 1 /t REG_DWORD /f
