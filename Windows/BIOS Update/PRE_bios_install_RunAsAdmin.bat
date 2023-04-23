pushd %~dp0
mkdir C:\rEFInd_Scripts
copy ".\PRE_bios_install_Windows.ps1" C:\rEFInd_Scripts
PowerShell -NoProfile -ExecutionPolicy Bypass -Command "& C:\rEFInd_Scripts\PRE_bios_install_Windows.ps1"
popd