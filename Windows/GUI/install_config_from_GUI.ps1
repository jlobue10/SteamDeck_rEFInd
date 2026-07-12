#Requires -RunAsAdministrator
# Installs the GUI-generated refind.conf and PNGs onto the EFI System Partition.
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

$esp = Mount-Esp
try {
    $src = Join-Path $env:LOCALAPPDATA 'SteamDeck_rEFInd\GUI'
    $dest = Join-Path $esp.Root 'EFI\refind'
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
    Dismount-Esp $esp
}
