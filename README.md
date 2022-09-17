# SteamDeck_rEFInd
This is a simple rEFInd install script for the Steam Deck

This simple install script assumes that the Steam and Windows boot entries are valid and have not been renamed (folders have correct names as well).
If you've followed a guide telling you to rename any EFI files or folders, please revert those changes before running this script.
Icons can be replaced with any 128x128 icons that a user desires. Please just make sure to update the refind.conf file for the correct icon names, if deviating from what's provided here.
The background can also be any 1280 x 800 properly formatted picture. Same as with the icons, if you plan to use a different background, make sure you update the line in refind.conf for your background image.
I recommend making any changes to the icons or background and refind.conf file before running the installation script.

Basic Installation instructions (assuming from a SteamOS command line).

Run git clone https://github.com/jlobue10/SteamDeck_rEFInd/

cd into the SteamDeck_rEFInd directory

chmod +x SteamDeck_rEFInd_install.sh

Then run
./SteamDeck_rEFInd_install.sh
