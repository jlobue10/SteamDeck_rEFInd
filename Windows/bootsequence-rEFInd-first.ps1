# Setting next boot as rEFInd... Windows PowerShell script (Run as Administrator) for task scheduler
# Credit goes to Reddit user lucidludic for the idea and some code snippets (modified)

$REFIND_IDENT = bcdedit /enum FIRMWARE | Select-String -Pattern 'refind_x64.efi' -Context 2 | findstr "{.*}"
$REFIND_GUID = ($REFIND_IDENT -split ' ')[-1]

bcdedit /set "{fwbootmgr}" bootsequence "$REFIND_GUID" /addfirst

# Graphical boot glitch fix
bcdedit /set "{globalsettings}" highestmode on
