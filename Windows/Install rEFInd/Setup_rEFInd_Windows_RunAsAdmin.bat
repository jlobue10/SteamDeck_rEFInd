pushd %~dp0
if not exist C:\rEFInd_Scripts mkdir C:\rEFInd_Scripts
REM Setup_rEFInd_Windows.ps1 registers itself as a scheduled task with
REM -RunLevel Highest, so this folder must not be writable by non-admins.
REM Directories created directly under C:\ inherit an ACE granting
REM Authenticated Users modify rights on new subfolders, which would let any
REM local user rewrite the script that then runs elevated at every logon.
REM SIDs, not names, so this works on non-English Windows.
icacls C:\rEFInd_Scripts /inheritance:r /grant:r "*S-1-5-32-544:(OI)(CI)F" /grant:r "*S-1-5-18:(OI)(CI)F" /grant:r "*S-1-5-11:(OI)(CI)RX" >nul
copy ".\Setup_rEFInd_Windows.ps1" C:\rEFInd_Scripts
PowerShell -NoProfile -ExecutionPolicy Bypass -Command "& C:\rEFInd_Scripts\Setup_rEFInd_Windows.ps1"
popd