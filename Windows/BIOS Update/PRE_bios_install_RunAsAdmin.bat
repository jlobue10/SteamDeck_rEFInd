@echo off
pushd "%~dp0"
REM These scripts run elevated, so C:\rEFInd_Scripts must not be writable,
REM ownable, or redirectable by non-admins. Directories created directly under
REM C:\ inherit an ACE granting Authenticated Users modify rights, and a
REM non-admin can pre-create the folder to become owner or plant a junction.
REM Delete-and-recreate to guarantee we are the creator, refuse a reparse point
REM up front, and fail closed if the create races. SIDs, not names, so this
REM works on non-English Windows.
if exist C:\rEFInd_Scripts (
  fsutil reparsepoint query C:\rEFInd_Scripts >nul 2>&1 && (
    echo ERROR: C:\rEFInd_Scripts already exists as a junction/symlink. Refusing to continue.
    echo Remove it manually after verifying where it points, then re-run.
    popd
    exit /b 1
  )
  rmdir /s /q C:\rEFInd_Scripts
)
mkdir C:\rEFInd_Scripts || (
  echo ERROR: could not create C:\rEFInd_Scripts ^(it may have been recreated by another process^). Aborting.
  popd
  exit /b 1
)
icacls C:\rEFInd_Scripts /setowner "*S-1-5-32-544" >nul
icacls C:\rEFInd_Scripts /inheritance:r /grant:r "*S-1-5-32-544:(OI)(CI)F" /grant:r "*S-1-5-18:(OI)(CI)F" /grant:r "*S-1-5-11:(OI)(CI)RX" >nul
copy /y ".\PRE_bios_install_Windows.ps1" C:\rEFInd_Scripts >nul
PowerShell -NoProfile -ExecutionPolicy Bypass -Command "& C:\rEFInd_Scripts\PRE_bios_install_Windows.ps1"
popd
