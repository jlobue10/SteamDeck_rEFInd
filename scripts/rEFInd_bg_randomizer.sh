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
CANDIDATE_FILE=""
STAGED_BG=""
TARGET_TMP=""
randomizer_cleanup() {
    [ -z "$TARGET_TMP" ] || rm -f -- "$TARGET_TMP"
    [ -z "$STAGED_BG" ] || rm -f -- "$STAGED_BG"
    [ -z "$CANDIDATE_FILE" ] || rm -f -- "$CANDIDATE_FILE"
    esp_cleanup
}
trap randomizer_cleanup EXIT

# This unit is root, but its input directory belongs to the desktop user. A
# find(1) -type check followed by a root cp(1) is not sufficient: the selected
# path can be swapped for a symlink between those operations. Select and open
# the file under the directory owner's unprivileged account instead. The root
# process receives bytes only, into a private temporary file with a size/time
# bound, then validates the PNG signature before touching the ESP.
BG_OWNER_UID="$(stat -Lc '%u' -- "$BG_DIR" 2>/dev/null)" || exit 0
case "$BG_OWNER_UID" in
    ''|*[!0-9]*|0)
        echo "rEFInd_bg_randomizer: refusing a background directory without a non-root owner" >&2
        exit 1
        ;;
esac
BG_OWNER="$(getent passwd "$BG_OWNER_UID" | cut -d: -f1)"
if [ -z "$BG_OWNER" ] || [ "$BG_OWNER" = root ] || ! command -v runuser >/dev/null 2>&1; then
    echo "rEFInd_bg_randomizer: could not resolve a safe account for the background directory" >&2
    exit 1
fi

CANDIDATE_FILE="$(mktemp)" || exit 1
if ! (
    set -o pipefail
    timeout 10s runuser -u "$BG_OWNER" -- \
        find "$BG_DIR" -maxdepth 1 -type f -readable -iname '*.png' -print0 2>/dev/null | \
        shuf -z -n1
) > "$CANDIDATE_FILE"; then
    echo "rEFInd_bg_randomizer: could not enumerate background images safely" >&2
    exit 1
fi
[ -s "$CANDIDATE_FILE" ] || exit 0

STAGED_BG="$(mktemp)" || exit 1
if ! (
    # Bash expresses RLIMIT_FSIZE in KiB. This caps a malicious or accidental
    # oversized input at 32 MiB before it can consume the root filesystem.
    ulimit -f 32768 || exit 1
    xargs -0 -r timeout 10s runuser -u "$BG_OWNER" -- cat -- < "$CANDIDATE_FILE"
) > "$STAGED_BG"; then
    echo "rEFInd_bg_randomizer: could not read the selected background safely" >&2
    exit 1
fi
PNG_SIGNATURE="$(od -An -tx1 -N8 -- "$STAGED_BG" 2>/dev/null | tr -d '[:space:]')"
if [ "$PNG_SIGNATURE" != 89504e470d0a1a0a ]; then
    echo "rEFInd_bg_randomizer: selected background is not a valid PNG file" >&2
    exit 1
fi

RESOLVED="$(resolve_refind_dir)" || {
    echo "rEFInd_bg_randomizer: no ESP with rEFInd on it; nothing to update" >&2
    exit 1
}
TARGET="${RESOLVED%%|*}"

esp_make_writable "$TARGET"
TARGET_TMP="$(mktemp "$TARGET/.background.png.XXXXXX")" || exit 1
cp -f -- "$STAGED_BG" "$TARGET_TMP" || exit 1
mv -f -- "$TARGET_TMP" "$TARGET/background.png" || exit 1
TARGET_TMP=""
# Flush before a temporary mount (if one was made) is torn down by the trap.
sync
