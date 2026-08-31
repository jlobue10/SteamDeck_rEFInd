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

# Remove Scheduled Task
Unregister-ScheduledTask -TaskName "rEFInd Boot Sequence" -Confirm:$false

# Clean up scripts. Refuse to recurse into a junction/symlink: if C:\rEFInd_Scripts
# is a reparse point (which a non-admin could have planted), Remove-Item -Recurse
# would delete the target's contents with our elevation. Delete only a real dir.
$scriptDir = "C:\rEFInd_Scripts"
if (Test-Path -LiteralPath $scriptDir) {
    $item = Get-Item -LiteralPath $scriptDir -Force
    if ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
        Write-Warning "$scriptDir is a junction/symlink; not deleting its target. Remove it manually after verifying where it points."
    } else {
        Remove-Item -LiteralPath $scriptDir -Recurse -Force
    }
}
