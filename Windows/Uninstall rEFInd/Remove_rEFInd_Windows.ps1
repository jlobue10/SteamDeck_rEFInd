# Run this to remove the rEFInd EFI entry as boot next EFI entry (bootsequence first)
# Be sure to also disable the scheduled task enabling rEFInd as first in the bootsequence

$REFIND_IDENT = bcdedit /enum FIRMWARE | Select-String -Pattern 'refind_x64.efi' -Context 2 | findstr "{"
$REFIND_GUID = ($REFIND_IDENT | Select-String "{.*}").Matches.Value

bcdedit /set "{fwbootmgr}" bootsequence "$REFIND_GUID" /remove

# Remove Scheduled Task
Unregister-ScheduledTask -TaskName "rEFInd Boot Sequence" -Confirm:$false

# Clean up scripts
Remove-Item -Path "C:\rEFInd_Scripts" -Recurse -Force
