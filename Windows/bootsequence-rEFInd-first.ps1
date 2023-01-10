# Setting next boot as rEFInd... Windows PowerShell script for task scheduler
# Credit goes to Reddit user lucidludic for the idea and some code snippets (modified)

$BCD_INFO = $(bcdedit /enum FIRMWARE)
$FILE_IDX = ($BCD_INFO | Select-String 'refind_x64.efi').LineNumber
$SEPARATORS = ($BCD_INFO | Select-String '--' ).LineNumber
foreach ($_ in $SEPARATORS) { if ($_ -lt $FILE_IDX) { $GUID_IDX = $_ } }
$REFIND_GUID = ($BCD_INFO | Select-Object -Index $GUID_IDX | Select-String "{.*}").Matches.Value

bcdedit /set "{fwbootmgr}" bootsequence "$REFIND_GUID" /addfirst
