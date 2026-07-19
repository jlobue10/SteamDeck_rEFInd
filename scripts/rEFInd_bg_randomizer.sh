#!/bin/bash
# Invoked only by systemd/rEFInd_bg_randomizer.service as root, so $HOME is not
# reliably the deck user's home here -- keep this path hardcoded to match the
# service's own hardcoded assumption (see systemd/rEFInd_bg_randomizer.service).
BG_DIR=/home/deck/.local/SteamDeck_rEFInd/backgrounds

# The destination used to be hardcoded to /esp/efi/refind/, which silently wrote
# to the wrong place whenever rEFInd lives on another ESP (a Windows-side or
# SD-card install). Resolve it the same way the config installer and the Windows
# randomizer do. Already root here, so no password prompt is involved.
# shellcheck source=/dev/null
. "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/lib_esp_target.sh" || exit 1
trap esp_cleanup EXIT

RAND_BG="$(ls "$BG_DIR" | grep .png | shuf -n1)"
[ -n "$RAND_BG" ] || exit 0

RESOLVED="$(resolve_refind_dir)" || {
    echo "rEFInd_bg_randomizer: no ESP with rEFInd on it; nothing to update" >&2
    exit 1
}
TARGET="${RESOLVED%%|*}"

cp -f "$BG_DIR/$RAND_BG" "$TARGET/background.png" || exit 1
# Flush before a temporary mount (if one was made) is torn down by the trap.
sync
