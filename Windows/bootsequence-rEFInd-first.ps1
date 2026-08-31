# Setting next boot as rEFInd... Windows PowerShell script (Run as Administrator) for task scheduler
# Credit goes to Reddit user lucidludic for the idea and some code snippets (modified)

# Resolve native tools by absolute System32 path, never by PATH lookup (this
# script runs elevated, including as a scheduled task).
$System32 = [Environment]::SystemDirectory
$Bcdedit  = Join-Path $System32 'bcdedit.exe'
$Findstr  = Join-Path $System32 'findstr.exe'

$REFIND_IDENT = & $Bcdedit /enum FIRMWARE | Select-String -Pattern 'refind_x64.efi' -Context 2 | & $Findstr "{"
$REFIND_GUID = ($REFIND_IDENT | Select-String "{.*}").Matches.Value

& $Bcdedit /set "{fwbootmgr}" bootsequence "$REFIND_GUID" /addfirst

# Graphical boot glitch fix
& $Bcdedit /set "{globalsettings}" highestmode on
