#Requires -RunAsAdministrator
# Downloads rEFInd from SourceForge, installs it to the EFI System Partition,
# applies the GUI-generated config, and creates a dedicated "rEFInd" firmware
# boot entry (a Boot#### EFI variable, byte-identical to what efibootmgr -c
# creates on Linux). The Windows Boot Manager entry is left untouched --
# repointing {bootmgr}, as older versions did, dragged Windows' optional-data
# blob ("WINDOWS...BCDOBJECT={...}") along into the entry, which efibootmgr
# shows as a long hex tail after refind_x64.efi and rEFInd receives as junk
# load options (issue #23).
#
# Reversible: run windows\uninstall_rEFInd.ps1 (also run by the app's
# uninstaller); the previous boot state is saved to
# %LOCALAPPDATA%\SteamDeck_rEFInd\bootmgr-backup.txt.
$ErrorActionPreference = 'Stop'

# Visual feedback: numbered, colored step banners plus an overall progress bar
# so the elevated console shows at a glance how far the install has gotten.
$TotalSteps = 6
$script:StepNum = 0
function Write-Step([string]$Message) {
    $script:StepNum++
    Write-Progress -Activity 'Installing rEFInd' `
        -Status "Step $script:StepNum of ${TotalSteps}: $Message" `
        -PercentComplete ((($script:StepNum - 1) / $TotalSteps) * 100)
    Write-Host ''
    Write-Host "[$script:StepNum/$TotalSteps] $Message" -ForegroundColor Cyan
}
try { $Host.UI.RawUI.WindowTitle = 'rEFInd installation' } catch {}

$RefindVer = '0.14.2'
$EspGuid = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
$RefindLoader = '\EFI\refind\refind_x64.efi'
$EfiGlobalGuid = '{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}'
# Optional-data signature of Windows Boot Manager's own Boot#### entry; any
# entry carrying it belongs to {bootmgr} and must never be overwritten.
$BootmgrBlobHex = '57494e444f5753'

# mountvol reports failure on stderr, which Windows PowerShell 5.1 turns into a
# terminating RemoteException when redirected under ErrorActionPreference Stop;
# run it with the preference relaxed so a failed mount stays a plain exit code.
function Invoke-Mountvol([string[]]$mvArgs) {
    $eap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { mountvol @mvArgs 2>$null | Out-Null } finally { $ErrorActionPreference = $eap }
    return $LASTEXITCODE
}

function Mount-Esp {
    $esp = Get-Partition | Where-Object { $_.GptType -eq $EspGuid -and $_.IsSystem } | Select-Object -First 1
    if ($esp -and $esp.DriveLetter) {
        return @{ Root = "$($esp.DriveLetter):"; Dismount = $false; Part = $esp }
    }
    $used = (Get-PSDrive -PSProvider FileSystem).Name
    foreach ($c in 'Z','Y','X','W','V','U','T') {
        if ($used -notcontains $c) {
            if ((Invoke-Mountvol @("${c}:", '/S')) -eq 0) {
                return @{ Root = "${c}:"; Dismount = $true; Part = $esp }
            }
        }
    }
    throw 'Could not mount the EFI System Partition. Run this script as Administrator.'
}

function Dismount-Esp($esp) {
    if ($esp.Dismount) { $null = Invoke-Mountvol @($esp.Root, '/D') }
}

# bcdedit is a native exe: under $ErrorActionPreference = 'Stop' a failed call
# neither throws nor stops the script (so errors would sail past unnoticed),
# and stderr captured via 2>&1 becomes a terminating NativeCommandError. Run it
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

# ---- UEFI NVRAM access (the Windows equivalent of efibootmgr) ----
# bcdedit on current Windows 11 cannot create firmware boot entries at all
# (/application firmware was removed, and {fwbootmgr} displayorder silently
# ignores non-firmware objects), so the Boot#### variable is written directly.
# Keep these helpers in sync with windows\uninstall_rEFInd.ps1.
Add-Type -Namespace RefindUefi -Name Native -MemberDefinition @'
[DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
public static extern uint GetFirmwareEnvironmentVariableW(string lpName, string lpGuid, byte[] pBuffer, uint nSize);
[DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
public static extern bool SetFirmwareEnvironmentVariableW(string lpName, string lpGuid, byte[] pValue, uint nSize);
[DllImport("advapi32.dll", SetLastError=true)]
public static extern bool OpenProcessToken(IntPtr h, uint acc, out IntPtr tok);
[DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
public static extern bool LookupPrivilegeValueW(string sys, string name, out long luid);
[DllImport("advapi32.dll", SetLastError=true)]
public static extern bool AdjustTokenPrivileges(IntPtr tok, bool dis, ref TOKPRIV newst, int len, IntPtr prev, IntPtr ret);
[DllImport("kernel32.dll")]
public static extern IntPtr GetCurrentProcess();
[StructLayout(LayoutKind.Sequential, Pack=4)]
public struct TOKPRIV { public uint Count; public long Luid; public uint Attr; }
'@

function Enable-UefiPrivilege {
    $tok = [IntPtr]::Zero
    [RefindUefi.Native]::OpenProcessToken([RefindUefi.Native]::GetCurrentProcess(), 0x28, [ref]$tok) | Out-Null
    $luid = 0L
    [RefindUefi.Native]::LookupPrivilegeValueW($null, 'SeSystemEnvironmentPrivilege', [ref]$luid) | Out-Null
    $tp = New-Object RefindUefi.Native+TOKPRIV
    $tp.Count = 1; $tp.Luid = $luid; $tp.Attr = 2  # SE_PRIVILEGE_ENABLED
    [RefindUefi.Native]::AdjustTokenPrivileges($tok, $false, [ref]$tp, 0, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
}

function Get-UefiVar([string]$name) {
    $buf = New-Object byte[] 4096
    $n = [RefindUefi.Native]::GetFirmwareEnvironmentVariableW($name, $EfiGlobalGuid, $buf, $buf.Length)
    if ($n -eq 0) { return $null }
    return ,$buf[0..($n - 1)]
}

# Empty/absent $value deletes the variable.
function Set-UefiVar([string]$name, [byte[]]$value) {
    $len = if ($value) { $value.Length } else { 0 }
    return [RefindUefi.Native]::SetFirmwareEnvironmentVariableW($name, $EfiGlobalGuid, $value, $len)
}

function ConvertTo-HexString([byte[]]$bytes) {
    if (-not $bytes) { return '' }
    return (($bytes | ForEach-Object { $_.ToString('x2') }) -join '')
}

# EFI_LOAD_OPTION: attributes, file-path-list length, UTF-16 description,
# then HD(partition) + File(loader) + End device path nodes.
function New-EfiLoadOption($part, [uint32]$sectorSize, [string]$description, [string]$loaderPath) {
    $bytes = New-Object System.Collections.Generic.List[byte]

    $hd = New-Object System.Collections.Generic.List[byte]
    $hd.AddRange([byte[]]@(4, 1, 42, 0))  # media device path, HD node, length 42
    0..3 | ForEach-Object { $hd.Add((([uint32]$part.PartitionNumber) -shr (8 * $_)) -band 0xFF) }
    $startLba = [uint64]($part.Offset / $sectorSize)
    $sizeLba = [uint64]($part.Size / $sectorSize)
    0..7 | ForEach-Object { $hd.Add([byte](($startLba -shr (8 * $_)) -band 0xFF)) }
    0..7 | ForEach-Object { $hd.Add([byte](($sizeLba -shr (8 * $_)) -band 0xFF)) }
    $hd.AddRange(([guid]$part.Guid).ToByteArray())  # partition signature
    $hd.Add(2)                                      # partition format: GPT
    $hd.Add(2)                                      # signature type: GUID

    $file = New-Object System.Collections.Generic.List[byte]
    $fileChars = [System.Text.Encoding]::Unicode.GetBytes($loaderPath + [char]0)
    $fileLen = 4 + $fileChars.Length
    $file.AddRange([byte[]]@(4, 4, ($fileLen -band 0xFF), (($fileLen -shr 8) -band 0xFF)))
    $file.AddRange($fileChars)

    $end = [byte[]]@(0x7f, 0xff, 4, 0)
    $fpLen = $hd.Count + $file.Count + $end.Length

    0..3 | ForEach-Object { $bytes.Add((([uint32]1) -shr (8 * $_)) -band 0xFF) }  # LOAD_OPTION_ACTIVE
    $bytes.Add($fpLen -band 0xFF); $bytes.Add(($fpLen -shr 8) -band 0xFF)
    $bytes.AddRange([System.Text.Encoding]::Unicode.GetBytes($description + [char]0))
    $bytes.AddRange($hd); $bytes.AddRange($file); $bytes.AddRange($end)
    return ,$bytes.ToArray()
}

function Get-BootOrderIds {
    $bo = Get-UefiVar 'BootOrder'
    if (-not $bo) { return @() }
    return @(for ($i = 0; $i + 1 -lt $bo.Length; $i += 2) { '{0:X4}' -f ($bo[$i] + ($bo[$i + 1] * 256)) })
}

function Set-BootOrderIds([string[]]$ids) {
    $bytes = New-Object System.Collections.Generic.List[byte]
    foreach ($id in $ids) {
        $v = [convert]::ToUInt16($id, 16)
        $bytes.Add($v -band 0xFF); $bytes.Add(($v -shr 8) -band 0xFF)
    }
    return Set-UefiVar 'BootOrder' $bytes.ToArray()
}

$esp = $null
$backup = $null
$installError = $null
$entryId = $null
try {
    Write-Step 'Downloading rEFInd from SourceForge...'
    $zip = Join-Path $env:TEMP "refind-bin-$RefindVer.zip"
    $zipUrl = "https://sourceforge.net/projects/refind/files/$RefindVer/refind-bin-gnuefi-$RefindVer.zip/download"
    Invoke-WebRequest -Uri $zipUrl -OutFile $zip -UserAgent 'Wget' -MaximumRedirection 10

    Write-Step 'Extracting...'
    $extract = Join-Path $env:TEMP 'refind-bin-extract'
    Remove-Item -Recurse -Force $extract -ErrorAction SilentlyContinue
    Expand-Archive -Path $zip -DestinationPath $extract -Force
    $bin = Join-Path $extract "refind-bin-$RefindVer"
    if (-not (Test-Path $bin)) {
        throw "Extraction did not produce the expected directory: $bin"
    }

    $esp = Mount-Esp
    $dest = Join-Path $esp.Root 'EFI\refind'
    Write-Step "Installing rEFInd files to $dest ..."
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
    Write-Step 'Downloading UsbXbox360Dxe.efi controller driver...'
    $driverDest = Join-Path $dest 'drivers_x64\UsbXbox360Dxe.efi'
    $driverUrl = 'https://github.com/jlobue10/UsbXbox360Dxe/releases/latest/download/UsbXbox360Dxe.efi'
    try {
        Invoke-WebRequest -Uri $driverUrl -OutFile $driverDest -MaximumRedirection 10
    } catch {
        Write-Warning "Failed to download UsbXbox360Dxe.efi; skipping controller driver. $_"
    }

    # Back up any existing config, then apply the GUI-generated one.
    Write-Step 'Applying the GUI-generated configuration...'
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

    Write-Step 'Creating the rEFInd firmware boot entry...'
    if (-not $esp.Part) {
        throw 'Could not identify the system EFI System Partition for the boot entry.'
    }
    Enable-UefiPrivilege
    $backupDir = Join-Path $env:LOCALAPPDATA 'SteamDeck_rEFInd'
    New-Item -ItemType Directory -Force $backupDir | Out-Null
    # Snapshot {bootmgr}, the firmware boot order, and raw BootOrder bytes
    # before modifying anything; abort untouched if the BCD store is unreadable.
    $bootmgrBefore = Invoke-Bcdedit '/enum', '{bootmgr}'
    if (-not $bootmgrBefore.Ok) {
        throw ("Could not read the {bootmgr} entry; boot settings were left untouched.`n" +
               ($bootmgrBefore.Output -join "`n"))
    }
    $fwBefore = Invoke-Bcdedit '/enum', '{fwbootmgr}'
    $backup = Join-Path $backupDir 'bootmgr-backup.txt'
    $bootmgrBefore.Output + $fwBefore.Output +
        @('', ('BootOrder(hex): ' + (ConvertTo-HexString (Get-UefiVar 'BootOrder')))) |
        Out-File -FilePath $backup -Encoding utf8

    # Older versions repointed {bootmgr} at rEFInd; undo that so Windows Boot
    # Manager is a normal Windows entry again (rEFInd chainloads it).
    $m = $bootmgrBefore.Output | Select-String -Pattern '^\s*path\s+(\S+)\s*$' | Select-Object -First 1
    if ($m -and $m.Matches[0].Groups[1].Value -ieq $RefindLoader) {
        Write-Host 'Restoring the Windows Boot Manager entry (repointed by an older version)...'
        $r = Invoke-Bcdedit '/set', '{bootmgr}', 'path', '\EFI\Microsoft\Boot\bootmgfw.efi'
        if (-not $r.Ok) { Write-Warning "Could not restore {bootmgr} path: $($r.Output -join ' ')" }
        $r = Invoke-Bcdedit '/set', '{bootmgr}', 'description', 'Windows Boot Manager'
        if (-not $r.Ok) { Write-Warning "Could not restore {bootmgr} description: $($r.Output -join ' ')" }
    }

    $sectorSize = (Get-Disk -Number $esp.Part.DiskNumber).LogicalSectorSize
    $loadOption = New-EfiLoadOption $esp.Part $sectorSize 'rEFInd' $RefindLoader
    $loadOptionHex = ConvertTo-HexString $loadOption
    $espGuidHex = ConvertTo-HexString (([guid]$esp.Part.Guid).ToByteArray())
    $loaderHex = ConvertTo-HexString ([System.Text.Encoding]::Unicode.GetBytes($RefindLoader))

    # Reuse an existing rEFInd entry for this ESP (rerun/upgrade) instead of
    # accumulating duplicates; otherwise take the first free Boot#### slot.
    # Entries carrying Windows Boot Manager's optional-data blob are always
    # left alone -- overwriting {bootmgr}'s own variable would break Windows.
    $freeId = $null
    foreach ($i in 0..255) {
        $id = '{0:X4}' -f $i
        $existing = Get-UefiVar "Boot$id"
        if (-not $existing) {
            if (-not $freeId) { $freeId = $id }
            continue
        }
        $hex = ConvertTo-HexString $existing
        if (-not $entryId -and $hex.Contains($loaderHex) -and $hex.Contains($espGuidHex) -and
            -not $hex.Contains($BootmgrBlobHex)) {
            $entryId = $id
        }
    }
    if (-not $entryId) { $entryId = $freeId }
    if (-not $entryId) { throw 'No free Boot#### slot found in NVRAM.' }

    if (-not (Set-UefiVar "Boot$entryId" $loadOption)) {
        throw ("Writing the Boot$entryId NVRAM variable failed (error " +
               [System.Runtime.InteropServices.Marshal]::GetLastWin32Error() +
               '). Boot settings are unchanged; Windows still boots normally.')
    }
    if ((ConvertTo-HexString (Get-UefiVar "Boot$entryId")) -ne $loadOptionHex) {
        throw "Boot$entryId did not read back as written."
    }
    Write-Host "rEFInd boot entry written as Boot$entryId."

    # Put rEFInd first in the boot order -- the same guarantee efibootmgr -c
    # gives on Linux. Windows stays bootable throughout: its own entry is
    # untouched and merely follows rEFInd in the order.
    $order = @(Get-BootOrderIds) | Where-Object { $_ -ne $entryId }
    if (-not (Set-BootOrderIds (@($entryId) + $order))) {
        Write-Warning 'Could not put rEFInd first in the firmware boot order.'
    }
} catch {
    $installError = $_
} finally {
    if ($esp) { Dismount-Esp $esp }
}

# Verify the result from live NVRAM rather than trusting the steps above --
# the Windows analog of the Linux scripts' efibootmgr read-back summary.
Write-Progress -Activity 'Installing rEFInd' -Completed
Write-Host ''
Write-Host '==================== Installation summary ====================' -ForegroundColor Cyan
if ($installError) {
    Write-Host "ERROR: $installError" -ForegroundColor Red
    Write-Host '---------------------------------------------------------------'
}
$entryOk = $false
$orderIds = @()
if ($entryId) {
    $entryOk = $null -ne (Get-UefiVar "Boot$entryId")
    $orderIds = @(Get-BootOrderIds)
    Write-Host "rEFInd entry:        Boot$entryId $(if ($entryOk) { '(present in NVRAM)' } else { '(MISSING from NVRAM)' })"
    Write-Host "Firmware boot order: $($orderIds -join ', ')"
}
$bootmgrNow = Invoke-Bcdedit '/enum', '{bootmgr}'
$bcdPath = $null
$m = $bootmgrNow.Output | Select-String -Pattern '^\s*path\s+(\S+)\s*$' | Select-Object -First 1
if ($bootmgrNow.Ok -and $m) { $bcdPath = $m.Matches[0].Groups[1].Value }
Write-Host "Windows Boot Manager path: $bcdPath"
Write-Host '---------------------------------------------------------------'
if ($installError) {
    Write-Host '*** FAILED: the installation did not complete -- see the error above. ***' -ForegroundColor Red
} elseif (-not $entryOk) {
    Write-Host '*** FAILED: the rEFInd boot entry is not present in NVRAM.          ***' -ForegroundColor Red
    Write-Host '*** rEFInd will NOT load at boot -- see any errors above.           ***' -ForegroundColor Red
} elseif ($orderIds.Count -and $orderIds[0] -ne $entryId) {
    Write-Host "WARNING: the rEFInd entry exists, but is NOT first in the firmware" -ForegroundColor Yellow
    Write-Host "boot order (it starts with Boot$($orderIds[0]))." -ForegroundColor Yellow
} else {
    Write-Host 'SUCCESS: rEFInd is installed and first in the firmware boot order.' -ForegroundColor Green
    Write-Host '(Windows Boot Manager was left untouched; rEFInd chainloads it.)'
}

if ($backup) {
    Write-Host ''
    Write-Host "Previous boot settings were saved to: $backup"
    Write-Host 'To remove rEFInd again, run (as Administrator):'
    Write-Host "  powershell -ExecutionPolicy Bypass -File `"$env:LOCALAPPDATA\SteamDeck_rEFInd\windows\uninstall_rEFInd.ps1`""
    Write-Host 'or uninstall "SteamDeck rEFInd GUI" from Windows Settings > Apps.'
}

# Hold the window open so the log and summary can actually be read (the
# Windows analog of the Linux scripts' tty-guarded pause), then close it --
# Environment.Exit ends the process even under the GUI launcher's -NoExit,
# so the user isn't left at a stray PowerShell prompt. The catch covers
# non-interactive hosts, where Read-Host cannot prompt.
Write-Host ''
try { $null = Read-Host 'Press Enter to close this window' } catch {}
[Environment]::Exit($(if ($installError) { 1 } else { 0 }))
