# Run this to remove the rEFInd EFI entry as boot next EFI entry (bootsequence first)
# Be sure to also disable the scheduled task enabling rEFInd as first in the bootsequence

# Resolve native tools by absolute System32 path, never by PATH lookup (this
# script runs elevated).
$System32 = [Environment]::SystemDirectory
$Bcdedit  = Join-Path $System32 'bcdedit.exe'
$Findstr  = Join-Path $System32 'findstr.exe'

$REFIND_IDENT = & $Bcdedit /enum FIRMWARE | Select-String -Pattern 'refind_x64.efi' -Context 2 | & $Findstr "{"
$REFIND_GUID = ($REFIND_IDENT | Select-String "{.*}").Matches.Value

& $Bcdedit /set "{fwbootmgr}" bootsequence "$REFIND_GUID" /remove
