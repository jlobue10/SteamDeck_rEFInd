#!/bin/bash

# A simple script to install the rEFInd customization GUI.
# Designed to be piped from curl:
#   curl -L https://github.com/jlobue10/SteamDeck_rEFInd/raw/main/install-GUI.sh | sh
echo -e "Installing SteamDeck rEFInd...\n"

READONLY_DISABLED=0
# Bazzite and ChimeraOS have no steamos-readonly; both helpers are no-ops there.
disable_readonly() {
    command -v steamos-readonly >/dev/null 2>&1 || return 0
    if [ "$READONLY_DISABLED" -eq 1 ]; then
        return 0
    fi
    if ! sudo steamos-readonly disable; then
        echo "Error: could not disable SteamOS read-only mode." >&2
        return 1
    fi
    READONLY_DISABLED=1
}
enable_readonly() {
    command -v steamos-readonly >/dev/null 2>&1 || return 0
    if [ "$READONLY_DISABLED" -eq 0 ]; then
        return 0
    fi
    if ! sudo steamos-readonly enable; then
        echo "CRITICAL: SteamOS read-only mode could not be restored. Run 'sudo steamos-readonly enable' before rebooting." >&2
        return 1
    fi
    READONLY_DISABLED=0
}
restore_readonly_on_exit() {
    status=$?
    trap - EXIT
    if [ "$READONLY_DISABLED" -eq 1 ] && ! enable_readonly; then
        status=70
    fi
    exit "$status"
}
trap restore_readonly_on_exit EXIT

cd "$HOME" || exit 1
rm -rf "$HOME/SteamDeck_rEFInd"
if ! git clone --depth 1 https://github.com/jlobue10/SteamDeck_rEFInd; then
    echo "Error: failed to clone the SteamDeck_rEFInd repository. Aborting." >&2
    exit 1
fi
cd SteamDeck_rEFInd || exit 1
CURRENT_WD="$(pwd)"

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

# Check out the tag matching the package being installed, so the staged
# scripts and assets are the ones the released binary expects. (The GUI's
# privileged actions are version-handshaked against the packaged helper
# binary, not hash-checked against these scripts any more, but staging
# main-branch files next to a release binary is still asking for skew.)
if [ -n "$VERSION" ] && [ "$VERSION" != "null" ]; then
    if git fetch --depth 1 origin "refs/tags/$VERSION:refs/tags/$VERSION" >/dev/null 2>&1 &&
        git checkout -q "$VERSION" >/dev/null 2>&1; then
        printf 'Staging scripts from tag %s.\n' "$VERSION"
    else
        echo "Warning: could not check out $VERSION; staging main-branch scripts instead." >&2
    fi
fi

disable_readonly || exit 1
mkdir -p "$HOME/.local/SteamDeck_rEFInd/GUI"
# Stage GUI/ file by file rather than with a blanket cp -rf: rEFInd_GUI.ini holds
# every saved setting, and refind.conf / background.png / os_icon*.png are what
# Create Config staged. Re-running this installer is the documented recovery
# after a SteamOS update, and it must not silently reset the user's config.
for _src in "$CURRENT_WD"/GUI/*; do
    [ -e "$_src" ] || continue
    case "$(basename "$_src")" in
        rEFInd_GUI.ini | refind.conf | background.png | os_icon?.png)
            [ -e "$HOME/.local/SteamDeck_rEFInd/GUI/$(basename "$_src")" ] ||
                cp -rf "$_src" "$HOME/.local/SteamDeck_rEFInd/GUI/"
            ;;
        *)
            cp -rf "$_src" "$HOME/.local/SteamDeck_rEFInd/GUI/"
            ;;
    esac
done
cp -rf "$CURRENT_WD/icons/" "$HOME/.local/SteamDeck_rEFInd"
cp -rf "$CURRENT_WD/backgrounds/" "$HOME/.local/SteamDeck_rEFInd"
cp -rf "$CURRENT_WD/themes/" "$HOME/.local/SteamDeck_rEFInd"
cp -rf "$CURRENT_WD/scripts/" "$HOME/.local/SteamDeck_rEFInd"
# Seed the config template only on a first install, for the same reason.
[ -e "$HOME/.local/SteamDeck_rEFInd/GUI/refind.conf" ] ||
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

if [ -f /etc/systemd/system/rEFInd_theme_randomizer.service ]; then
    sudo systemctl disable --now rEFInd_theme_randomizer.service
    # Force removing old service file from previous versions
    echo -e "\nRemoving old rEFInd_theme_randomizer.service\n"
    sudo rm /etc/systemd/system/rEFInd_theme_randomizer.service
fi

# The package's post_install scriptlet handles daemon-reload plus enabling and
# starting bootnext-refind.service.
if ! sudo pacman -U --noconfirm "$INSTALL_PKG"; then
    echo "Error: pacman failed to install $INSTALL_PKG. Aborting." >&2
    exit 1
fi
rm -f "$INSTALL_PKG"

# Passwordless Install Config / Install Themes: the pacman package above
# installed the root-owned helper binary
# (/etc/SteamDeck_rEFInd/SteamDeck_rEFInd_helper); the sudoers rule below
# whitelists exactly its two install subcommands, so the GUI can run them
# with `sudo -n` and no password prompt. /etc is a persistent overlay on
# SteamOS (upperdir on /var), so everything here survives OS updates just
# like the systemd units. The privileged shell scripts of previous versions
# (config/themes installers, both randomizers, lib_esp_target.sh) are
# superseded by helper subcommands — remove stale root-owned copies so
# nothing points at dead code.
sudo install -d -m 0755 /etc/SteamDeck_rEFInd
sudo rm -f /etc/SteamDeck_rEFInd/install_config_from_GUI.sh \
    /etc/SteamDeck_rEFInd/install_themes_from_GUI.sh \
    /etc/SteamDeck_rEFInd/rEFInd_bg_randomizer.sh \
    /etc/SteamDeck_rEFInd/rEFInd_theme_randomizer.sh \
    /etc/SteamDeck_rEFInd/lib_esp_target.sh

# bootnext-refind.service runs this as root on every boot, so it gets a
# root-owned copy here too -- executing (or sourcing) a $HOME/.local path
# from a root unit would let any code running as the desktop user get root
# at the next boot. The package installs the same file; doing it here as
# well means the unit is safe even when the installed release predates this
# change.
sudo install -o root -g root -m 0755 \
    "$CURRENT_WD/scripts/restore_EFI_entries.sh" \
    /etc/SteamDeck_rEFInd/restore_EFI_entries.sh

# The package's post_install started bootnext-refind.service BEFORE the fresh
# restore_EFI_entries.sh above replaced the release's copy; if the release
# still carried the pre-SteamOS-3.9 script (which failed on healthy, already
# active entries), the unit is now sitting in "failed". Re-run it with the
# staged copy so the install ends with the service healthy.
if [ "$(systemctl is-active bootnext-refind.service 2>/dev/null)" = "failed" ]; then
    sudo systemctl restart bootnext-refind.service \
        || echo "Warning: bootnext-refind.service did not start; check 'sudo systemctl status bootnext-refind.service'." >&2
fi

# The theme randomizer unit ships in the release package like the two units
# above, but the installed release may predate it. Stage the unit file
# directly when the package didn't provide it, so the GUI's Theme Rand
# buttons work immediately; the removal block before `pacman -U` above keeps
# a later package upgrade from colliding with this copy.
if [ ! -f /etc/systemd/system/rEFInd_theme_randomizer.service ]; then
    sudo install -o root -g root -m 0644 \
        "$CURRENT_WD/systemd/rEFInd_theme_randomizer.service" \
        /etc/systemd/system/rEFInd_theme_randomizer.service
    sudo systemctl daemon-reload
fi

# The randomizer runs as root under systemd, where $HOME is /root. Record the
# actual desktop user's backgrounds directory as data in a root-owned file on
# SteamOS's persistent /etc overlay. The service validates and reads this file;
# it never sources it as shell code.
case "$HOME" in
    /*) ;;
    *)
        echo "Error: HOME is not an absolute path; refusing to configure the background randomizer." >&2
        exit 1
        ;;
esac
case "$HOME" in
    *$'\n'*)
        echo "Error: HOME contains a newline; refusing to create a malformed randomizer configuration." >&2
        exit 1
        ;;
esac
BG_DIR_CONFIG_TMP="$(mktemp)" || exit 1
if ! printf '%s\n' "$HOME/.local/SteamDeck_rEFInd/backgrounds" > "$BG_DIR_CONFIG_TMP" ||
    ! sudo install -o root -g root -m 0644 "$BG_DIR_CONFIG_TMP" \
        /etc/SteamDeck_rEFInd/background-dir; then
    rm -f "$BG_DIR_CONFIG_TMP"
    echo "Error: could not install the background randomizer path configuration." >&2
    exit 1
fi
rm -f "$BG_DIR_CONFIG_TMP"

INSTALL_USER="$(id -un)"
# Refuse a username that is not a plain POSIX account name before splicing it
# into the sudoers rule: sed metacharacters would corrupt the substitution, and
# whitespace could in principle widen the generated rule. visudo -cf below is
# the backstop, but validate up front so we fail with a clear message instead of
# writing a surprising rule.
case "$INSTALL_USER" in
    *[!a-z0-9_-]* | '' | [!a-z_]*)
        echo "Error: refusing to build a sudoers rule for unexpected username '$INSTALL_USER'." >&2
        echo "Install Config and Install Themes will not be enabled." >&2
        INSTALL_USER="" ;;
esac
if [ -n "$INSTALL_USER" ]; then
sed "s/^USER /$INSTALL_USER /" "$CURRENT_WD/scripts/zz_SteamDeck_rEFInd_install_config" \
    > "$CURRENT_WD/sudoers_rule.tmp"
if sudo visudo -cf "$CURRENT_WD/sudoers_rule.tmp" > /dev/null 2>&1; then
    sudo install -o root -g root -m 0440 "$CURRENT_WD/sudoers_rule.tmp" \
        /etc/sudoers.d/zz_SteamDeck_rEFInd_install_config
    echo "Enabled passwordless config install for $INSTALL_USER."
else
    echo "Warning: the generated sudoers rule failed visudo validation and was NOT installed." >&2
    echo "Install Config and Install Themes will refuse to run until it is (re-run this installer)." >&2
fi
rm -f "$CURRENT_WD/sudoers_rule.tmp"
fi

enable_readonly || exit 70

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
