<#
.SYNOPSIS
    Removes the padding Inno Setup leaves in the setup program's version strings.

.DESCRIPTION
    Inno Setup does not write a version resource of its own. It overwrites the
    strings of a template that is filled with spaces: ProductName has room for
    60 characters, ProductVersion for 50, and whatever VersionInfoProductName
    and VersionInfoProductTextVersion do not use stays behind as spaces. The
    setup program therefore reports "SteamDeck_rEFInd" followed by 44 spaces.

    SignPath compares both values exactly (Windows/GUI/signpath/installer.xml:
    product-name, product-version) and refused the installer with "The file has
    an unexpected product name". This script ends each string of the version
    resource where its text ends: the spaces after it become NUL characters and
    the string's own length field is set to the text's length. Nothing is moved
    and the file keeps its size, so the installer's data behind the program is
    where Inno Setup's loader expects it.

    Run it on the installer after ISCC and before the installer is signed (a
    change after signing would break the signature).

.PARAMETER Path
    The setup program that ISCC wrote.

.PARAMETER ExpectedProductName
    When given, the script fails unless Windows afterwards reports exactly this
    ProductName for the file.

.PARAMETER ExpectedProductVersion
    When given, the script fails unless Windows afterwards reports exactly this
    ProductVersion for the file.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [string]$ExpectedProductName,
    [string]$ExpectedProductVersion
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$file = (Resolve-Path -LiteralPath $Path).ProviderPath
[byte[]]$bytes = [System.IO.File]::ReadAllBytes($file)
$size = $bytes.Length

function Get-U16([int]$at) { [System.BitConverter]::ToUInt16($bytes, $at) }
function Get-U32([int]$at) { [System.BitConverter]::ToUInt32($bytes, $at) }
function Get-Aligned([int]$at) { ($at + 3) -band (-bnot 3) }

# The UTF-16 text at $at up to its NUL, and the offset after that NUL.
function Read-Key([int]$at) {
    $end = $at
    while ((Get-U16 $end) -ne 0) { $end += 2 }
    [pscustomobject]@{
        Text = [System.Text.Encoding]::Unicode.GetString($bytes, $at, $end - $at)
        Next = $end + 2
    }
}

# --- Where the version resource is in the file: PE header, section table,
# --- resource directory (type 16 = RT_VERSION, its first name, its first language).
if ($size -lt 0x40 -or (Get-U16 0) -ne 0x5A4D) { throw "$file is not a Windows program (no MZ header)." }
$pe = [int](Get-U32 0x3C)
if ((Get-U32 $pe) -ne 0x00004550) { throw "$file is not a Windows program (no PE header)." }
$sectionCount = Get-U16 ($pe + 6)
$optional = $pe + 24
$magic = Get-U16 $optional
$directories = if ($magic -eq 0x20B) { $optional + 112 } elseif ($magic -eq 0x10B) { $optional + 96 } else { throw "Unknown PE format 0x$('{0:X}' -f $magic)." }
$resourceRva = Get-U32 ($directories + 2 * 8)
if ($resourceRva -eq 0) { throw "$file has no resources." }
$sections = $optional + (Get-U16 ($pe + 20))

function Convert-RvaToOffset([uint32]$rva) {
    for ($i = 0; $i -lt $sectionCount; $i++) {
        $s = $sections + 40 * $i
        $virtualSize = Get-U32 ($s + 8)
        $virtualAddress = Get-U32 ($s + 12)
        $rawSize = Get-U32 ($s + 16)
        $rawOffset = Get-U32 ($s + 20)
        $span = [Math]::Max($virtualSize, $rawSize)
        if ($rva -ge $virtualAddress -and $rva -lt $virtualAddress + $span) {
            return [int]($rva - $virtualAddress + $rawOffset)
        }
    }
    throw "Address 0x$('{0:X}' -f $rva) is in no section of $file."
}

$resourceRoot = Convert-RvaToOffset $resourceRva
# Written as decimals: PowerShell reads 0x80000000 and 0xFEEF04BD as negative 32-bit numbers.
$highBit = 2147483648            # 0x80000000: the entry leads to another directory
$fixedSignature = 4277077181     # 0xFEEF04BD: VS_FIXEDFILEINFO

# The entry of a resource directory with this id, or its first entry when $id is $null.
function Get-DirectoryEntry([int]$directory, $id) {
    $count = (Get-U16 ($directory + 12)) + (Get-U16 ($directory + 14))
    for ($i = 0; $i -lt $count; $i++) {
        $entry = $directory + 16 + 8 * $i
        if ($null -eq $id -or (Get-U32 $entry) -eq $id) { return (Get-U32 ($entry + 4)) }
    }
    throw "$file has no version resource."
}

$typeEntry = Get-DirectoryEntry $resourceRoot 16
if (($typeEntry -band $highBit) -eq 0) { throw "Unexpected resource directory in $file (type)." }
$nameEntry = Get-DirectoryEntry ($resourceRoot + ($typeEntry -band 0x7FFFFFFF)) $null
if (($nameEntry -band $highBit) -eq 0) { throw "Unexpected resource directory in $file (name)." }
$languageEntry = Get-DirectoryEntry ($resourceRoot + ($nameEntry -band 0x7FFFFFFF)) $null
if (($languageEntry -band $highBit) -ne 0) { throw "Unexpected resource directory in $file (language)." }
$dataEntry = $resourceRoot + $languageEntry
$version = Convert-RvaToOffset (Get-U32 $dataEntry)
$versionSize = [int](Get-U32 ($dataEntry + 4))
if ($version + $versionSize -gt $size) { throw "The version resource of $file ends outside the file." }

# --- VS_VERSIONINFO: header, the fixed part, then StringFileInfo and VarFileInfo.
$rootLength = Get-U16 $version
$fixedLength = Get-U16 ($version + 2)
$rootKey = Read-Key ($version + 6)
if ($rootKey.Text -ne 'VS_VERSION_INFO' -or $rootLength -gt $versionSize) { throw "Unexpected version resource in $file." }
$fixed = Get-Aligned $rootKey.Next
if ($fixedLength -lt 52 -or (Get-U32 $fixed) -ne $fixedSignature) { throw "Unexpected version resource in $file (fixed part)." }
$rootEnd = $version + $rootLength

$trimmed = New-Object System.Collections.Generic.List[string]
$block = Get-Aligned ($fixed + $fixedLength)
while ($block + 6 -le $rootEnd) {
    $blockLength = Get-U16 $block
    if ($blockLength -eq 0) { break }
    $blockKey = Read-Key ($block + 6)
    if ($blockKey.Text -eq 'StringFileInfo') {
        $table = Get-Aligned $blockKey.Next
        while ($table + 6 -le $block + $blockLength) {
            $tableLength = Get-U16 $table
            if ($tableLength -eq 0) { break }
            $tableKey = Read-Key ($table + 6)
            $entry = Get-Aligned $tableKey.Next
            while ($entry + 6 -le $table + $tableLength) {
                $entryLength = Get-U16 $entry
                if ($entryLength -eq 0) { break }
                $valueLength = Get-U16 ($entry + 2)      # in characters, with the NUL
                $entryKey = Read-Key ($entry + 6)
                $value = Get-Aligned $entryKey.Next
                if ($valueLength -gt 0) {
                    if ($value + 2 * $valueLength -gt $entry + $entryLength) { throw "The string $($entryKey.Text) of $file ends outside its entry." }
                    # The text as Windows reads it: up to the first NUL.
                    $length = 0
                    while ($length -lt $valueLength -and (Get-U16 ($value + 2 * $length)) -ne 0) { $length++ }
                    $text = [System.Text.Encoding]::Unicode.GetString($bytes, $value, 2 * $length)
                    $kept = $text.TrimEnd(' ')
                    if ($kept.Length -ne $length) {
                        for ($i = $value + 2 * $kept.Length; $i -lt $value + 2 * $length; $i++) { $bytes[$i] = 0 }
                        [System.BitConverter]::GetBytes([uint16]($kept.Length + 1)).CopyTo($bytes, $entry + 2)
                        $trimmed.Add(('{0}: "{1}" (was {2} characters, {3} of them spaces at the end)' -f $entryKey.Text, $kept, $length, ($length - $kept.Length)))
                    }
                }
                $entry = Get-Aligned ($entry + $entryLength)
            }
            $table = Get-Aligned ($table + $tableLength)
        }
    }
    $block = Get-Aligned ($block + $blockLength)
}

if ($trimmed.Count -gt 0) {
    if ($bytes.Length -ne $size) { throw 'The file changed its size; nothing was written.' }
    [System.IO.File]::WriteAllBytes($file, $bytes)
    Write-Host "Trimmed $($trimmed.Count) version strings of $([System.IO.Path]::GetFileName($file)):"
    $trimmed | ForEach-Object { Write-Host "  $_" }
} else {
    Write-Host "No version string of $([System.IO.Path]::GetFileName($file)) ends in spaces; the file was not changed."
}

# --- What Windows now reports, which is what SignPath compares.
$info = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($file)
Write-Host "ProductName:    [$($info.ProductName)]"
Write-Host "ProductVersion: [$($info.ProductVersion)]"
if ((Get-Item -LiteralPath $file).Length -ne $size) { throw "$file changed its size." }
if ($PSBoundParameters.ContainsKey('ExpectedProductName') -and $info.ProductName -cne $ExpectedProductName) {
    throw "ProductName is [$($info.ProductName)], expected [$ExpectedProductName]."
}
if ($PSBoundParameters.ContainsKey('ExpectedProductVersion') -and $info.ProductVersion -cne $ExpectedProductVersion) {
    throw "ProductVersion is [$($info.ProductVersion)], expected [$ExpectedProductVersion]."
}
