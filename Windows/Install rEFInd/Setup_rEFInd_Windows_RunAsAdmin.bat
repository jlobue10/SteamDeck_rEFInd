pushd %~dp0
mkdir C:\rEFInd_Scripts
copy ".\Setup_rEFInd_Windows.ps1" C:\rEFInd_Scripts
PowerShell -NoProfile -ExecutionPolicy Bypass -Command "& C:\rEFInd_Scripts\Setup_rEFInd_Windows.ps1"
popd