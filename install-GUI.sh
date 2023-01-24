#!/bin/bash
# A simple script to install the rEFInd customization GUI

PASSWD="$(zenity --password --title="Enter sudo password" 2>/dev/null)"
echo "$PASSWD" | sudo -v -S
ANS=$?
if [[ $ANS == 1 ]]; then
	zenity --error --title="Password Error" --text="`printf "Incorrect password provided.\nPlease try again providing the correct sudo password."`" --width=400 2>/dev/null
	exit 1
fi

sudo steamos-readonly disable
sudo pacman-key --init
sudo pacman-key --populate archlinux
sudo pacman -Syu base-devel
yes | sudo pacman -Syu glibc
yes | sudo pacman -Syu hwinfo
yes | sudo pacman -Syu linux-api-headers
yes | sudo pacman -Syu qt5-base
CURRENT_WD=$(pwd)
mkdir -p /home/deck/.SteamDeck_rEFInd/backgrounds
yes | cp -rf $CURRENT_WD/GUI/ /home/deck/.SteamDeck_rEFInd
yes | cp -rf $CURRENT_WD/icons/ /home/deck/.SteamDeck_rEFInd
yes | cp $CURRENT_WD/themes/background.png /home/deck/.SteamDeck_rEFInd/backgrounds/
yes | cp $CURRENT_WD/{restore_EFI_entries.sh,bootnext-refind.service} /home/deck/.SteamDeck_rEFInd/
yes | cp $CURRENT_WD/{install_config_from_GUI.sh,refind_install_pacman_GUI.sh,refind_install_no_pacman_GUI.sh} /home/deck/.SteamDeck_rEFInd/
yes | cp $CURRENT_WD/refind-GUI.conf /home/deck/.SteamDeck_rEFInd/GUI/refind.conf
chmod +x /home/deck/.SteamDeck_rEFInd/*.sh
chmod +x /home/deck/.SteamDeck_rEFInd/GUI/rEFInd_GUI.desktop
cd /home/deck/.SteamDeck_rEFInd/GUI/src
qmake
make
cp rEFInd_GUI ../
sudo steamos-readonly enable
