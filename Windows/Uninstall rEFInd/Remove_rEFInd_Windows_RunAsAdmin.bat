pushd %~dp0
if not exist C:\rEFInd_Scripts mkdir C:\rEFInd_Scripts
REM Admin-only ACL: folders created directly under C:\ inherit an ACE granting
REM Authenticated Users modify rights, and these scripts are run elevated.
REM SIDs, not names, so this works on non-English Windows.
icacls C:\rEFInd_Scripts /inheritance:r /grant:r "*S-1-5-32-544:(OI)(CI)F" /grant:r "*S-1-5-18:(OI)(CI)F" /grant:r "*S-1-5-11:(OI)(CI)RX" >nul
copy ".\Remove_rEFInd_Windows.ps1" C:\rEFInd_Scripts
PowerShell -NoProfile -ExecutionPolicy Bypass -Command "& C:\rEFInd_Scripts\Remove_rEFInd_Windows.ps1"
popd