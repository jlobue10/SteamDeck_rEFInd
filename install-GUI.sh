#!/bin/bash
# A simple script to install the rEFInd customization GUI

echo -e "\nPlease make sure a sudo password is already set before continuing. If you have not set the user\
 or sudo password, please exit this installer with 'Ctrl+c' and then create a password either using 'passwd'\
 from a command line or by using the KDE Plasma User settings GUI.\n"
 
sudo steamos-readonly disable
sudo pacman-key --init
sudo pacman-key --populate archlinux
# sudo pacman -Sy base-devel
# Install base-devel member packages
sudo pacman -Sy --noconfirm archlinux-keyring autoconf automake binutils bison debugedit fakeroot file findutils flex gawk gcc gettext\
 grep groff gzip libtool m4 make pacman patch pkgconf sed sudo texinfo which
sudo pacman -Sy --noconfirm glibc hwinfo linux-api-headers qt5-base
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
	echo -e "\nGUI compile failed. Please try again after ensuring that your cloned repo is up to date and your pacman config is normal.\n"
	sudo steamos-readonly enable
	exit 1
fi

cp rEFInd_GUI ../
sudo steamos-readonly enable

while true; do
	read -p "Do you want to copy the rEFInd_GUI icon to the desktop? (y/n) " YN
	case $YN in 
		[yY]) echo -e "\nOk, icon will be copied to the desktop.\n"
			cp /home/deck/.SteamDeck_rEFInd/GUI/refind_GUI.desktop /home/deck/Desktop
			chmod +x /home/deck/Desktop/refind_GUI.desktop
			break;;
		[nN]) echo -e "\nIcon will not be copied to the desktop.\n"
			exit 1;;
		*) echo -e "\nInvalid response.\n";;
	esac
done
