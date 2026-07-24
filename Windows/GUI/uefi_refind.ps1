# Locale-independent helpers for locating the ESP referenced by the active
# rEFInd firmware boot entry. These functions read the standard UEFI variables
# directly instead of parsing localized bcdedit output.

$script:EfiGlobalGuid = '{8be4df61-93ca-11d2-aa0d-00e098032b8c}'

if (-not ('RefindFirmware.Native' -as [type])) {
    Add-Type -Namespace RefindFirmware -Name Native -MemberDefinition @'
[DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
public static extern uint GetFirmwareEnvironmentVariableW(string name, string guid, byte[] value, uint size);
[DllImport("advapi32.dll", SetLastError=true)]
public static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
[DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
public static extern bool LookupPrivilegeValueW(string system, string name, out long luid);
[DllImport("advapi32.dll", SetLastError=true)]
public static extern bool AdjustTokenPrivileges(IntPtr token, bool disableAll, ref TOKPRIV state, int length, IntPtr previous, IntPtr returned);
[DllImport("kernel32.dll")]
public static extern IntPtr GetCurrentProcess();
[DllImport("kernel32.dll")]
public static extern bool CloseHandle(IntPtr handle);
[StructLayout(LayoutKind.Sequential, Pack=4)]
public struct TOKPRIV { public uint Count; public long Luid; public uint Attr; }
'@
}

function Enable-RefindFirmwarePrivilege {
    $token = [IntPtr]::Zero
    if (-not [RefindFirmware.Native]::OpenProcessToken(
            [RefindFirmware.Native]::GetCurrentProcess(), 0x28, [ref]$token)) {
        throw "OpenProcessToken failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }
    try {
        $luid = 0L
        if (-not [RefindFirmware.Native]::LookupPrivilegeValueW(
                $null, 'SeSystemEnvironmentPrivilege', [ref]$luid)) {
            throw "LookupPrivilegeValue failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
        }
        $state = New-Object RefindFirmware.Native+TOKPRIV
        $state.Count = 1
        $state.Luid = $luid
        $state.Attr = 2
        if (-not [RefindFirmware.Native]::AdjustTokenPrivileges(
                $token, $false, [ref]$state, 0, [IntPtr]::Zero, [IntPtr]::Zero)) {
            throw "AdjustTokenPrivileges failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
        }
    } finally {
        [RefindFirmware.Native]::CloseHandle($token) | Out-Null
    }
}

function Get-RefindFirmwareVariable([string]$Name) {
    for ($size = 1024; $size -le 1MB; $size *= 2) {
        $buffer = New-Object byte[] $size
        $length = [RefindFirmware.Native]::GetFirmwareEnvironmentVariableW(
            $Name, $script:EfiGlobalGuid, $buffer, $buffer.Length)
        if ($length -gt 0) {
            return ,$buffer[0..($length - 1)]
        }
        $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        if ($errorCode -eq 122) { continue }
        if ($errorCode -eq 2 -or $errorCode -eq 203) { return $null }
        throw "Reading UEFI variable $Name failed with Win32 error $errorCode."
    }
    throw "UEFI variable $Name exceeds the 1 MiB safety limit."
}

function Get-RefindBootOrderIds {
    $order = Get-RefindFirmwareVariable 'BootOrder'
    if (-not $order) { return @() }
    return @(for ($i = 0; $i + 1 -lt $order.Length; $i += 2) {
        '{0:X4}' -f ($order[$i] + ($order[$i + 1] * 256))
    })
}

function Get-RefindBootPartitionGuid {
    Enable-RefindFirmwarePrivilege
    foreach ($id in (Get-RefindBootOrderIds)) {
        $bytes = Get-RefindFirmwareVariable "Boot$id"
        if (-not $bytes -or $bytes.Length -lt 10) { continue }

        $descriptionEnd = 6
        while ($descriptionEnd + 1 -lt $bytes.Length) {
            if ($bytes[$descriptionEnd] -eq 0 -and $bytes[$descriptionEnd + 1] -eq 0) {
                $descriptionEnd += 2
                break
            }
            $descriptionEnd += 2
        }
        if ($descriptionEnd + 4 -gt $bytes.Length) { continue }

        $pathLength = [BitConverter]::ToUInt16($bytes, 4)
        $pathEnd = [Math]::Min($bytes.Length, $descriptionEnd + $pathLength)
        $partitionGuid = $null
        $loaderPath = $null
        for ($offset = $descriptionEnd; $offset + 4 -le $pathEnd;) {
            $nodeLength = [BitConverter]::ToUInt16($bytes, $offset + 2)
            if ($nodeLength -lt 4 -or $offset + $nodeLength -gt $pathEnd) { break }
            $type = $bytes[$offset]
            $subType = $bytes[$offset + 1]
            if ($type -eq 4 -and $subType -eq 1 -and
                $nodeLength -ge 42 -and $bytes[$offset + 41] -eq 2) {
                $guidBytes = New-Object byte[] 16
                [Array]::Copy($bytes, $offset + 24, $guidBytes, 0, 16)
                $partitionGuid = New-Object Guid (,$guidBytes)
            } elseif ($type -eq 4 -and $subType -eq 4) {
                $loaderPath = [Text.Encoding]::Unicode.GetString(
                    $bytes, $offset + 4, $nodeLength - 4).TrimEnd([char]0)
            }
            $offset += $nodeLength
        }
        if ($partitionGuid -and
            $loaderPath -match '^\\EFI\\refind\\refind[^\\]*\.efi$') {
            return $partitionGuid
        }
    }
    return $null
}
