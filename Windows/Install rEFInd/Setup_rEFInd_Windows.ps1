# Setting next boot as rEFInd... Windows PowerShell script (Run as Administrator) for task scheduler
# Credit goes to Reddit user lucidludic for the idea and some code snippets (modified)

$REFIND_IDENT = bcdedit /enum FIRMWARE | Select-String -Pattern 'refind_x64.efi' -Context 2 | findstr "{"
$REFIND_GUID = ($REFIND_IDENT | Select-String "{.*}").Matches.Value

bcdedit /set "{fwbootmgr}" bootsequence "$REFIND_GUID" /addfirst

# Setting this as a Scheduled task to occur at logon

$trigger = New-ScheduledTaskTrigger -AtLogOn
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries

Write-Host -NoNewline "- Setting Boot Priority on logon: "
$action = New-ScheduledTaskAction -Execute "C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe" -Argument "-executionpolicy bypass -file C:\rEFInd_Scripts\Setup_rEFInd_Windows.ps1"

$description = "Modify BCDEdit so rEFInd priority stays on top"
Register-ScheduledTask -TaskName "rEFInd Boot Sequence" -Action $action -Trigger $trigger -RunLevel Highest -Description $description -Settings $settings >> $null -Force

# Graphical boot glitch fix
bcdedit /set "{globalsettings}" highestmode on

# Set UTC Timezone - Dual-Boot time fix
reg add "HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\TimeZoneInformation" /v RealTimeIsUniversal /d 1 /t REG_DWORD /f