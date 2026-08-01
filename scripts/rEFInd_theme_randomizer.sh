#!/bin/bash
# Picks a random rEFInd theme on each boot by replacing themes/active_theme.conf
# on the ESP with a copy of a randomly chosen theme's theme.conf. The live
# refind.conf is never rewritten: the GUI's Create Config appends one stable
# "include themes/active_theme.conf" line when theming is enabled, and this
# unit only ever swaps the file that line points at.
#
# Invoked by systemd as root (rEFInd_theme_randomizer.service). Unlike the
# background randomizer, every input lives root-owned on the ESP (the themes
# tree is installed there by the GUI's Install Themes button), so there is no
# user-directory read and no runuser dance here.
#
# This is a cosmetic boot-time unit: any content or environment problem warns
# and exits 0 rather than leaving a failed unit behind. Nonzero exits are
# reserved for internal errors (mktemp and friends).
#
# Note: if both this unit and rEFInd_bg_randomizer.service are enabled, the
# theme's banner wins -- a theme.conf sets its own "banner", and the include
# line supersedes the randomized background.png. That is documented behavior,
# not a conflict to resolve here.

# The destination is resolved the same way the config installer and the
# background randomizer resolve it -- never a hardcoded /esp/efi/refind/.
# Already root here, so no password prompt is involved.
# shellcheck source=/dev/null
. "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/lib_esp_target.sh" || exit 1
STAGED_CONF=""
randomizer_cleanup() {
    [ -z "$STAGED_CONF" ] || rm -f -- "$STAGED_CONF"
    esp_cleanup
}
trap randomizer_cleanup EXIT

RESOLVED="$(resolve_refind_dir)" || {
    echo "rEFInd_theme_randomizer: no ESP with rEFInd on it; nothing to update" >&2
    exit 0
}
TARGET="${RESOLVED%%|*}"

# Theming disabled (Theme combo set to None, or a hand-written config): the
# live refind.conf does not include active_theme.conf, so swapping it would
# change nothing at boot. Stay silent -- this is the normal "off" state.
grep -qF 'include themes/active_theme.conf' "$TARGET/refind.conf" 2> /dev/null || exit 0

THEMES_DIR="$TARGET/themes"

# Candidates are the installed themes' own confs: themes/*/theme.conf,
# non-empty. active_theme.conf itself sits at themes/ top level, so the
# per-directory glob can never pick it.
CANDIDATES=()
for conf in "$THEMES_DIR"/*/theme.conf; do
    [ -s "$conf" ] || continue
    CANDIDATES+=("$conf")
done
if [ "${#CANDIDATES[@]}" -eq 0 ]; then
    echo "rEFInd_theme_randomizer: no themes found under $THEMES_DIR; keeping the current theme" >&2
    exit 0
fi

PICK="$(printf '%s\n' "${CANDIDATES[@]}" | shuf -n 1)" || exit 1

esp_make_writable "$TARGET"

# Best-effort sweep of staging files an interrupted earlier run left behind
# (the EXIT trap cannot run across SIGKILL or power loss). Only the exact
# ".active_theme.conf.<suffix>" shape created below is matched.
for stale in "$THEMES_DIR"/.active_theme.conf.*; do
    [ -f "$stale" ] && rm -f -- "$stale" 2> /dev/null
done

# Staged publish: same-directory mktemp + rename, exactly like the background
# randomizer's atomic background.png swap, so a failure mid-copy can never
# truncate the live active_theme.conf.
STAGED_CONF="$(mktemp "$THEMES_DIR/.active_theme.conf.XXXXXX")" || exit 1
cp -f -- "$PICK" "$STAGED_CONF" || exit 1
mv -f -- "$STAGED_CONF" "$THEMES_DIR/active_theme.conf" || exit 1
STAGED_CONF=""
# Flush before a temporary mount (if one was made) is torn down by the trap.
sync
