#!/bin/bash

# Installs the GUI-generated refind.conf and PNGs onto the EFI System Partition
# that actually boots rEFInd.
#
# This previously hardcoded /esp/efi/refind/, so a rEFInd installed onto a
# different ESP (a Windows-side install onto its own ESP, or an SD-card ESP)
# kept receiving config updates at a path the firmware never boots -- the
# "I updated the config and nothing changed at boot" failure. ESP resolution now
# lives in lib_esp_target.sh and mirrors Windows/GUI/install_config_from_GUI.ps1;
# keep the two in behavioral parity.
#
# Resolution and copying both run under one sudo call: the Deck's ESP is mounted
# 0700 root:root by systemd-gpt-auto-generator, so even reading it to find where
# rEFInd lives needs root. /esp is a separate vfat partition, not part of the
# immutable rootfs, so no steamos-readonly bracketing is required here.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HOME/.local/SteamDeck_rEFInd/GUI"
FILES="refind.conf background.png os_icon1.png os_icon2.png os_icon3.png os_icon4.png"

# Quoted heredoc: nothing below is expanded by this shell, only by root's.
read -r -d '' PAYLOAD <<'EOS'
set -u
LIB="$1"
SRC="$2"
FILES="$3"

# shellcheck source=/dev/null
. "$LIB" || { echo "ERR_NOLIB $LIB"; exit 7; }
STAGED_FILES=()
cleanup() {
    local staged
    for staged in "${STAGED_FILES[@]}"; do
        [ -n "$staged" ] && rm -f -- "$staged" 2>/dev/null
    done
    esp_cleanup
}
trap cleanup EXIT

RESOLVED="$(resolve_refind_dir)" || { echo "ERR_NOTARGET"; exit 3; }
TARGET="${RESOLVED%%|*}"
HOW="${RESOLVED#*|}"

esp_make_writable "$TARGET"

mkdir -p "$TARGET" 2>/dev/null || { echo "ERR_MKDIR $TARGET"; exit 4; }

# Best-effort sweep of staging leftovers from an earlier interrupted run (the
# EXIT trap cannot run across SIGKILL or power loss, and random staging names
# would accumulate on the small ESP). Only the ".<name>.new.<suffix>" shapes
# created below are matched, so no live file can be touched.
for f in $FILES refind.conf.prev; do
    for stale in "$TARGET/.$f.new."*; do
        [ -f "$stale" ] && rm -f -- "$stale" 2>/dev/null
    done
done

[ -f "$SRC/refind.conf" ] && [ -s "$SRC/refind.conf" ] \
    || { echo "ERR_NOSRC"; exit 6; }

COPIED=0
declare -A STAGED
for f in $FILES; do
    [ -f "$SRC/$f" ] || continue
    stage="$(mktemp "$TARGET/.${f}.new.XXXXXX")" \
        || { echo "ERR_COPY $f"; exit 5; }
    STAGED["$f"]="$stage"
    STAGED_FILES+=("$stage")
    cp -f -- "$SRC/$f" "$stage" 2>/dev/null \
        || { echo "ERR_COPY $f"; exit 5; }
    if [ "$f" = refind.conf ] && [ ! -s "$stage" ]; then
        echo "ERR_COPY $f"
        exit 5
    fi
    COPIED=$((COPIED + 1))
done
[ -n "${STAGED[refind.conf]:-}" ] || { echo "ERR_NOSRC"; exit 6; }

if [ -f "$TARGET/refind.conf" ]; then
    backup_stage="$(mktemp "$TARGET/.refind.conf.prev.new.XXXXXX")" \
        || { echo "ERR_COPY refind.conf.prev"; exit 5; }
    STAGED_FILES+=("$backup_stage")
    cp -- "$TARGET/refind.conf" "$backup_stage" 2>/dev/null \
        && mv -f -- "$backup_stage" "$TARGET/refind.conf.prev" 2>/dev/null \
        || { echo "ERR_COPY refind.conf.prev"; exit 5; }
fi

# Staged, publish-last: assets first, refind.conf last. Publishing walks the
# same $FILES list as staging, so the two loops cannot drift apart.
for f in $FILES; do
    [ "$f" = refind.conf ] && continue
    [ -n "${STAGED[$f]:-}" ] || continue
    mv -f -- "${STAGED[$f]}" "$TARGET/$f" 2>/dev/null \
        || { echo "ERR_COPY $f"; exit 5; }
done
mv -f -- "${STAGED[refind.conf]}" "$TARGET/refind.conf" 2>/dev/null \
    || { echo "ERR_COPY refind.conf"; exit 5; }

# Flush to the ESP before the temporary mount (if any) goes away.
sync
echo "OK $COPIED|$TARGET|$HOW"
EOS

OUT="$(zenity --password --title="Enter sudo password" 2>/dev/null \
    | sudo -S bash -c "$PAYLOAD" bash "$SCRIPT_DIR/lib_esp_target.sh" "$SRC" "$FILES" 2>/dev/null)"
ANS=$?

RESULT="${OUT##*$'\n'}"
COUNT="${RESULT#OK }"; COUNT="${COUNT%%|*}"
DEST="${RESULT#*|}"; DEST="${DEST%%|*}"
HOW="${RESULT##*|}"

if [[ $ANS == 0 ]]; then
    zenity --info --title="Success" --width=560 2>/dev/null \
        --text="$(printf "Installed %s file(s) to:\n%s\n\nChosen as %s." "$COUNT" "$DEST" "$HOW")"
else
    # Distinguish the failures the old script lumped together as "wrong
    # password": it suppressed cp's stderr and keyed the dialog off the exit
    # status alone, so a missing destination reported an authentication error.
    case "$ANS" in
        3) MSG="$(printf "No EFI System Partition with rEFInd on it could be found,\nand no system ESP is mounted.\n\nInstall rEFInd first, then install the config.")" ;;
        4) MSG="$(printf "Could not create the destination directory:\n%s\n\nThe EFI System Partition may be mounted read-only." "${RESULT#ERR_MKDIR }")" ;;
        5) MSG="$(printf "Failed while copying %s to the EFI System Partition.\n\nIt may be full or mounted read-only." "${RESULT#ERR_COPY }")" ;;
        6) MSG="$(printf "No non-empty refind.conf was found in:\n%s\n\nGenerate the config in the GUI first (Create Config)." "$SRC")" ;;
        7) MSG="$(printf "Could not load the ESP resolution helper:\n%s\n\nRe-run the GUI installer to restore it." "${RESULT#ERR_NOLIB }")" ;;
        *) MSG="$(printf "Incorrect sudo password, or the prompt was cancelled.\n\nPlease try again providing the correct sudo password.")" ;;
    esac
    zenity --error --title="Install failed" --text="$MSG" --width=600 2>/dev/null
    exit 1
fi
