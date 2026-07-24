#Requires -RunAsAdministrator
# Copies a random background PNG to rEFInd's directory on whichever EFI System
# Partition actually contains rEFInd (on multi-ESP machines that may not be the
# system ESP). Run hidden at logon by the "SteamDeck_rEFInd_bg_randomizer"
# scheduled
# task, so progress and errors are also written to rEFInd_bg_randomizer.log in
# the app data directory.
$ErrorActionPreference = 'Stop'

$EspGuid = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
$RefindLoader = 'EFI\refind\refind_x64.efi'
$DataDir = Join-Path $env:LOCALAPPDATA 'SteamDeck_rEFInd'
$LogFile = Join-Path $DataDir 'rEFInd_bg_randomizer.log'
. (Join-Path $PSScriptRoot 'uefi_refind.ps1')

Set-Content -Path $LogFile -Value @() -ErrorAction SilentlyContinue
function Log($msg) {
    $line = '[{0}] {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $msg
    Write-Host $line
    Add-Content -Path $LogFile -Value $line -ErrorAction SilentlyContinue
}

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
        throw 'Could not mount the system EFI System Partition.'
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

try {
    $bgDir = Join-Path $DataDir 'backgrounds'
    $pngs = @(Get-ChildItem -Path $bgDir -Filter '*.png' -File -ErrorAction SilentlyContinue)
    if (-not $pngs) {
        Log "No PNG backgrounds found in $bgDir; nothing to do."
        exit 0
    }

    $esps = @(Get-Partition | Where-Object { $_.GptType -eq $EspGuid })

    # First choice: the ESP the firmware's rEFInd boot entry points at -- on
    # multi-ESP machines a stale EFI\refind on another ESP must not shadow it.
    $mount = $null
    $nvramGuid = Get-RefindBootPartitionGuid
    $nvramPart = $esps | Where-Object {
        $nvramGuid -and ([guid]$_.Guid -eq $nvramGuid)
    } | Select-Object -First 1
    if ($nvramPart) {
        try {
            $m = Mount-EspPartition $nvramPart
            if (Test-Path (Join-Path $m.Root $RefindLoader)) {
                Log "Using the ESP from the firmware rEFInd boot entry (disk $($nvramPart.DiskNumber), partition $($nvramPart.PartitionNumber))."
                $mount = $m
            } else {
                Dismount-Esp $m
            }
        } catch {
            Log "Could not mount the firmware rEFInd entry's ESP: $_"
        }
    }

    # No usable firmware entry: pick the ESP that contains rEFInd, system ESP first.
    if (-not $mount) {
        $ordered = @($esps | Where-Object { $_.IsSystem }) + @($esps | Where-Object { -not $_.IsSystem })
        foreach ($p in $ordered) {
            try { $m = Mount-EspPartition $p } catch {
                Log "Skipping unreachable ESP (disk $($p.DiskNumber) partition $($p.PartitionNumber)): $_"
                continue
            }
            if (Test-Path (Join-Path $m.Root $RefindLoader)) { $mount = $m; break }
            Dismount-Esp $m
        }
    }
    if (-not $mount) {
        Log 'rEFInd was not found on any EFI System Partition; nothing to do.'
        exit 0
    }

    try {
        $destBg = Join-Path $mount.Root 'EFI\refind\background.png'
        # With more than one background available, avoid re-picking the one
        # that is already installed.
        $candidates = $pngs
        if ($pngs.Count -gt 1 -and (Test-Path $destBg)) {
            $current = (Get-FileHash -Algorithm SHA256 $destBg).Hash
            $fresh = @($pngs | Where-Object { (Get-FileHash -Algorithm SHA256 $_.FullName).Hash -ne $current })
            if ($fresh.Count) { $candidates = $fresh }
        }
        $bg = $candidates | Get-Random
        Copy-Item -Force $bg.FullName $destBg
        Log "Background set to $($bg.Name)"
    } finally {
        Dismount-Esp $mount
    }
} catch {
    Log "ERROR: $_"
    exit 1
}
