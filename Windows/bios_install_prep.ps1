# Run this to remove the rEFInd EFI entry as boot next EFI entry (bootsequence first)
# Be sure to also disable the scheduled task enabling rEFInd as first in the bootsequence

$BCD_INFO = $(bcdedit /enum FIRMWARE)
$FILE_IDX = ($BCD_INFO | Select-String 'refind_x64.efi').LineNumber
$SEPARATORS = ($BCD_INFO | Select-String '--' ).LineNumber
foreach ($_ in $SEPARATORS) { if ($_ -lt $FILE_IDX) { $GUID_IDX = $_ } }
$REFIND_GUID = ($BCD_INFO | Select-Object -Index $GUID_IDX | Select-String "{.*}").Matches.Value

bcdedit /set "{fwbootmgr}" bootsequence "$REFIND_GUID" /remove
