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
trap esp_cleanup EXIT

RESOLVED="$(resolve_refind_dir)" || { echo "ERR_NOTARGET"; exit 3; }
TARGET="${RESOLVED%%|*}"
HOW="${RESOLVED#*|}"

mkdir -p "$TARGET" 2>/dev/null || { echo "ERR_MKDIR $TARGET"; exit 4; }

COPIED=0
for f in $FILES; do
    [ -f "$SRC/$f" ] || continue
    cp -f "$SRC/$f" "$TARGET/" 2>/dev/null || { echo "ERR_COPY $f"; exit 5; }
    COPIED=$((COPIED + 1))
done
[ "$COPIED" -gt 0 ] || { echo "ERR_NOSRC"; exit 6; }

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
        6) MSG="$(printf "None of the config files were found in:\n%s\n\nGenerate the config in the GUI first (Create Config)." "$SRC")" ;;
        7) MSG="$(printf "Could not load the ESP resolution helper:\n%s\n\nRe-run the GUI installer to restore it." "${RESULT#ERR_NOLIB }")" ;;
        *) MSG="$(printf "Incorrect sudo password, or the prompt was cancelled.\n\nPlease try again providing the correct sudo password.")" ;;
    esac
    zenity --error --title="Install failed" --text="$MSG" --width=600 2>/dev/null
    exit 1
fi
