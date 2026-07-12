#Requires -RunAsAdministrator
# Copies a random background PNG to the rEFInd directory on the EFI System
# Partition. Run at logon by the "rEFInd_bg_randomizer" scheduled task.
$ErrorActionPreference = 'Stop'

$EspGuid = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'

function Mount-Esp {
    $esp = Get-Partition | Where-Object { $_.GptType -eq $EspGuid -and $_.IsSystem } | Select-Object -First 1
    if ($esp -and $esp.DriveLetter) {
        return @{ Root = "$($esp.DriveLetter):"; Dismount = $false }
    }
    $used = (Get-PSDrive -PSProvider FileSystem).Name
    foreach ($c in 'Z','Y','X','W','V','U','T') {
        if ($used -notcontains $c) {
            mountvol "${c}:" /S 2>$null | Out-Null
            if ($LASTEXITCODE -eq 0) {
                return @{ Root = "${c}:"; Dismount = $true }
            }
        }
    }
    throw 'Could not mount the EFI System Partition. Run this script as Administrator.'
}

function Dismount-Esp($esp) {
    if ($esp.Dismount) { mountvol $esp.Root /D | Out-Null }
}

$bgDir = Join-Path $env:LOCALAPPDATA 'SteamDeck_rEFInd\backgrounds'
$bg = Get-ChildItem -Path $bgDir -Filter '*.png' -File -ErrorAction SilentlyContinue | Get-Random -ErrorAction SilentlyContinue
if (-not $bg) {
    Write-Host "No PNG backgrounds found in $bgDir"
    exit 0
}

$esp = Mount-Esp
try {
    $dest = Join-Path $esp.Root 'EFI\refind'
    if (Test-Path $dest) {
        Copy-Item -Force $bg.FullName (Join-Path $dest 'background.png')
        Write-Host "Background set to $($bg.Name)"
    }
} finally {
    Dismount-Esp $esp
}
