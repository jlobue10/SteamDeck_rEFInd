#!/bin/bash
# Invoked by systemd as root, so $HOME cannot identify the desktop user. The
# GUI installer records the invoking user's background directory as one plain
# line in a root-owned file on SteamOS's persistent /etc overlay. Never source
# this file: even if its ownership were accidentally weakened, its contents
# must remain data rather than shell code.
BG_DIR_CONFIG=/etc/SteamDeck_rEFInd/background-dir
BG_DIR=/home/deck/.local/SteamDeck_rEFInd/backgrounds

load_background_dir() {
    local owner mode configured extra

    [ -f "$BG_DIR_CONFIG" ] && [ ! -L "$BG_DIR_CONFIG" ] || return 1
    owner="$(stat -c '%u:%g' -- "$BG_DIR_CONFIG" 2>/dev/null)" || return 1
    mode="$(stat -c '%a' -- "$BG_DIR_CONFIG" 2>/dev/null)" || return 1
    [ "$owner" = "0:0" ] || return 1
    # Owner may write; group and other must not. Reject special mode bits too.
    case "$mode" in
        [0-7][0145][0145]) ;;
        *) return 1 ;;
    esac

    exec 3< "$BG_DIR_CONFIG" || return 1
    IFS= read -r configured <&3 || {
        exec 3<&-
        return 1
    }
    if IFS= read -r extra <&3; then
        exec 3<&-
        return 1
    fi
    exec 3<&-
    case "$configured" in
        /*) printf '%s\n' "$configured" ;;
        *) return 1 ;;
    esac
}

if CONFIGURED_BG_DIR="$(load_background_dir)"; then
    BG_DIR="$CONFIGURED_BG_DIR"
elif [ -e "$BG_DIR_CONFIG" ] || [ -L "$BG_DIR_CONFIG" ]; then
    echo "rEFInd_bg_randomizer: ignoring unsafe or malformed $BG_DIR_CONFIG" >&2
fi

# The destination used to be hardcoded to /esp/efi/refind/, which silently wrote
# to the wrong place whenever rEFInd lives on another ESP (a Windows-side or
# SD-card install). Resolve it the same way the config installer and the Windows
# randomizer do. Already root here, so no password prompt is involved.
# shellcheck source=/dev/null
. "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/lib_esp_target.sh" || exit 1
trap esp_cleanup EXIT

# -type f (not -L) and an anchored *.png glob on purpose: this runs as root at
# every boot over a directory the desktop user owns, so following a symlink
# named *.png would copy any root-readable file onto the ESP, and the old
# `ls | grep .png` also matched unanchored ("notes-png.txt") and broke on
# spaces in filenames.
RAND_BG="$(find "$BG_DIR" -maxdepth 1 -type f -iname '*.png' 2>/dev/null | shuf -n1)"
[ -n "$RAND_BG" ] || exit 0

RESOLVED="$(resolve_refind_dir)" || {
    echo "rEFInd_bg_randomizer: no ESP with rEFInd on it; nothing to update" >&2
    exit 1
}
TARGET="${RESOLVED%%|*}"

esp_make_writable "$TARGET"
cp -f "$RAND_BG" "$TARGET/background.png" || exit 1
# Flush before a temporary mount (if one was made) is torn down by the trap.
sync
