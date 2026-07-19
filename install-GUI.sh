#!/bin/bash

# A simple script to install the rEFInd customization GUI.
# Designed to be piped from curl:
#   curl -L https://github.com/jlobue10/SteamDeck_rEFInd/raw/main/install-GUI.sh | sh
echo -e "Installing SteamDeck rEFInd...\n"
cd "$HOME" || exit 1
rm -rf "$HOME/SteamDeck_rEFInd"
if ! git clone --depth 1 https://github.com/jlobue10/SteamDeck_rEFInd; then
    echo "Error: failed to clone the SteamDeck_rEFInd repository. Aborting." >&2
    exit 1
fi
cd SteamDeck_rEFInd || exit 1
CURRENT_WD="$(pwd)"

sudo steamos-readonly disable
# Make sure readonly gets re-enabled even if the script aborts partway through
trap 'sudo steamos-readonly enable' EXIT
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

# /usr is the immutable A/B rootfs: a SteamOS update replaces it wholesale and
# takes the pacman-installed binary with it. The copy under $HOME/.local is on
# the home partition, survives updates, and is what the desktop entry points at.
cp -f /usr/bin/SteamDeck_rEFInd "$HOME/.local/SteamDeck_rEFInd/GUI/SteamDeck_rEFInd"
chmod +x "$HOME/.local/SteamDeck_rEFInd/GUI/SteamDeck_rEFInd"
cp -f /usr/share/applications/SteamDeck_rEFInd.desktop "$HOME/.local/SteamDeck_rEFInd/GUI/SteamDeck_rEFInd.desktop"
# Desktop file ships with /home/deck hardcoded; rewrite it for the actual user's home
sed -i "s|/home/deck|$HOME|g" "$HOME/.local/SteamDeck_rEFInd/GUI/SteamDeck_rEFInd.desktop"

# Install as a real XDG launcher so it shows up in the application menu. This
# location needs no trust marking, so it works regardless of Plasma version.
mkdir -p "$HOME/.local/share/applications"
install -Dm644 "$HOME/.local/SteamDeck_rEFInd/GUI/SteamDeck_rEFInd.desktop" \
    "$HOME/.local/share/applications/SteamDeck_rEFInd.desktop"
update-desktop-database "$HOME/.local/share/applications" 2>/dev/null

# Desktop shortcut, as a symlink into the authorized location above. KDE only
# treats a .desktop file as a launcher when it lives in an authorized XDG
# applications dir; a plain executable copy sitting in ~/Desktop is instead run
# through the shell, which fails on "[Desktop Entry]":
#   ~/Desktop/./SteamDeck_rEFInd.desktop: line 1: [Desktop: command not found
# Symlinking means KIO resolves the target and launches it properly.
mkdir -p "$HOME/Desktop"
rm -f "$HOME/Desktop/SteamDeck_rEFInd.desktop"
ln -sfn "$HOME/.local/share/applications/SteamDeck_rEFInd.desktop" \
    "$HOME/Desktop/SteamDeck_rEFInd.desktop"

echo -e "Installation complete...\n"
