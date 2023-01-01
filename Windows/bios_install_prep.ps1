# Run this to remove the rEFInd EFI entry as boot next EFI entry (bootsequence first)
# Be sure to also disable the scheduled task enabling rEFInd as first in the bootsequence

$REFIND_IDENT = bcdedit /enum FIRMWARE |findstr "den des"|Select-String rEFInd -Context 1,0|findstr "den"
$REFIND_GUID = ($REFIND_IDENT -split ' ')[-1]
bcdedit /set "{fwbootmgr}" bootsequence "$REFIND_GUID" /remove
