#Requires -RunAsAdministrator
# Read-only diagnostic for "Install Config succeeds but nothing changes at
# boot" (v3.4.0 native-helper flow) -- the Windows counterpart of
# scripts/diagnose_install_config.sh. Gathers, in one elevated run:
#
#   1. every Boot#### NVRAM entry (description, whether its loader path looks
#      like rEFInd, its GPT partition GUID) and which entry the in-process
#      resolver's two tiers would match,
#   2. every ESP-typed partition, with timestamps + SHA-256 of each copy of
#      EFI\refind\refind.conf and any fallback EFI\BOOT\bootx64.efi,
#   3. the staged source config under %LOCALAPPDATA%, flagged MATCHES/DIFFERS
#      against each ESP copy,
#   4. the tail of the GUI's install log.
#
# It never writes NVRAM or any ESP; letterless partitions are mounted on a
# private temp directory (no drive letter consumed) and unmounted again.
# Run from an elevated PowerShell:
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\diagnose_install_config.ps1
#
# and attach the full output to the issue/report.
$ErrorActionPreference = 'Stop'

$EspGuid = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
$EfiGlobalGuid = '{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}'
$DataDir = Join-Path $env:LOCALAPPDATA 'SteamDeck_rEFInd'
$SrcConf = Join-Path $DataDir 'GUI\refind.conf'

function Write-Section([string]$title) { Write-Host "`n===== $title =====" }

# mountvol reports failure on stderr, which Windows PowerShell 5.1 turns into a
# terminating RemoteException when redirected under ErrorActionPreference Stop.
function Invoke-Mountvol([string[]]$mvArgs) {
    $eap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { mountvol @mvArgs 2>$null | Out-Null } finally { $ErrorActionPreference = $eap }
    return $LASTEXITCODE
}

# ---- UEFI NVRAM access (read-only here); same block as uninstall_rEFInd.ps1 ----
Add-Type -Namespace RefindUefi -Name Native -MemberDefinition @'
[DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
public static extern uint GetFirmwareEnvironmentVariableW(string lpName, string lpGuid, byte[] pBuffer, uint nSize);
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
    for ($size = 1024; $size -le 1MB; $size *= 2) {
        $buf = New-Object byte[] $size
        $n = [RefindUefi.Native]::GetFirmwareEnvironmentVariableW($name, $EfiGlobalGuid, $buf, $buf.Length)
        if ($n -gt 0) { return ,$buf[0..($n - 1)] }
        $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        if ($errorCode -eq 122) { continue }
        if ($errorCode -eq 2 -or $errorCode -eq 203) { return $null }
        throw "Reading UEFI variable $name failed with Win32 error $errorCode."
    }
    throw "UEFI variable $name exceeds the 1 MiB safety limit."
}

function Get-BootOrderIds {
    $bo = Get-UefiVar 'BootOrder'
    if (-not $bo) { return @() }
    return @(for ($i = 0; $i + 1 -lt $bo.Length; $i += 2) { '{0:X4}' -f ($bo[$i] + ($bo[$i + 1] * 256)) })
}

# Minimal EFI_LOAD_OPTION parse, mirroring espops/loadoption.cpp: description,
# the first GPT hard-drive node's partition GUID, and the first file-path node.
function Read-LoadOption([byte[]]$bytes) {
    $r = [pscustomobject]@{ Description = ''; PartitionGuid = ''; LoaderPath = '' }
    if (-not $bytes -or $bytes.Length -lt 8) { return $r }
    $filePathListLength = $bytes[4] + ($bytes[5] * 256)
    $i = 6
    while ($i + 1 -lt $bytes.Length) {
        $ch = $bytes[$i] + ($bytes[$i + 1] * 256)
        $i += 2
        if ($ch -eq 0) { break }
        $r.Description += [char]$ch
    }
    $pathsEnd = $i + $filePathListLength
    if ($pathsEnd -gt $bytes.Length) { return $r }
    while ($i + 4 -le $pathsEnd) {
        $type = $bytes[$i]; $subType = $bytes[$i + 1]
        $len = $bytes[$i + 2] + ($bytes[$i + 3] * 256)
        if ($len -lt 4 -or $i + $len -gt $pathsEnd) { break }
        if ($type -eq 4 -and $subType -eq 1 -and $len -ge 42 -and
            $bytes[$i + 41] -eq 2 -and -not $r.PartitionGuid) {
            # EFI GUIDs: first three fields little-endian, last 8 bytes verbatim.
            $g = $bytes[($i + 24)..($i + 39)]
            # [uint32] cast: the byte arithmetic overflows Int32 into Double,
            # and the x8 format specifier rejects Double.
            $r.PartitionGuid = ('{0:x8}-{1:x4}-{2:x4}-{3:x2}{4:x2}-{5:x2}{6:x2}{7:x2}{8:x2}{9:x2}{10:x2}' -f `
                [uint32]([uint32]$g[0] + ([uint32]$g[1] * 256) + ([uint32]$g[2] * 65536) + ([uint32]$g[3] * 16777216)),
                ($g[4] + ($g[5] * 256)), ($g[6] + ($g[7] * 256)),
                $g[8], $g[9], $g[10], $g[11], $g[12], $g[13], $g[14], $g[15])
        } elseif ($type -eq 4 -and $subType -eq 4 -and -not $r.LoaderPath) {
            $j = $i + 4
            while ($j + 1 -lt $i + $len) {
                $ch = $bytes[$j] + ($bytes[$j + 1] * 256)
                if ($ch -eq 0) { break }
                $r.LoaderPath += [char]$ch
                $j += 2
            }
        } elseif ($type -eq 0x7F -and $subType -eq 0xFF) {
            break
        }
        $i += $len
    }
    return $r
}

function Test-RefindLoaderPath([string]$p) {
    return ($p -replace '/', '\') -imatch '\\EFI\\refind\\refind[^\\]*\.efi'
}

Write-Section 'system'
Get-Date
(Get-CimInstance Win32_ComputerSystemProduct).Name
$app = Join-Path ${env:ProgramFiles} 'SteamDeck_rEFInd'
foreach ($exe in 'SteamDeck_rEFInd.exe', 'SteamDeck_rEFInd_helper.exe') {
    $p = Join-Path $app $exe
    if (Test-Path -LiteralPath $p) {
        $vi = (Get-Item -LiteralPath $p).VersionInfo
        Write-Host ("{0}: present (file version {1})" -f $exe, $vi.FileVersion)
    } else {
        Write-Host "${exe}: MISSING at $p"
    }
}
Write-Host "data dir: $DataDir"

Write-Section 'firmware boot entries'
Enable-UefiPrivilege
$orderIds = Get-BootOrderIds
Write-Host ("BootOrder: {0}" -f ($(if ($orderIds) { $orderIds -join ',' } else { '(unreadable or empty)' })))
$candidateIds = @($orderIds + @(0..255 | ForEach-Object { '{0:X4}' -f $_ }) | Select-Object -Unique)
$entries = @()
foreach ($id in $candidateIds) {
    $bytes = Get-UefiVar "Boot$id"
    if (-not $bytes) { continue }
    $o = Read-LoadOption $bytes
    $entries += [pscustomobject]@{ Id = $id; Desc = $o.Description; Loader = $o.LoaderPath; Guid = $o.PartitionGuid }
    Write-Host ("Boot{0}  '{1}'  loader='{2}'  partition={3}" -f $id, $o.Description, $o.LoaderPath, $o.PartitionGuid)
}

Write-Section "resolver NVRAM match (what the GUI's tier 1 would pick)"
$matchGuid = ''
foreach ($tier in 'loader', 'label') {
    foreach ($e in $entries) {
        if (-not $e.Guid) { continue }
        $hit = if ($tier -eq 'loader') { Test-RefindLoaderPath $e.Loader } else { $e.Desc -ceq 'rEFInd' }
        if ($hit) {
            Write-Host ("tier {0} matched: Boot{1} '{2}' -> partition {3}" -f $tier, $e.Id, $e.Desc, $e.Guid)
            $matchGuid = $e.Guid
            break
        }
    }
    if ($matchGuid) { break }
}
if (-not $matchGuid) { Write-Host 'no rEFInd entry matched by loader path or exact label' }

Write-Section "staged source config ($DataDir\GUI)"
if (Test-Path -LiteralPath $SrcConf) {
    Get-ChildItem -LiteralPath (Join-Path $DataDir 'GUI') -File |
        Where-Object { $_.Name -match '^(refind\.conf|background\.png|os_icon\d\.png|active_theme\.conf)$' } |
        Format-Table Name, Length, LastWriteTime -AutoSize | Out-Host
    Write-Host ("SHA256 {0}" -f (Get-FileHash -Algorithm SHA256 -LiteralPath $SrcConf).Hash.ToLower())
    Write-Host '-- menuentry/include/showtools lines:'
    Select-String -LiteralPath $SrcConf -Pattern '^\s*(menuentry|include|showtools|default_selection|resolution)' |
        ForEach-Object { Write-Host ("  {0}: {1}" -f $_.LineNumber, $_.Line) }
} else {
    Write-Host 'NO staged refind.conf -- Create Config has not run for this user'
}

# Every ESP-typed partition: where each copy of the config lives and whether
# it matches the staged source.
$tempMounts = @()
try {
    foreach ($part in @(Get-Partition | Where-Object { $_.GptType -eq $EspGuid })) {
        Write-Section ("ESP disk {0} partition {1} (GUID {2})" -f $part.DiskNumber, $part.PartitionNumber, $part.Guid)
        $partGuidBare = ([guid]$part.Guid).ToString().ToLower()
        if ($matchGuid -and $partGuidBare -eq $matchGuid) {
            Write-Host "*** this is the ESP the firmware's rEFInd entry points at ***"
        }
        # Letterless partitions serialize DriveLetter as a NUL char, not empty.
        $root = ''
        if ($part.DriveLetter -and [int][char]$part.DriveLetter -ge 65) {
            $root = "$($part.DriveLetter):"
            Write-Host "already mounted at $root"
        } else {
            $volName = @($part.AccessPaths) | Where-Object { $_ -like '\\?\Volume*' } | Select-Object -First 1
            if (-not $volName) { Write-Host 'no volume access path (skipped)'; continue }
            $dir = Join-Path $env:TEMP ("sdr-diag-" + [guid]::NewGuid().ToString('N'))
            New-Item -ItemType Directory -Path $dir | Out-Null
            if ((Invoke-Mountvol @($dir, $volName)) -eq 0) {
                $tempMounts += $dir
                $root = $dir
                Write-Host "probe-mounted at $dir ($volName)"
            } else {
                Remove-Item -LiteralPath $dir -Force -ErrorAction SilentlyContinue
                Write-Host 'could not mount (skipped)'
                continue
            }
        }
        $refindDir = Join-Path $root 'EFI\refind'
        if (Test-Path -LiteralPath $refindDir) {
            Get-ChildItem -LiteralPath $refindDir -File -ErrorAction SilentlyContinue |
                Select-Object -First 25 | Format-Table Name, Length, LastWriteTime -AutoSize | Out-Host
            $espConf = Join-Path $refindDir 'refind.conf'
            if (Test-Path -LiteralPath $espConf) {
                $espHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $espConf).Hash.ToLower()
                Write-Host "SHA256 $espHash"
                if (Test-Path -LiteralPath $SrcConf) {
                    $srcHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $SrcConf).Hash.ToLower()
                    if ($espHash -eq $srcHash) {
                        Write-Host '>>> refind.conf here MATCHES the staged source'
                    } else {
                        # Compare again with line endings normalized:
                        # pre-3.4.1 Windows builds staged CRLF, so a
                        # CRLF-only difference means the other OS's GUI
                        # installed the same config -- content-wise a
                        # match, and exactly the cross-GUI overwrite the
                        # origin sidecar below attributes.
                        $espText = ([System.IO.File]::ReadAllText($espConf)) -replace "`r", ''
                        $srcText = ([System.IO.File]::ReadAllText($SrcConf)) -replace "`r", ''
                        if ($espText -ceq $srcText) {
                            Write-Host ">>> refind.conf here MATCHES the staged source except line endings (same config installed by the other OS's GUI)"
                        } else {
                            Write-Host '>>> refind.conf here DIFFERS from the staged source'
                        }
                    }
                }
                # Who last installed this copy (written by >= 3.4.1 installers).
                $originFile = Join-Path $refindDir 'refind.conf.origin'
                if (Test-Path -LiteralPath $originFile) {
                    Write-Host '-- refind.conf.origin:'
                    Get-Content -LiteralPath $originFile | Where-Object { $_ -notmatch '^#' } | Write-Host
                } else {
                    Write-Host 'no refind.conf.origin (installed by a pre-3.4.1 version, or hand-placed)'
                }
            } else {
                Write-Host 'no refind.conf in EFI\refind'
            }
            $activeTheme = Join-Path $refindDir 'themes\active_theme.conf'
            if (Test-Path -LiteralPath $activeTheme) {
                Get-Item -LiteralPath $activeTheme | Format-Table Name, Length, LastWriteTime -AutoSize | Out-Host
            }
        } else {
            Write-Host 'no EFI\refind directory'
        }
        # A rEFInd parked on the fallback path boots without any Boot#### entry
        # and reads EFI\BOOT\refind.conf -- a write to EFI\refind never reaches it.
        $fallback = Join-Path $root 'EFI\BOOT\bootx64.efi'
        if (Test-Path -LiteralPath $fallback) {
            Get-ChildItem -LiteralPath (Join-Path $root 'EFI\BOOT') -File -ErrorAction SilentlyContinue |
                Select-Object -First 10 | Format-Table Name, Length, LastWriteTime -AutoSize | Out-Host
            $probe = [System.IO.File]::ReadAllBytes($fallback)
            $text = [System.Text.Encoding]::ASCII.GetString($probe)
            if ($text -imatch 'refind') {
                Write-Host '>>> EFI\BOOT\bootx64.efi looks like a rEFInd binary (fallback-path install)'
            }
        }
    }
} finally {
    foreach ($dir in $tempMounts) {
        $null = Invoke-Mountvol @($dir, '/D')
        Remove-Item -LiteralPath $dir -Force -ErrorAction SilentlyContinue
    }
}

Write-Section 'GUI install log (last 40 lines)'
$logDir = Join-Path $DataDir 'GUI\logs'
if (Test-Path -LiteralPath $logDir) {
    $latest = Get-ChildItem -LiteralPath $logDir -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($latest) {
        Write-Host "-- $($latest.FullName):"
        Get-Content -LiteralPath $latest.FullName -Tail 40
    }
} else {
    Write-Host "no log directory at $logDir"
}

Write-Section 'done'
Write-Host 'Attach everything above to the report.'
