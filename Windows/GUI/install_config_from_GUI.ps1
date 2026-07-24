#Requires -RunAsAdministrator
# Installs the GUI-generated refind.conf and PNGs onto the EFI System Partition
# that firmware actually launches rEFInd from. The authoritative source is the
# firmware boot entry whose path is \EFI\refind\refind_x64.efi (created either
# by this GUI's installer repointing {bootmgr}, or by a Linux-side
# refind-install as a separate NVRAM entry pointing at the Linux ESP) -- the
# Windows analog of the Linux script reading efibootmgr -v. Only when no such
# entry exists do we fall back to scanning ESPs for a rEFInd install; a stale
# EFI\refind left on one ESP must not shadow the one firmware really boots.
$ErrorActionPreference = 'Stop'

$EspGuid = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
$RefindLoader = 'EFI\refind\refind_x64.efi'
. (Join-Path $PSScriptRoot 'uefi_refind.ps1')

# mountvol reports failure on stderr, which Windows PowerShell 5.1 turns into a
# terminating RemoteException when redirected under ErrorActionPreference Stop;
# run it with the preference relaxed so a failed mount stays a plain exit code.
function Invoke-Mountvol([string[]]$mvArgs) {
    $eap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { mountvol @mvArgs 2>$null | Out-Null } finally { $ErrorActionPreference = $eap }
    return $LASTEXITCODE
}

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
                if ((Invoke-Mountvol @("${c}:", '/S')) -eq 0) {
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
        'mountvol' { $null = Invoke-Mountvol @($m.Root, '/D') }
        'accesspath' {
            Remove-PartitionAccessPath -DiskNumber $m.DiskNumber -PartitionNumber $m.PartitionNumber `
                -AccessPath $m.Dir -ErrorAction SilentlyContinue
            Remove-Item -Force -ErrorAction SilentlyContinue $m.Dir
        }
    }
}

$esps = @(Get-Partition | Where-Object { $_.GptType -eq $EspGuid })
$system = $esps | Where-Object { $_.IsSystem } | Select-Object -First 1

# First choice: the ESP the firmware's rEFInd boot entry points at.
$mount = $null
$nvramGuid = Get-RefindBootPartitionGuid
$nvramPart = $esps | Where-Object {
    $nvramGuid -and ([guid]$_.Guid -eq $nvramGuid)
} | Select-Object -First 1
if ($nvramPart) {
    try {
        $m = Mount-EspPartition $nvramPart
        if (Test-Path (Join-Path $m.Root $RefindLoader)) {
            Write-Host "Using the ESP from the firmware rEFInd boot entry (disk $($nvramPart.DiskNumber), partition $($nvramPart.PartitionNumber))."
            $mount = $m
        } else {
            # Stale NVRAM entry; fall through to the filesystem scan.
            Dismount-Esp $m
        }
    } catch {}
}

# No usable firmware entry: scan for an ESP that contains rEFInd, system ESP
# first (that is where the Windows installer places it), then any others for
# multi-ESP setups; fall back to the system ESP (a fresh-install location).
if (-not $mount) {
    $ordered = @()
    if ($system) { $ordered += $system }
    $ordered += ($esps | Where-Object { -not $_.IsSystem })
    foreach ($p in $ordered) {
        try { $m = Mount-EspPartition $p } catch { continue }
        $found = Test-Path (Join-Path $m.Root $RefindLoader)
        if ($found) { $mount = $m; break }
        Dismount-Esp $m
    }
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
    # Keep one rollback copy of the live config before overwriting it.
    $liveConf = Join-Path $dest 'refind.conf'
    if (Test-Path $liveConf) {
        Copy-Item -Force $liveConf (Join-Path $dest 'refind.conf.prev')
    }
    $copied = 0
    foreach ($f in 'refind.conf','background.png','os_icon1.png','os_icon2.png','os_icon3.png','os_icon4.png') {
        $p = Join-Path $src $f
        if (Test-Path $p) {
            Copy-Item -Force $p (Join-Path $dest $f)
            Write-Host "Installed $f"
            $copied++
        }
    }
    if ($copied -eq 0) {
        throw "No config files were found in $src. Use Create Config in the GUI first."
    }
    # A temp access-path mount's directory name means nothing to the user, so
    # describe that ESP by disk/partition instead.
    $where = if ($mount.Kind -eq 'accesspath') {
        "EFI\refind on disk $($mount.DiskNumber), partition $($mount.PartitionNumber)"
    } else {
        $dest
    }
    Write-Host "Config installed to $where"
} finally {
    Dismount-Esp $mount
}
