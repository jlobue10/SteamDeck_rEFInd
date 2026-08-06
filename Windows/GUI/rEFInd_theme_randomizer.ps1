#Requires -RunAsAdministrator
# Picks a random rEFInd theme by replacing themes\active_theme.conf on
# whichever EFI System Partition actually contains rEFInd -- the Windows
# counterpart of rEFInd_theme_randomizer.sh. The live refind.conf is never
# rewritten: the GUI's Create Config appends one stable
# "include themes/active_theme.conf" line when theming is enabled, and this
# script only ever swaps the file that line points at. Run hidden at logon by
# the "SteamDeck_rEFInd_theme_randomizer" scheduled task, so progress and
# errors are also written to rEFInd_theme_randomizer.log in the app data
# directory.
$ErrorActionPreference = 'Stop'

$EspGuid = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
$RefindLoader = 'EFI\refind\refind_x64.efi'
$DataDir = Join-Path $env:LOCALAPPDATA 'SteamDeck_rEFInd'
$LogFile = Join-Path $DataDir 'rEFInd_theme_randomizer.log'
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
        $refindDir = Join-Path $mount.Root 'EFI\refind'
        # Theming disabled (Theme combo set to None, or a hand-written config):
        # the live refind.conf does not include active_theme.conf, so swapping
        # it would change nothing at boot. This is the normal "off" state.
        $conf = Join-Path $refindDir 'refind.conf'
        if (-not ((Test-Path $conf) -and
                (Select-String -Path $conf -SimpleMatch 'include themes/active_theme.conf' -Quiet))) {
            Log 'Theming is not enabled in refind.conf; nothing to do.'
            exit 0
        }

        $themesDir = Join-Path $refindDir 'themes'
        # Candidates are the installed themes' own confs: themes\*\theme.conf,
        # non-empty. active_theme.conf itself sits at themes\ top level, so the
        # per-directory pattern can never pick it.
        $confs = @(Get-ChildItem -Path (Join-Path $themesDir '*\theme.conf') -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Length -gt 0 })
        if (-not $confs) {
            Log "No themes found under $themesDir; keeping the current theme."
            exit 0
        }

        # With more than one theme available, avoid re-picking the one that is
        # already active.
        $activeConf = Join-Path $themesDir 'active_theme.conf'
        $candidates = $confs
        if ($confs.Count -gt 1 -and (Test-Path $activeConf)) {
            $current = (Get-FileHash -Algorithm SHA256 $activeConf).Hash
            $fresh = @($confs | Where-Object { (Get-FileHash -Algorithm SHA256 $_.FullName).Hash -ne $current })
            if ($fresh.Count) { $candidates = $fresh }
        }
        $pick = $candidates | Get-Random

        # Best-effort sweep of staging files an interrupted earlier run left
        # behind, then a staged publish (same-directory copy + rename, like the
        # Linux script) so a failure mid-copy can never truncate the live
        # active_theme.conf.
        Get-ChildItem -Path (Join-Path $themesDir '.active_theme.conf.*') -File -ErrorAction SilentlyContinue |
            Remove-Item -Force -ErrorAction SilentlyContinue
        $stage = Join-Path $themesDir ('.active_theme.conf.' + [guid]::NewGuid().ToString('N'))
        try {
            Copy-Item -Force $pick.FullName $stage
            Move-Item -Force $stage $activeConf
        } finally {
            Remove-Item -Force -ErrorAction SilentlyContinue $stage
        }
        Log "Theme set to $($pick.Directory.Name)"
    } finally {
        Dismount-Esp $mount
    }
} catch {
    Log "ERROR: $_"
    exit 1
}
