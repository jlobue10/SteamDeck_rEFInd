#Requires -RunAsAdministrator
# Installs the staged themes tree (%LOCALAPPDATA%\SteamDeck_rEFInd\themes,
# ~12 MB of theme.conf + PNG assets) onto the EFI System Partition that
# firmware actually launches rEFInd from, as EFI\refind\themes\ -- the themes
# counterpart of install_config_from_GUI.ps1. ESP resolution is identical to
# that script: the firmware rEFInd boot entry first (via the shared
# uefi_refind.ps1 helper), then a filesystem scan, then the system ESP.
#
# Copy strategy: the whole tree is first copied into a private staging
# directory on the ESP itself (same volume), then each theme directory is
# swapped into place with directory renames. rEFInd only reads the tree at
# the next boot, so per-theme-directory replacement is atomic enough, and a
# failure before the swap loop leaves the live tree completely untouched.
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

$staging = $null
try {
    $src = Join-Path $env:LOCALAPPDATA 'SteamDeck_rEFInd\themes'
    $dest = Join-Path $mount.Root 'EFI\refind\themes'
    New-Item -ItemType Directory -Force $dest | Out-Null

    # A theme is a first-level directory holding a non-empty theme.conf.
    $themes = @()
    if (Test-Path -LiteralPath $src -PathType Container) {
        $themes = @(Get-ChildItem -LiteralPath $src -Directory | Where-Object {
            $conf = Join-Path $_.FullName 'theme.conf'
            (Test-Path -LiteralPath $conf -PathType Leaf) -and
                ((Get-Item -LiteralPath $conf).Length -gt 0)
        })
    }
    if ($themes.Count -eq 0) {
        throw "No themes were found in $src. Reinstall the GUI to restore the shipped themes."
    }

    # Free-space check before anything is written: source size plus headroom
    # for the staging copy that briefly coexists with the live tree.
    $neededBytes = (Get-ChildItem -LiteralPath $src -Recurse -File |
        Measure-Object -Property Length -Sum).Sum
    if (-not $neededBytes) { $neededBytes = 16MB }
    # Drive-letter mounts expose free space via their PSDrive; an access-path
    # mount does not, so query the partition's volume object instead.
    $freeBytes = $null
    if ($mount.Root -match '^[A-Za-z]:$') {
        $freeBytes = (Get-PSDrive -Name $mount.Root.Substring(0,1) -ErrorAction SilentlyContinue).Free
    } elseif ($null -ne $mount.DiskNumber) {
        $vol = Get-Partition -DiskNumber $mount.DiskNumber -PartitionNumber $mount.PartitionNumber -ErrorAction SilentlyContinue |
            Get-Volume -ErrorAction SilentlyContinue
        if ($vol) { $freeBytes = $vol.SizeRemaining }
    }
    if ($null -eq $freeBytes) { $freeBytes = [long]::MaxValue } # could not measure; proceed
    if ($freeBytes -lt ($neededBytes + 4MB)) {
        throw ("Not enough free space on the EFI System Partition: need about " +
               [math]::Ceiling(($neededBytes + 4MB) / 1MB) + " MB. Nothing was changed.")
    }

    # Best-effort sweep of staging/backup directories an earlier interrupted
    # run left behind. Only the exact shapes created below are matched.
    Get-ChildItem -LiteralPath $dest -Directory -Filter '.install-staging-*' -ErrorAction SilentlyContinue |
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    Get-ChildItem -LiteralPath $dest -Directory -Filter '.old-*' -ErrorAction SilentlyContinue |
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue

    $staging = Join-Path $dest ('.install-staging-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force $staging | Out-Null

    # Full staging copy first: the live tree is untouched until every byte has
    # landed on the ESP.
    foreach ($t in $themes) {
        Copy-Item -Recurse -LiteralPath $t.FullName -Destination (Join-Path $staging $t.Name)
    }

    # Swap each staged theme into place by directory renames on the same
    # volume: current -> .old-<name>, staged -> live, drop the .old copy. A
    # failure mid-swap leaves every theme either fully old or fully new.
    $installed = 0
    foreach ($t in $themes) {
        $live = Join-Path $dest $t.Name
        $old = Join-Path $dest ('.old-' + $t.Name)
        if (Test-Path -LiteralPath $old) {
            Remove-Item -Recurse -Force -LiteralPath $old
        }
        if (Test-Path -LiteralPath $live) {
            Move-Item -LiteralPath $live -Destination $old
        }
        try {
            Move-Item -LiteralPath (Join-Path $staging $t.Name) -Destination $live
        } catch {
            # Put the previous version back so the theme is not lost entirely.
            if (Test-Path -LiteralPath $old) {
                Move-Item -LiteralPath $old -Destination $live -ErrorAction SilentlyContinue
            }
            throw "Failed while publishing the theme '$($t.Name)'."
        }
        if (Test-Path -LiteralPath $old) {
            Remove-Item -Recurse -Force -LiteralPath $old -ErrorAction SilentlyContinue
        }
        Write-Host "Installed theme $($t.Name)"
        $installed++
    }

    # A temp access-path mount's directory name means nothing to the user, so
    # describe that ESP by disk/partition instead.
    $where = if ($mount.Kind -eq 'accesspath') {
        "EFI\refind\themes on disk $($mount.DiskNumber), partition $($mount.PartitionNumber)"
    } else {
        $dest
    }
    Write-Host "$installed theme(s) installed to $where"
} finally {
    if ($staging -and (Test-Path -LiteralPath $staging)) {
        Remove-Item -Recurse -Force -LiteralPath $staging -ErrorAction SilentlyContinue
    }
    Dismount-Esp $mount
}
