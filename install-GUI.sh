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
# sudo pacman -Syu base-devel
# Install base-devel member packages
yes | sudo pacman -Syu archlinux-keyring autoconf automake binutils bison debugedit fakeroot file findutils flex gawk gcc gettext\
 grep groff gzip libtool m4 make pacman patch pkgconf sed sudo texinfo which
yes | sudo pacman -Syu glibc hwinfo linux-api-headers qt5-base
CURRENT_WD=$(pwd)
mkdir -p /home/deck/.SteamDeck_rEFInd/backgrounds
yes | cp -rf $CURRENT_WD/GUI/ /home/deck/.SteamDeck_rEFInd
yes | cp -rf $CURRENT_WD/icons/ /home/deck/.SteamDeck_rEFInd
yes | cp $CURRENT_WD/themes/background.png /home/deck/.SteamDeck_rEFInd/backgrounds/
yes | cp $CURRENT_WD/{restore_EFI_entries.sh,bootnext-refind.service} /home/deck/.SteamDeck_rEFInd/
yes | cp $CURRENT_WD/{install_config_from_GUI.sh,refind_install_pacman_GUI.sh,refind_install_no_pacman_GUI.sh} /home/deck/.SteamDeck_rEFInd/
yes | cp $CURRENT_WD/refind-GUI.conf /home/deck/.SteamDeck_rEFInd/GUI/refind.conf
chmod +x /home/deck/.SteamDeck_rEFInd/*.sh
chmod +x /home/deck/.SteamDeck_rEFInd/GUI/refind_GUI.desktop
chmod +x $CURRENT_WD/reinstall-GUI.sh
cd /home/deck/.SteamDeck_rEFInd/GUI/src
qmake
make
if [ ! -f /home/deck/.SteamDeck_rEFInd/GUI/src/rEFInd_GUI ]; then
	zenity --error --title="Installation Error" --text="`printf "GUI compile failed.\nPlease try again ensuring that your cloned repo\nis up to date and your pacman config is normal."`" --width=400 2>/dev/null
	sudo steamos-readonly enable
	exit 1
fi
cp rEFInd_GUI ../
sudo steamos-readonly enable
if zenity --question --text="Would you like to copy the Shortcut to the desktop?" --width=400 2>/dev/null; then
	cp /home/deck/.SteamDeck_rEFInd/GUI/refind_GUI.desktop /home/deck/Desktop
	chmod +x /home/deck/Desktop/refind_GUI.desktop
fi
