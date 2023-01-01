# Setting next boot as rEFInd... Windows PowerShell script for task scheduler
# Credit goes to Reddit user lucidludic for the idea and some code snippets (modified)

$REFIND_IDENT = bcdedit /enum FIRMWARE |findstr "den des"|Select-String rEFInd -Context 1,0|findstr "den"
$REFIND_GUID = ($REFIND_IDENT -split ' ')[-1]
bcdedit /set "{fwbootmgr}" bootsequence "$REFIND_GUID" /addfirst
