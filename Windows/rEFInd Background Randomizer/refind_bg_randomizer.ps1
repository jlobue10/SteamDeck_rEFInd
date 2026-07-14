#Requires -RunAsAdministrator
<#

rEFInd background randomizer for Windows by ryanrudolf
https://github.com/ryanrudolfoba/

Prerequisites:

Place png images that you wish to use as backgrounds for rEFInd here -
C:\refind_scripts\backgrounds

Your refind.conf must reference the randomized image, e.g.:
banner backgrounds/background.png

Place this script in a scheduled task to automatically run at startup. In the
task, enable "Run with highest privileges" and (under Conditions) untick
"Start the task only if the computer is on AC power" so it also runs on
battery. Progress and errors are written to
C:\refind_scripts\refind_bg_randomizer.log.

#>
$ErrorActionPreference = 'Stop'

$EspGuid = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
$RefindLoader = 'EFI\refind\refind_x64.efi'
$ScriptsDir = 'C:\refind_scripts'
$LogFile = Join-Path $ScriptsDir 'refind_bg_randomizer.log'

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
    $bgDir = Join-Path $ScriptsDir 'backgrounds'
    $pngs = @(Get-ChildItem -Path $bgDir -Filter '*.png' -File -ErrorAction SilentlyContinue)
    if (-not $pngs) {
        Log "No PNG backgrounds found in $bgDir; nothing to do."
        exit 0
    }

    # Pick the ESP that actually contains rEFInd, system ESP first.
    $esps = @(Get-Partition | Where-Object { $_.GptType -eq $EspGuid })
    $ordered = @($esps | Where-Object { $_.IsSystem }) + @($esps | Where-Object { -not $_.IsSystem })
    $mount = $null
    foreach ($p in $ordered) {
        try { $m = Mount-EspPartition $p } catch {
            Log "Skipping unreachable ESP (disk $($p.DiskNumber) partition $($p.PartitionNumber)): $_"
            continue
        }
        if (Test-Path (Join-Path $m.Root $RefindLoader)) { $mount = $m; break }
        Dismount-Esp $m
    }
    if (-not $mount) {
        Log 'rEFInd was not found on any EFI System Partition; nothing to do.'
        exit 0
    }

    try {
        $destDir = Join-Path $mount.Root 'EFI\refind\backgrounds'
        New-Item -ItemType Directory -Force $destDir | Out-Null
        $destBg = Join-Path $destDir 'background.png'
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
