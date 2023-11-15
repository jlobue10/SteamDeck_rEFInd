#!/bin/bash
# A simple script to install the rEFInd customization GUI

echo ""

read -p "Please make sure a sudo password is already set before continuing. If you have not set the user\
 or sudo password, please exit this installer with 'Ctrl+c' and then create a password either using 'passwd'\
 from a command line or by using the KDE Plasma User settings GUI. Otherwise, press Enter/Return to continue with the install."
 
sudo steamos-readonly disable
sudo pacman-key --init
sudo pacman-key --populate archlinux
# sudo pacman -Sy base-devel
# Install base-devel member packages
sudo pacman -Sy --noconfirm archlinux-keyring autoconf automake binutils bison debugedit fakeroot file findutils flex gawk gcc gettext\
 grep groff gzip libtool m4 make pacman patch pkgconf sed sudo texinfo which
sudo pacman -Sy --noconfirm lib32-glibc glibc hwinfo linux-api-headers qt5-base
echo -e "Installing SteamDeck rEFInd...\n"
cd $HOME
sudo rm -rf ./SteamDeck_rEFInd/
git clone https://github.com/jlobue10/SteamDeck_rEFind
cd SteamDeck_rEFind
CURRENT_WD=$(pwd)
mkdir -p $HOME/.SteamDeck_rEFInd/backgrounds
yes | cp -rf $CURRENT_WD/GUI/ $HOME/.SteamDeck_rEFInd
yes | cp -rf $CURRENT_WD/icons/ $HOME/.SteamDeck_rEFInd
yes | cp $CURRENT_WD/themes/background.png $HOME/.SteamDeck_rEFInd/backgrounds/
yes | cp $CURRENT_WD/{restore_EFI_entries.sh,bootnext-refind.service} $HOME/.SteamDeck_rEFInd/
yes | cp $CURRENT_WD/{install_config_from_GUI.sh,refind_install_pacman_GUI.sh,refind_install_no_pacman_GUI.sh} $HOME/.SteamDeck_rEFInd/
yes | cp $CURRENT_WD/refind-GUI.conf $HOME/.SteamDeck_rEFInd/GUI/refind.conf
chmod 755 $HOME/.SteamDeck_rEFInd/*.sh
chmod +x $HOME/.SteamDeck_rEFInd/GUI/refind_GUI.desktop
cd $HOME/.SteamDeck_rEFInd/GUI/src
qmake
make

if [ ! -f $HOME/.SteamDeck_rEFInd/GUI/src/rEFInd_GUI ]; then
	echo -e "\nGUI compile failed. Please try again after ensuring that your cloned repo is up to date and your pacman config is normal.\n"
	sudo steamos-readonly enable
	exit 1
fi

#Create file for passwordless sudo for config file, background and icon installation
#cat > $HOME/.SteamDeck_rEFInd/install_config_from_GUI <<EOF
#$USER ALL = NOPASSWD: $HOME/.SteamDeck_rEFInd/install_config_from_GUI.sh
#EOF

#chmod 0666 $HOME/.SteamDeck_rEFInd/install_config_from_GUI

#sudo cp $HOME/.SteamDeck_rEFInd/install_config_from_GUI /etc/sudoers.d 2>/dev/null

cp -f rEFInd_GUI ../
sudo steamos-readonly enable

cp $HOME/.SteamDeck_rEFInd/GUI/refind_GUI.desktop $HOME/Desktop
chmod +x $HOME/Desktop/refind_GUI.desktop
