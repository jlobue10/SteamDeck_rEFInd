#Requires -RunAsAdministrator
# Installs the GUI-generated refind.conf and PNGs onto the EFI System Partition
# that firmware actually launches rEFInd from. On Windows the firmware boots the
# Windows Boot Manager ({bootmgr}), which the GUI's installer repoints at
# \EFI\refind\refind_x64.efi on the system ESP; on multi-ESP machines we still
# confirm which ESP actually holds rEFInd rather than assuming the system one.
$ErrorActionPreference = 'Stop'

$EspGuid = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
$RefindLoader = 'EFI\refind\refind_x64.efi'

# Make a specific ESP partition reachable, returning its filesystem root and how
# it was mounted. Handles: an already-lettered ESP, the letterless system ESP
# (mountvol /S), and a letterless non-system ESP (temporary directory access
# path, which does not consume a drive letter).
function Mount-EspPartition($part) {
    if ([char]::IsLetter([char]$part.DriveLetter)) {
        return @{ Root = "$($part.DriveLetter):"; Kind = 'letter' }
    }
    if ($part.IsSystem) {
        $used = (Get-PSDrive -PSProvider FileSystem).Name
        foreach ($c in 'Z','Y','X','W','V','U','T') {
            if ($used -notcontains $c) {
                mountvol "${c}:" /S 2>$null | Out-Null
                if ($LASTEXITCODE -eq 0) {
                    return @{ Root = "${c}:"; Kind = 'mountvol' }
                }
            }
        }
        throw 'Could not mount the system EFI System Partition. Run this script as Administrator.'
    }
    $dir = Join-Path $env:TEMP ('refind-esp-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force $dir | Out-Null
    Add-PartitionAccessPath -DiskNumber $part.DiskNumber -PartitionNumber $part.PartitionNumber -AccessPath $dir
    return @{ Root = $dir; Kind = 'accesspath'; DiskNumber = $part.DiskNumber;
              PartitionNumber = $part.PartitionNumber; Dir = $dir }
}

function Dismount-Esp($m) {
    switch ($m.Kind) {
        'mountvol' { mountvol $m.Root /D | Out-Null }
        'accesspath' {
            Remove-PartitionAccessPath -DiskNumber $m.DiskNumber -PartitionNumber $m.PartitionNumber `
                -AccessPath $m.Dir -ErrorAction SilentlyContinue
            Remove-Item -Force -ErrorAction SilentlyContinue $m.Dir
        }
    }
}

# Candidate ESPs, system ESP first (that is the one firmware boots and where the
# installer places rEFInd), then any others for multi-ESP setups.
$esps = @(Get-Partition | Where-Object { $_.GptType -eq $EspGuid })
$system = $esps | Where-Object { $_.IsSystem } | Select-Object -First 1
$ordered = @()
if ($system) { $ordered += $system }
$ordered += ($esps | Where-Object { -not $_.IsSystem })

# Pick the ESP that actually contains rEFInd; fall back to the system ESP (a
# fresh-install location) if none do.
$mount = $null
foreach ($p in $ordered) {
    try { $m = Mount-EspPartition $p } catch { continue }
    $found = Test-Path (Join-Path $m.Root $RefindLoader)
    if ($found) { $mount = $m; break }
    Dismount-Esp $m
}
if (-not $mount) {
    if (-not $system) {
        throw 'No EFI System Partition found. Run this script as Administrator.'
    }
    $mount = Mount-EspPartition $system
}

try {
    $src = Join-Path $env:LOCALAPPDATA 'SteamDeck_rEFInd\GUI'
    $dest = Join-Path $mount.Root 'EFI\refind'
    New-Item -ItemType Directory -Force $dest | Out-Null
    foreach ($f in 'refind.conf','background.png','os_icon1.png','os_icon2.png','os_icon3.png','os_icon4.png') {
        $p = Join-Path $src $f
        if (Test-Path $p) {
            Copy-Item -Force $p (Join-Path $dest $f)
            Write-Host "Installed $f"
        }
    }
    Write-Host "Config installed to $dest"
} finally {
    Dismount-Esp $mount
}
