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
#
# This is a cosmetic boot-time unit, so a problem with the user's content or
# the environment warns and exits 0 rather than leaving a failed unit behind
# on the ~1/N boots that a bad file happens to be drawn. Nonzero exits are
# reserved for internal errors (mktemp and friends).
BG_OWNER_UID="$(stat -Lc '%u' -- "$BG_DIR" 2>/dev/null)" || exit 0
case "$BG_OWNER_UID" in
    ''|*[!0-9]*|0)
        echo "rEFInd_bg_randomizer: refusing a background directory without a non-root owner" >&2
        exit 0
        ;;
esac
BG_OWNER="$(getent passwd "$BG_OWNER_UID" | cut -d: -f1)"
if [ -z "$BG_OWNER" ] || [ "$BG_OWNER" = root ] || ! command -v runuser >/dev/null 2>&1; then
    echo "rEFInd_bg_randomizer: could not resolve a safe account for the background directory" >&2
    exit 0
fi

CANDIDATE_FILE="$(mktemp)" || exit 1
if ! (
    set -o pipefail
    timeout 10s runuser -u "$BG_OWNER" -- \
        find "$BG_DIR" -maxdepth 1 -type f -readable -iname '*.png' -print0 2>/dev/null | \
        shuf -z
) > "$CANDIDATE_FILE"; then
    echo "rEFInd_bg_randomizer: could not enumerate background images safely; keeping the current background" >&2
    exit 0
fi
[ -s "$CANDIDATE_FILE" ] || exit 0

# A single bad file (renamed JPEG, oversized, unreadable) should not spoil the
# boot: walk the shuffled candidates and take the first one that validates,
# bounded so a directory full of junk still finishes quickly.
MAX_BG_BYTES=33554432
MAX_ATTEMPTS=3
STAGED_BG="$(mktemp)" || exit 1
STAGED_OK=""
ATTEMPTS=0
while IFS= read -r -d '' CANDIDATE; do
    [ "$ATTEMPTS" -lt "$MAX_ATTEMPTS" ] || break
    ATTEMPTS=$((ATTEMPTS + 1))
    # head(1) enforces the 32 MiB cap in the pipe itself: an RLIMIT_FSIZE
    # ulimit would have to survive runuser's PAM stack, where pam_limits can
    # silently replace it from limits.conf.
    if ! (
        set -o pipefail
        timeout 10s runuser -u "$BG_OWNER" -- cat -- "$CANDIDATE" | \
            head -c "$MAX_BG_BYTES"
    ) > "$STAGED_BG"; then
        echo "rEFInd_bg_randomizer: could not read '$CANDIDATE' safely; trying another" >&2
        continue
    fi
    # Exactly at the cap means head(1) may have truncated a larger file.
    STAGED_SIZE="$(stat -c '%s' -- "$STAGED_BG" 2>/dev/null)" || continue
    if [ "$STAGED_SIZE" -ge "$MAX_BG_BYTES" ]; then
        echo "rEFInd_bg_randomizer: '$CANDIDATE' exceeds ${MAX_BG_BYTES} bytes; trying another" >&2
        continue
    fi
    PNG_SIGNATURE="$(od -An -tx1 -N8 -- "$STAGED_BG" 2>/dev/null | tr -d '[:space:]')"
    if [ "$PNG_SIGNATURE" != 89504e470d0a1a0a ]; then
        echo "rEFInd_bg_randomizer: '$CANDIDATE' is not a valid PNG file; trying another" >&2
        continue
    fi
    STAGED_OK=1
    break
done < "$CANDIDATE_FILE"
if [ -z "$STAGED_OK" ]; then
    echo "rEFInd_bg_randomizer: no usable background image found; keeping the current background" >&2
    exit 0
fi

RESOLVED="$(resolve_refind_dir)" || {
    echo "rEFInd_bg_randomizer: no ESP with rEFInd on it; nothing to update" >&2
    exit 0
}
TARGET="${RESOLVED%%|*}"

esp_make_writable "$TARGET"
TARGET_TMP="$(mktemp "$TARGET/.background.png.XXXXXX")" || exit 1
cp -f -- "$STAGED_BG" "$TARGET_TMP" || exit 1
mv -f -- "$TARGET_TMP" "$TARGET/background.png" || exit 1
TARGET_TMP=""
# Flush before a temporary mount (if one was made) is torn down by the trap.
sync
