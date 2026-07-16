#Requires -RunAsAdministrator
# Fully removes the Windows-side rEFInd install:
#   - restores the Windows Boot Manager entry if an older GUI version
#     repointed {bootmgr} at rEFInd
#   - deletes the rEFInd Boot#### NVRAM entries that target the system ESP
#     (rEFInd entries pointing at another ESP -- e.g. a Linux-side install --
#     are reported and left alone) and drops them from BootOrder
#   - removes EFI\refind and EFI\Xbox360 from the system ESP
#     (pass -KeepEspFiles to keep the files and only undo the boot entries)
#   - unregisters the rEFInd_bg_randomizer scheduled task
# Run standalone as Administrator, or automatically via the app's uninstaller.
param([switch]$KeepEspFiles)
$ErrorActionPreference = 'Stop'

$EspGuid = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
$RefindLoader = '\EFI\refind\refind_x64.efi'
$EfiGlobalGuid = '{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}'
# Optional-data signature of Windows Boot Manager's own Boot#### entry; any
# entry carrying it belongs to {bootmgr} and must never be deleted here.
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
# neither throws nor stops the script, and stderr captured via 2>&1 becomes a
# terminating NativeCommandError. Run it relaxed; report via the exit code.
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

# ---- UEFI NVRAM access; keep in sync with windows\install_rEFInd.ps1 ----
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

# UTF-16 description at offset 6 of an EFI_LOAD_OPTION.
function Get-LoadOptionDescription([byte[]]$bytes) {
    $i = 6
    $desc = ''
    while ($i + 1 -lt $bytes.Length) {
        $ch = $bytes[$i] + ($bytes[$i + 1] * 256)
        if ($ch -eq 0) { break }
        $desc += [char]$ch
        $i += 2
    }
    return $desc
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

Write-Host 'Removing the Windows-side rEFInd install...'
Enable-UefiPrivilege

# 1. Restore the Windows Boot Manager entry if an older version repointed it.
$bootmgr = Invoke-Bcdedit '/enum', '{bootmgr}'
if ($bootmgr.Ok) {
    $m = $bootmgr.Output | Select-String -Pattern '^\s*path\s+(\S+)\s*$' | Select-Object -First 1
    if ($m -and $m.Matches[0].Groups[1].Value -ieq $RefindLoader) {
        Write-Host 'Restoring the Windows Boot Manager entry (was repointed at rEFInd)...'
        $r = Invoke-Bcdedit '/set', '{bootmgr}', 'path', '\EFI\Microsoft\Boot\bootmgfw.efi'
        if (-not $r.Ok) { Write-Warning "Could not restore {bootmgr} path: $($r.Output -join ' ')" }
        $r = Invoke-Bcdedit '/set', '{bootmgr}', 'description', 'Windows Boot Manager'
        if (-not $r.Ok) { Write-Warning "Could not restore {bootmgr} description: $($r.Output -join ' ')" }
    }
} else {
    Write-Warning "Could not read the {bootmgr} entry: $($bootmgr.Output -join ' ')"
}

# 2. Delete rEFInd NVRAM entries that target the system ESP; report entries on
# other ESPs (a Linux-side rEFInd install) and leave them alone.
$esp = $null
$removed = @()
$foreign = @()
try {
    $esp = Mount-Esp
} catch {
    Write-Warning "System ESP unavailable; skipping boot entry and file cleanup. $_"
}
if ($esp -and $esp.Part) {
    try {
        $espGuidHex = ConvertTo-HexString (([guid]$esp.Part.Guid).ToByteArray())
        $loaderHex = ConvertTo-HexString ([System.Text.Encoding]::Unicode.GetBytes($RefindLoader))
        foreach ($i in 0..255) {
            $id = '{0:X4}' -f $i
            $bytes = Get-UefiVar "Boot$id"
            if (-not $bytes) { continue }
            $hex = ConvertTo-HexString $bytes
            if (-not $hex.Contains($loaderHex)) { continue }
            if ($hex.Contains($BootmgrBlobHex)) { continue }  # {bootmgr}'s own entry; restored above
            $desc = Get-LoadOptionDescription $bytes
            if ($hex.Contains($espGuidHex)) {
                if (Set-UefiVar "Boot$id" $null) {
                    Write-Host "Deleted boot entry Boot$id ('$desc')."
                    $removed += $id
                } else {
                    Write-Warning "Could not delete boot entry Boot$id ('$desc')."
                }
            } else {
                $foreign += "Boot$id ('$desc')"
            }
        }
        if ($removed.Count) {
            $order = @(Get-BootOrderIds) | Where-Object { $removed -notcontains $_ }
            if (-not (Set-BootOrderIds $order)) {
                Write-Warning 'Could not update the firmware boot order.'
            }
        }
        if ($foreign.Count) {
            Write-Host "Left untouched (rEFInd on another ESP, e.g. a Linux install): $($foreign -join ', ')"
        }

        # 3. Remove rEFInd's files (and the Xbox 360 driver's config dir) from
        # the system ESP.
        if (-not $KeepEspFiles) {
            foreach ($d in 'EFI\refind', 'EFI\Xbox360') {
                $p = Join-Path $esp.Root $d
                if (Test-Path $p) {
                    Remove-Item -Recurse -Force $p
                    Write-Host "Removed $d from the EFI System Partition."
                }
            }
        }
    } finally {
        Dismount-Esp $esp
    }
}

# 4. Remove the background randomizer scheduled task, if enabled.
if (Get-ScheduledTask -TaskName 'rEFInd_bg_randomizer' -ErrorAction SilentlyContinue) {
    Unregister-ScheduledTask -TaskName 'rEFInd_bg_randomizer' -Confirm:$false
    Write-Host 'Removed the rEFInd_bg_randomizer scheduled task.'
}

# Summary, read back from live NVRAM.
Write-Host ''
Write-Host '==================== Uninstall summary ===================='
$orderIds = @(Get-BootOrderIds)
Write-Host "Firmware boot order: $($orderIds -join ', ')"
foreach ($id in $orderIds) {
    $bytes = Get-UefiVar "Boot$id"
    if ($bytes) { Write-Host ("  Boot{0}: {1}" -f $id, (Get-LoadOptionDescription $bytes)) }
}
if ($removed.Count) {
    Write-Host "Removed entries: $(@($removed | ForEach-Object { "Boot$_" }) -join ', ')"
} else {
    Write-Host 'No Windows-side rEFInd boot entries were present.'
}
Write-Host 'Done.'
