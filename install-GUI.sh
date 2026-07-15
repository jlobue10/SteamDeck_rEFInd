#!/bin/bash

# A simple script to install the rEFInd customization GUI
sudo steamos-readonly disable
# Make sure readonly gets re-enabled even if the script aborts partway through
trap 'sudo steamos-readonly enable' EXIT
echo -e "Installing SteamDeck rEFInd...\n"
CURRENT_WD=$(pwd)
mkdir -p "$HOME/.local/SteamDeck_rEFInd"
cp -rf "$CURRENT_WD/GUI/" "$HOME/.local/SteamDeck_rEFInd"
cp -rf "$CURRENT_WD/icons/" "$HOME/.local/SteamDeck_rEFInd"
cp -rf "$CURRENT_WD/backgrounds/" "$HOME/.local/SteamDeck_rEFInd"
cp -rf "$CURRENT_WD/scripts/" "$HOME/.local/SteamDeck_rEFInd"
cp -f "$CURRENT_WD/refind-GUI.conf" "$HOME/.local/SteamDeck_rEFInd/GUI/refind.conf"
# Shortcut inside GUI/ (the folder the app's Open Folder button shows) to the
# backgrounds folder the randomizer picks from.
ln -sfn ../backgrounds "$HOME/.local/SteamDeck_rEFInd/GUI/backgrounds"
chmod +x "$HOME"/.local/SteamDeck_rEFInd/scripts/*.sh

#Clean up old installation...
if [ -d "$HOME/.SteamDeck_rEFInd" ]; then
    rm -rf "$HOME/.SteamDeck_rEFInd"
fi

#Clean up old icon...
if [ -f "$HOME/Desktop/refind_GUI.desktop" ]; then
    rm -f "$HOME/Desktop/refind_GUI.desktop"
fi

# Thanks to Maclay74 steam-patch for the following syntax
RELEASE=$(curl -s 'https://api.github.com/repos/jlobue10/SteamDeck_rEFInd/releases' | jq -r 'first(.[] | select(.prerelease == false))')
VERSION=$(jq -r '.tag_name' <<< "${RELEASE}")
# Releases also carry a -debug- split package (symbols only); install only the
# regular package.
DOWNLOAD_URL=$(jq -r 'first(.assets[].browser_download_url | select(endswith("x86_64.pkg.tar.zst") and (contains("-debug-") | not)))' <<< "${RELEASE}")

if [ -z "$DOWNLOAD_URL" ] || [ "$DOWNLOAD_URL" == "null" ]; then
    echo "Error: could not determine a release download URL from the GitHub API. Aborting." >&2
    exit 1
fi

printf "Installing version %s...\n" "${VERSION}"
INSTALL_PKG="$(basename "$DOWNLOAD_URL")"
wget -O "$INSTALL_PKG" "$DOWNLOAD_URL"
if [ $? -ne 0 ] || [ ! -s "$INSTALL_PKG" ]; then
    echo "Error: failed to download $DOWNLOAD_URL. Aborting." >&2
    exit 1
fi

if pacman -Qs SteamDeck_rEFInd > /dev/null; then
    sudo pacman -R --noconfirm SteamDeck_rEFInd
fi

if [ -f /etc/systemd/system/bootnext-refind.service ]; then
    sudo systemctl disable --now bootnext-refind.service
    # Force removing old service file from previous versions
    echo -e "\nRemoving old bootnext-refind.service\n"
    sudo rm /etc/systemd/system/bootnext-refind.service
fi

if [ -f /etc/systemd/system/rEFInd_bg_randomizer.service ]; then
    sudo systemctl disable --now rEFInd_bg_randomizer.service
    # Force removing old service file from previous versions
    echo -e "\nRemoving old rEFInd_bg_randomizer.service\n"
    sudo rm /etc/systemd/system/rEFInd_bg_randomizer.service
fi

# The package's post_install scriptlet handles daemon-reload plus enabling and
# starting bootnext-refind.service.
if ! sudo pacman -U --noconfirm "$INSTALL_PKG"; then
    echo "Error: pacman failed to install $INSTALL_PKG. Aborting." >&2
    exit 1
fi
rm -f "$INSTALL_PKG"

# Leaving passwordless sudo stuff to try to fix another day...
#Create file for passwordless sudo for config file, background and icon installation
#cat > $HOME/.local/SteamDeck_rEFInd/install_config_from_GUI <<EOF
#$USER ALL = NOPASSWD: /usr/bin/install_config_from_GUI.sh
#EOF

#chmod 0666 $HOME/.local/SteamDeck_rEFInd/install_config_from_GUI

#sudo cp $HOME/.local/SteamDeck_rEFInd/install_config_from_GUI /etc/sudoers.d 2>/dev/null

sudo steamos-readonly enable

cp -f /usr/bin/SteamDeck_rEFInd "$HOME/.local/SteamDeck_rEFInd/GUI/SteamDeck_rEFInd"
cp -f /usr/share/applications/SteamDeck_rEFInd.desktop "$HOME/.local/SteamDeck_rEFInd/GUI/SteamDeck_rEFInd.desktop"
# Desktop file ships with /home/deck hardcoded; rewrite it for the actual user's home
sed -i "s|/home/deck|$HOME|g" "$HOME/.local/SteamDeck_rEFInd/GUI/SteamDeck_rEFInd.desktop"
cp -f "$HOME/.local/SteamDeck_rEFInd/GUI/SteamDeck_rEFInd.desktop" "$HOME/Desktop/SteamDeck_rEFInd.desktop"
chmod +x "$HOME/.local/SteamDeck_rEFInd/GUI/SteamDeck_rEFInd.desktop"
chmod +x "$HOME/Desktop/SteamDeck_rEFInd.desktop"
echo -e "Installation complete...\n"
