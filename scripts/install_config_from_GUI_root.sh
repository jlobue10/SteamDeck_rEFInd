#!/bin/bash
# Installs the GUI-generated refind.conf and PNGs onto the EFI System
# Partition that actually boots rEFInd — the passwordless counterpart of
# install_config_from_GUI.sh (the zenity fallback).
#
# install-GUI.sh installs this file root-owned as
# /etc/SteamDeck_rEFInd/install_config_from_GUI.sh and whitelists exactly that
# path in /etc/sudoers.d/zz_SteamDeck_rEFInd_install_config, so the GUI can run
# it synchronously with `sudo -n` and no password prompt. Two hard rules
# follow from being a NOPASSWD root target:
#
#   1. SELF-CONTAINED. Never source anything user-writable (the staged
#      ~/.local/SteamDeck_rEFInd/scripts/lib_esp_target.sh included) — that
#      would hand root to whoever can edit the sourced file. The ESP
#      resolution below is inlined from scripts/lib_esp_target.sh; keep the
#      logic in behavioral parity when either changes.
#   2. NO INSTALL-TIME PLACEHOLDERS. The GUI refuses to run this file unless
#      it is byte-identical to the copy embedded in the binary at build time,
#      so the invoking user is resolved at runtime from SUDO_USER instead of
#      being sed-substituted at install time.
#
# Everything printed here is captured by the GUI and shown in its result
# dialog, so keep the output short and human-readable. The exit codes match
# the zenity fallback script's payload where they overlap.

set -u

# The config to install lives in the invoking user's home (sudo sets
# SUDO_USER; the sudoers rule only exists for regular users).
RUN_USER="${SUDO_USER:-}"
if [ -z "$RUN_USER" ] || [ "$RUN_USER" = root ]; then
    echo "This script is meant to be launched by the SteamDeck_rEFInd GUI via sudo."
    exit 2
fi
USER_HOME="$(getent passwd "$RUN_USER" | cut -d: -f6)"
if [ -z "$USER_HOME" ] || [ ! -d "$USER_HOME" ]; then
    echo "Could not resolve ${RUN_USER}'s home directory."
    exit 2
fi
SRC="$USER_HOME/.local/SteamDeck_rEFInd/GUI"
FILES="refind.conf background.png os_icon1.png os_icon2.png os_icon3.png os_icon4.png"

# ---------------------------------------------------------------------------
# ESP resolution, inlined from scripts/lib_esp_target.sh (see rule 1 above).
# ---------------------------------------------------------------------------

ESP_TYPE_GUID=c12a7328-f81f-11d2-ba4b-00a0c93ec93b

# Temp mounts are recorded in a file, not a shell array: resolve_refind_dir()
# is invoked via command substitution, so anything it mounts is registered from
# a subshell — an array assignment there would be lost to the parent and the
# EXIT trap would unmount nothing, leaving removable ESPs mounted read-write.
ESP_TMPMNT_LIST="$(mktemp)"

esp_cleanup() {
    local m
    if [ -n "${ESP_TMPMNT_LIST:-}" ] && [ -f "$ESP_TMPMNT_LIST" ]; then
        while read -r m; do
            [ -n "$m" ] || continue
            umount "$m" 2> /dev/null
            rmdir "$m" 2> /dev/null
        done < "$ESP_TMPMNT_LIST"
        rm -f "$ESP_TMPMNT_LIST"
    fi
}

# Echo a mount point for device $1, mounting it if it isn't mounted already.
esp_ensure_mounted() {
    local dev="$1" mp
    mp="$(findmnt -no TARGET -S "$dev" 2> /dev/null | head -1)"
    if [ -n "$mp" ]; then
        printf '%s\n' "$mp"
        return 0
    fi
    mp="$(mktemp -d)"
    if mount "$dev" "$mp" 2> /dev/null; then
        printf '%s\n' "$mp" >> "$ESP_TMPMNT_LIST"
        printf '%s\n' "$mp"
        return 0
    fi
    rmdir "$mp" 2> /dev/null
    return 1
}

esp_has_refind() { compgen -G "$1/EFI/refind/refind*.efi" > /dev/null 2>&1; }

# Partition GUID of the ESP the firmware's rEFInd entry points at, considered in
# BootOrder order. Two tiers, matching the PowerShell counterpart: an
# \EFI\refind\refind*.efi loader path first, then an entry labelled exactly
# "rEFInd" (what a Linux `efibootmgr -c -L rEFInd` install creates) for
# firmwares that render the path in a form the strict match misses.
esp_refind_guid() {
    local out order ids=() id line guid tier
    out="$(efibootmgr -v 2> /dev/null)" || return 1
    order="$(sed -n 's/^BootOrder: //p' <<< "$out" | tr -d ' ')"
    [ -n "$order" ] && IFS=, read -ra ids <<< "$order"
    # Entries missing from BootOrder are still worth checking, just last.
    while read -r id; do
        [[ " ${ids[*]:-} " == *" $id "* ]] || ids+=("$id")
    done < <(sed -nE 's/^Boot([0-9A-Fa-f]{4})\*? .*/\1/p' <<< "$out")

    for tier in loader label; do
        for id in "${ids[@]:-}"; do
            [ -n "$id" ] || continue
            line="$(grep -E "^Boot${id}\*? " <<< "$out")"
            [ -n "$line" ] || continue
            # efibootmgr >= 18 appends a tab + device path after the label, so
            # never anchor a bare label to end-of-line.
            if [ "$tier" = loader ]; then
                grep -qiE '\\EFI\\refind\\refind[^\\]*\.efi' <<< "$line" || continue
            else
                sed -nE "s/^Boot${id}\*? +([^\t]*).*/\1/p" <<< "$line" \
                    | grep -qx "rEFInd" || continue
            fi
            guid="$(sed -nE 's/.*HD\([0-9]+,GPT,([0-9a-fA-F-]{36}),.*/\1/p' <<< "$line")"
            [ -n "$guid" ] || continue
            printf '%s\n' "${guid,,}"
            return 0
        done
    done
    return 1
}

# Echo "<refind dir>|<how it was chosen>" for the ESP that actually boots
# rEFInd. Returns 1 when nothing suitable was found.
resolve_refind_dir() {
    local guid dev mp

    # 1. The ESP the firmware boots rEFInd from -- but only when rEFInd is
    #    really there, so a stale NVRAM entry falls through instead of winning.
    guid="$(esp_refind_guid)"
    if [ -n "$guid" ]; then
        dev="$(lsblk -rno PATH,PARTUUID 2> /dev/null \
            | awk -v g="$guid" 'tolower($2)==g {print $1; exit}')"
        if [ -n "$dev" ] && mp="$(esp_ensure_mounted "$dev")" && esp_has_refind "$mp"; then
            printf '%s|%s\n' "$mp/EFI/refind" "the ESP in the firmware's rEFInd boot entry ($dev)"
            return 0
        fi
    fi

    # 2. Any ESP that has rEFInd on it.
    while read -r dev; do
        [ -n "$dev" ] || continue
        mp="$(esp_ensure_mounted "$dev")" || continue
        if esp_has_refind "$mp"; then
            printf '%s|%s\n' "$mp/EFI/refind" "an ESP containing rEFInd ($dev)"
            return 0
        fi
    done < <(lsblk -rno PATH,PARTTYPE 2> /dev/null \
        | awk -v t="$ESP_TYPE_GUID" '$2==t {print $1}')

    # 3. The running system's ESP, for a first install not yet booted.
    for mp in /esp /boot/efi /efi /boot; do
        if [ -d "$mp/EFI" ]; then
            printf '%s|%s\n' "$mp/EFI/refind" "the running system's ESP ($mp)"
            return 0
        fi
    done

    return 1
}

# ---------------------------------------------------------------------------
# Copy the generated config onto the resolved ESP.
# ---------------------------------------------------------------------------

trap esp_cleanup EXIT

RESOLVED="$(resolve_refind_dir)" || {
    echo "No EFI System Partition with rEFInd on it could be found, and no system ESP is mounted."
    echo "Install rEFInd first, then install the config."
    exit 3
}
TARGET="${RESOLVED%%|*}"
HOW="${RESOLVED#*|}"

mkdir -p "$TARGET" 2> /dev/null || {
    echo "Could not create $TARGET -- the EFI System Partition may be mounted read-only."
    exit 4
}

COPIED=0
for f in $FILES; do
    # Existence check AND content read both run as the invoking user, never as
    # root: this script is reachable without a password, so reading the source
    # with root privileges would let a symlink/hardlink under ~/.local
    # exfiltrate any root-readable file onto the (world-readable when
    # temp-mounted) ESP. runuser can only read what the user can.
    runuser -u "$RUN_USER" -- test -f "$SRC/$f" 2> /dev/null || continue
    if ! runuser -u "$RUN_USER" -- cat -- "$SRC/$f" > "$TARGET/$f" 2> /dev/null; then
        rm -f "$TARGET/$f" 2> /dev/null
        echo "Failed while copying $f to $TARGET -- the ESP may be full or read-only."
        exit 5
    fi
    COPIED=$((COPIED + 1))
done

if [ "$COPIED" -eq 0 ]; then
    echo "No config files were found in $SRC."
    echo "Use Create Config in the GUI first."
    exit 6
fi

# Flush to the ESP before any temporary mount goes away.
sync
echo "Installed $COPIED file(s) to $TARGET"
echo "(chosen as $HOW)"
exit 0
