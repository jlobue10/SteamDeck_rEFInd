#Requires -RunAsAdministrator
# Downloads rEFInd from SourceForge, installs it to the EFI System Partition,
# applies the GUI-generated config, and points the Windows Boot Manager
# firmware entry at rEFInd.
#
# Reversible: the previous {bootmgr} values are saved to
# %LOCALAPPDATA%\SteamDeck_rEFInd\bootmgr-backup.txt, and the revert command is
# printed at the end.
$ErrorActionPreference = 'Stop'

$RefindVer = '0.14.2'
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

Write-Host 'Downloading rEFInd zip file...'
$zip = Join-Path $env:TEMP "refind-bin-$RefindVer.zip"
$zipUrl = "https://sourceforge.net/projects/refind/files/$RefindVer/refind-bin-gnuefi-$RefindVer.zip/download"
Invoke-WebRequest -Uri $zipUrl -OutFile $zip -UserAgent 'Wget' -MaximumRedirection 10

Write-Host 'Extracting...'
$extract = Join-Path $env:TEMP 'refind-bin-extract'
Remove-Item -Recurse -Force $extract -ErrorAction SilentlyContinue
Expand-Archive -Path $zip -DestinationPath $extract -Force
$bin = Join-Path $extract "refind-bin-$RefindVer"
if (-not (Test-Path $bin)) {
    throw "Extraction did not produce the expected directory: $bin"
}

$esp = Mount-Esp
try {
    $dest = Join-Path $esp.Root 'EFI\refind'
    Write-Host "Installing rEFInd files to $dest ..."
    New-Item -ItemType Directory -Force $dest | Out-Null
    Copy-Item -Force (Join-Path $bin 'refind\refind_x64.efi') $dest
    foreach ($d in 'drivers_x64','tools_x64','icons') {
        Copy-Item -Recurse -Force (Join-Path $bin "refind\$d") $dest
    }
    Copy-Item -Recurse -Force (Join-Path $bin 'fonts') $dest

    # Back up any existing config, then apply the GUI-generated one.
    $conf = Join-Path $dest 'refind.conf'
    if (Test-Path $conf) {
        Copy-Item -Force $conf (Join-Path $dest 'refind-bkp.conf')
    }
    $src = Join-Path $env:LOCALAPPDATA 'SteamDeck_rEFInd\GUI'
    foreach ($f in 'refind.conf','background.png','os_icon1.png','os_icon2.png','os_icon3.png','os_icon4.png') {
        $p = Join-Path $src $f
        if (Test-Path $p) { Copy-Item -Force $p (Join-Path $dest $f) }
    }
    foreach ($d in 'backgrounds','icons') {
        $p = Join-Path $env:LOCALAPPDATA "SteamDeck_rEFInd\$d"
        if (Test-Path $p) { Copy-Item -Recurse -Force $p $dest }
    }

    Write-Host 'Pointing the Windows Boot Manager firmware entry at rEFInd...'
    $backup = Join-Path $env:LOCALAPPDATA 'SteamDeck_rEFInd\bootmgr-backup.txt'
    New-Item -ItemType Directory -Force (Split-Path $backup) | Out-Null
    bcdedit /enum '{bootmgr}' | Out-File -FilePath $backup -Encoding utf8
    bcdedit /set '{bootmgr}' path \EFI\refind\refind_x64.efi
    bcdedit /set '{bootmgr}' description 'rEFInd Boot Manager'
} finally {
    Dismount-Esp $esp
}

Write-Host ''
Write-Host 'rEFInd installed successfully.'
Write-Host "Previous boot manager settings were saved to: $backup"
Write-Host 'To revert to booting Windows directly, run (as Administrator):'
Write-Host '  bcdedit /set "{bootmgr}" path \EFI\Microsoft\Boot\bootmgfw.efi'
Write-Host '  bcdedit /set "{bootmgr}" description "Windows Boot Manager"'
