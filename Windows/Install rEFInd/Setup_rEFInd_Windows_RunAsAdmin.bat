@echo off
pushd "%~dp0"
REM Setup_rEFInd_Windows.ps1 registers itself as a scheduled task with
REM -RunLevel Highest, so C:\rEFInd_Scripts must not be writable, ownable, or
REM redirectable by non-admins. Directories created directly under C:\ inherit
REM an ACE granting Authenticated Users modify rights on new subfolders, and a
REM non-admin can pre-create the folder to become its owner (owner keeps
REM implicit WRITE_DAC regardless of DACL) or plant a junction. icacls
REM /inheritance:r /grant:r alone fixes neither ownership nor a pre-existing
REM explicit ACE, so we delete-and-recreate to guarantee we are the creator,
REM refuse a reparse point up front, and fail closed if the create races.
REM SIDs, not names, so this works on non-English Windows.
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
copy /y ".\Setup_rEFInd_Windows.ps1" C:\rEFInd_Scripts >nul
PowerShell -NoProfile -ExecutionPolicy Bypass -Command "& C:\rEFInd_Scripts\Setup_rEFInd_Windows.ps1"
popd
