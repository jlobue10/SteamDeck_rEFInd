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
$RefindLoader = '\EFI\refind\refind_x64.efi'

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

# bcdedit is a native exe: under $ErrorActionPreference = 'Stop' a failed call
# neither throws nor stops the script (so errors would sail past unnoticed --
# the same silent-failure class the Linux scripts' efibootmgr phase had), and
# stderr captured via 2>&1 becomes a terminating NativeCommandError. Run it
# with the preference relaxed and report success via the exit code.
function Invoke-Bcdedit {
    param([string[]]$BcdArgs)
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & bcdedit @BcdArgs 2>&1 | ForEach-Object { "$_" }
        return [pscustomobject]@{ Ok = ($LASTEXITCODE -eq 0); Output = @($out) }
    } finally {
        $ErrorActionPreference = $prev
    }
}

$esp = $null
$backup = $null
$installError = $null
try {
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
    $dest = Join-Path $esp.Root 'EFI\refind'
    Write-Host "Installing rEFInd files to $dest ..."
    New-Item -ItemType Directory -Force $dest | Out-Null
    Copy-Item -Force (Join-Path $bin 'refind\refind_x64.efi') $dest
    foreach ($d in 'drivers_x64','tools_x64','icons') {
        Copy-Item -Recurse -Force (Join-Path $bin "refind\$d") $dest
    }
    Copy-Item -Recurse -Force (Join-Path $bin 'fonts') $dest

    # SkorionOS Xbox 360 USB controller UEFI driver: dropping it into rEFInd's
    # drivers_x64 folder makes wired/handheld gamepads (ROG Ally, Legion Go,
    # etc.) usable in the boot menu. The driver auto-creates its own config at
    # \EFI\Xbox360\config.ini on first boot, so only the .efi is needed here.
    # NOTE: temporarily fetched from the jlobue10 fork (adds Legion Go 2 PIDs +
    # Ally lockup fix); revert to SkorionOS once upstream PR #6 is merged/released.
    Write-Host 'Downloading UsbXbox360Dxe.efi controller driver...'
    $driverDest = Join-Path $dest 'drivers_x64\UsbXbox360Dxe.efi'
    $driverUrl = 'https://github.com/jlobue10/UsbXbox360Dxe/releases/latest/download/UsbXbox360Dxe.efi'
    try {
        Invoke-WebRequest -Uri $driverUrl -OutFile $driverDest -MaximumRedirection 10
    } catch {
        Write-Warning "Failed to download UsbXbox360Dxe.efi; skipping controller driver. $_"
    }

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
    $backupDir = Join-Path $env:LOCALAPPDATA 'SteamDeck_rEFInd'
    New-Item -ItemType Directory -Force $backupDir | Out-Null
    # Snapshot {bootmgr} and the firmware boot order before modifying either;
    # if the BCD store can't even be read, abort while nothing has changed.
    $bootmgrBefore = Invoke-Bcdedit '/enum', '{bootmgr}'
    if (-not $bootmgrBefore.Ok) {
        throw ("Could not read the {bootmgr} entry; boot settings were left untouched.`n" +
               ($bootmgrBefore.Output -join "`n"))
    }
    $fwBefore = Invoke-Bcdedit '/enum', '{fwbootmgr}'
    $backup = Join-Path $backupDir 'bootmgr-backup.txt'
    $bootmgrBefore.Output + $fwBefore.Output | Out-File -FilePath $backup -Encoding utf8

    $set = Invoke-Bcdedit '/set', '{bootmgr}', 'path', $RefindLoader
    if (-not $set.Ok) {
        throw ("Pointing {bootmgr} at rEFInd failed; boot settings were left untouched.`n" +
               ($set.Output -join "`n"))
    }
    $desc = Invoke-Bcdedit '/set', '{bootmgr}', 'description', 'rEFInd Boot Manager'
    if (-not $desc.Ok) {
        Write-Warning "Could not rename the boot entry (cosmetic only): $($desc.Output -join ' ')"
    }
    # Repointing {bootmgr} only takes effect if the firmware actually boots
    # Windows Boot Manager -- put it first in the firmware boot order, the
    # same guarantee efibootmgr -c gives the Linux scripts.
    $order = Invoke-Bcdedit '/set', '{fwbootmgr}', 'displayorder', '{bootmgr}', '/addfirst'
    if (-not $order.Ok) {
        Write-Warning "Could not put Windows Boot Manager first in the firmware boot order: $($order.Output -join ' ')"
    }
} catch {
    $installError = $_
} finally {
    if ($esp) { Dismount-Esp $esp }
}

# Verify the result from the live BCD store rather than trusting the steps
# above -- the Windows analog of the Linux scripts' NVRAM read-back summary.
Write-Host ''
Write-Host '==================== Installation summary ===================='
if ($installError) {
    Write-Host "ERROR: $installError"
    Write-Host '---------------------------------------------------------------'
}
$bootmgrNow = Invoke-Bcdedit '/enum', '{bootmgr}'
$fwNow = Invoke-Bcdedit '/enum', '{fwbootmgr}'
$bootmgrNow.Output | ForEach-Object { Write-Host $_ }
Write-Host '---------------------------------------------------------------'
$bcdPath = $null
$m = $bootmgrNow.Output | Select-String -Pattern '^\s*path\s+(\S+)\s*$' | Select-Object -First 1
if ($bootmgrNow.Ok -and $m) { $bcdPath = $m.Matches[0].Groups[1].Value }
# First identifier after "displayorder" is the head of the firmware boot order.
$fwFirst = $null
$m = $fwNow.Output | Select-String -Pattern '^\s*displayorder\s+(\S+)\s*$' | Select-Object -First 1
if ($fwNow.Ok -and $m) { $fwFirst = $m.Matches[0].Groups[1].Value }

if ($installError) {
    Write-Host '*** FAILED: the installation did not complete -- see the error above. ***'
    if ($bcdPath -eq $RefindLoader) {
        Write-Host '(The boot entry currently points at rEFInd, likely from an earlier install.)'
    }
} elseif ($bcdPath -ne $RefindLoader) {
    Write-Host '*** FAILED: the Windows Boot Manager entry does not point at rEFInd. ***'
    Write-Host '*** rEFInd will NOT load at boot -- see any errors above.            ***'
} elseif ($fwFirst -and $fwFirst -ne '{bootmgr}') {
    Write-Host 'WARNING: the boot entry points at rEFInd, but Windows Boot Manager is'
    Write-Host "NOT first in the firmware boot order (it starts with $fwFirst)."
} else {
    Write-Host 'SUCCESS: rEFInd is installed and the firmware boot entry points at it.'
}
if ($backup) {
    Write-Host ''
    Write-Host "Previous boot manager settings were saved to: $backup"
    Write-Host 'To revert to booting Windows directly, run (as Administrator):'
    Write-Host '  bcdedit /set "{bootmgr}" path \EFI\Microsoft\Boot\bootmgfw.efi'
    Write-Host '  bcdedit /set "{bootmgr}" description "Windows Boot Manager"'
}
