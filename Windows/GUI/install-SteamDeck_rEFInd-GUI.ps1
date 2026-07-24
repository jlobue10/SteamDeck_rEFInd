#Requires -RunAsAdministrator
# Windows setup script for the rEFInd Customization GUI.
# Installs executable code under Program Files and creates Start Menu / Desktop
# shortcuts. Mutable configuration is initialized under %LOCALAPPDATA% by the
# app on first launch.
param(
    [string]$ExePath = ''
)
$ErrorActionPreference = 'Stop'

# This script lives in <repo>\Windows\GUI, so the repo root is two levels up.
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $ExePath) {
    # Prefer a deployed build dir, fall back to the plain build dir.
    foreach ($candidate in "$repo\GUI\src\build-win\SteamDeck_rEFInd.exe", "$repo\GUI\src\build\SteamDeck_rEFInd.exe") {
        if (Test-Path $candidate) { $ExePath = $candidate; break }
    }
}

$dest = Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'SteamDeck_rEFInd'
New-Item -ItemType Directory -Force $dest | Out-Null

foreach ($d in 'GUI','icons','backgrounds') {
    Copy-Item -Recurse -Force (Join-Path $repo $d) $dest
}
New-Item -ItemType Directory -Force (Join-Path $dest 'windows') | Out-Null
# GUI-build scripts live in Windows\GUI; they ship to the runtime data dir as windows\.
Copy-Item -Force (Join-Path $PSScriptRoot '*.ps1') (Join-Path $dest 'windows')
Copy-Item -Force (Join-Path $repo 'refind-GUI.conf') (Join-Path $dest 'GUI\refind.conf')
& (Join-Path $dest 'windows\rEFInd_bg_randomizer_task.ps1') -Migrate

if ($ExePath -and (Test-Path $ExePath)) {
    $exeDir = Split-Path -Parent $ExePath
    Copy-Item -Force $ExePath (Join-Path $dest 'SteamDeck_rEFInd.exe')
    # Bring along Qt runtime files if windeployqt was run into the build dir.
    Get-ChildItem -Path $exeDir -Filter '*.dll' -File -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item -Force $_.FullName $dest }
    foreach ($sub in 'platforms','styles','imageformats','iconengines','translations') {
        $p = Join-Path $exeDir $sub
        if (Test-Path $p) { Copy-Item -Recurse -Force $p $dest }
    }
} else {
    Write-Warning 'No built SteamDeck_rEFInd.exe found; support files were staged, but you must build the exe (see README) and re-run this script.'
}

$exeTarget = Join-Path $dest 'SteamDeck_rEFInd.exe'
if (Test-Path $exeTarget) {
    $ws = New-Object -ComObject WScript.Shell
    $startMenu = Join-Path ([Environment]::GetFolderPath('StartMenu')) 'Programs'
    foreach ($lnkDir in ([Environment]::GetFolderPath('Desktop')), $startMenu) {
        if (-not (Test-Path $lnkDir)) { continue }
        $lnk = $ws.CreateShortcut((Join-Path $lnkDir 'rEFInd GUI.lnk'))
        $lnk.TargetPath = $exeTarget
        $lnk.WorkingDirectory = $dest
        $lnk.IconLocation = "$exeTarget,0"
        $lnk.Description = 'rEFInd Customization GUI'
        $lnk.Save()
    }
    Write-Host "Installed protected application files to $dest with Desktop and Start Menu shortcuts."
    Write-Host 'Note: the app requests Administrator rights on launch (needed for EFI partition access).'
}
