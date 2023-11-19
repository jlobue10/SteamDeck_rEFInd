#!/bin/bash

# A simple script to install the rEFInd customization GUI
sudo steamos-readonly disable
echo -e "Installing SteamDeck rEFInd...\n"
cd $HOME
# sudo rm -rf $HOME/SteamDeck_rEFInd/
git clone https://github.com/jlobue10/SteamDeck_rEFind
cd SteamDeck_rEFind
CURRENT_WD=$(pwd)
mkdir -p $HOME/.local/SteamDeck_rEFInd
cp -rf $CURRENT_WD/GUI/ $HOME/.local/SteamDeck_rEFInd
cp -rf $CURRENT_WD/icons/ $HOME/.local/SteamDeck_rEFInd
cp -rf $CURRENT_WD/backgrounds/ $HOME/.local/SteamDeck_rEFInd
cp -f $CURRENT_WD/refind-GUI.conf $HOME/.local/SteamDeck_rEFInd/GUI/refind.conf

# Thanks to Maclay74 steaam-patch for the following syntax
RELEASE=$(curl -s 'https://api.github.com/repos/jlobue10/SteamDeck_rEFInd/releases' | jq -r "first(.[] | select(.prerelease == "false"))")
VERSION=$(jq -r '.tag_name' <<< ${RELEASE} )
DOWNLOAD_URL=$(jq -r '.assets[].browser_download_url | select(endswith("x86_64.pkg.tar.zst"))' <<< ${RELEASE})

printf "Installing version %s...\n" "${VERSION}"
wget $DOWNLOAD_URL

sudo pacman -Qs SteamDeck_rEFInd
STEAMDECK_REFIND_STATUS=$?
if [ $STEAMDECK_REFIND_STATUS == 0 ]; then
    	sudo pacman -R --noconfirm SteamDeck_rEFInd
fi

INSTALL_PKG="$(ls | grep pkg.tar.zst)"
sudo pacman -U --noconfirm $INSTALL_PKG

# Leaving passwordless sudo stuff to try to fix another day...
#Create file for passwordless sudo for config file, background and icon installation
#cat > $HOME/.SteamDeck_rEFInd/install_config_from_GUI <<EOF
#$USER ALL = NOPASSWD: $HOME/.SteamDeck_rEFInd/install_config_from_GUI.sh
#EOF

#chmod 0666 $HOME/.SteamDeck_rEFInd/install_config_from_GUI

#sudo cp $HOME/.SteamDeck_rEFInd/install_config_from_GUI /etc/sudoers.d 2>/dev/null

sudo steamos-readonly enable

cp -f /usr/share/applications/SteamDeck_rEFInd.desktop $HOME/Desktop/SteamDeck_rEFInd.desktop
chmod +x $HOME/Desktop/SteamDeck_rEFInd.desktop
echo -e "Installation complete...\n"
