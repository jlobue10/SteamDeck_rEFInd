pushd %~dp0
mkdir C:\rEFInd_Scripts
copy ".\POST_bios_install_Windows.ps1" C:\rEFInd_Scripts
PowerShell -NoProfile -ExecutionPolicy Bypass -Command "& C:\rEFInd_Scripts\POST_bios_install_Windows.ps1"
popd