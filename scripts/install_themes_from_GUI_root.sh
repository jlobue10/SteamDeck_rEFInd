#!/bin/bash
# Installs the staged themes tree (~12 MB of theme.conf + PNG assets) onto the
# EFI System Partition that actually boots rEFInd, as EFI/refind/themes/ --
# the themes counterpart of install_config_from_GUI_root.sh.
#
# install-GUI.sh installs this file root-owned as
# /etc/SteamDeck_rEFInd/install_themes_from_GUI.sh and whitelists exactly that
# path in /etc/sudoers.d/zz_SteamDeck_rEFInd_install_config, so the GUI can
# run it synchronously with `sudo -n` and no password prompt. The same two
# hard rules as the config helper follow from being a NOPASSWD root target:
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
# Copy strategy: the user's themes tree is read entirely through
# `runuser -u <user> -- tar -c`, never by root directly — the same
# no-root-reads-under-$HOME posture as the config helper (a symlink or
# hardlink under ~/.local must not be able to exfiltrate root-readable files
# onto the world-readable ESP), just with tar instead of per-file cat because
# a themes tree is hundreds of small PNGs. root only extracts the byte stream
# into a private staging directory ON the ESP (same filesystem), then swaps
# each theme directory into place with directory renames. Per-file staging of
# 12 MB of PNGs would buy nothing here: rEFInd only reads the tree at the
# next boot, so per-theme-directory replacement is atomic enough, and a
# failure before the swap loop leaves the live tree completely untouched.
# vfat cannot hold symlinks and GNU tar refuses ".."/absolute member names by
# default, so extraction cannot escape the staging directory.
#
# Everything printed here is captured by the GUI and shown in its result
# dialog, so keep the output short and human-readable. The exit codes match
# install_config_from_GUI_root.sh where they overlap.

set -u

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
SRC="$USER_HOME/.local/SteamDeck_rEFInd/themes"

# ---------------------------------------------------------------------------
# ESP resolution, inlined from scripts/lib_esp_target.sh (see rule 1 above).
# ---------------------------------------------------------------------------

ESP_TYPE_GUID=c12a7328-f81f-11d2-ba4b-00a0c93ec93b

# Temp mounts are recorded in a file, not a shell array: resolve_refind_dir()
# is invoked via command substitution, so anything it mounts is registered from
# a subshell — an array assignment there would be lost to the parent and the
# EXIT trap would unmount nothing, leaving removable ESPs mounted read-write.
ESP_TMPMNT_LIST="$(mktemp)"
STAGING_DIR=""

esp_cleanup() {
    local m
    if [ -n "$STAGING_DIR" ] && [ -d "$STAGING_DIR" ]; then
        rm -rf -- "$STAGING_DIR" 2> /dev/null
    fi
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
    # Read-only for the probe: this runs as root without a password and mounts
    # EVERY ESP-typed partition just to test whether rEFInd is on it, including
    # whatever removable media is attached. Only the ESP actually chosen as the
    # target gets remounted writable, via esp_make_writable below.
    if mount -o ro,nosuid,nodev,noexec "$dev" "$mp" 2> /dev/null; then
        printf '%s\n' "$mp" >> "$ESP_TMPMNT_LIST"
        printf '%s\n' "$mp"
        return 0
    fi
    rmdir "$mp" 2> /dev/null
    return 1
}

# Make the ESP behind $1 (a mount point or any path under one) writable, but
# only if it is one of our own read-only probe mounts. ESPs the system already
# had mounted are left exactly as they were.
esp_make_writable() {
    local path="$1" m
    [ -n "$path" ] || return 0
    [ -f "$ESP_TMPMNT_LIST" ] || return 0
    while read -r m; do
        [ -n "$m" ] || continue
        case "$path" in
            "$m" | "$m"/*)
                mount -o remount,rw "$m" 2> /dev/null
                return 0
                ;;
        esac
    done < "$ESP_TMPMNT_LIST"
    return 0
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
# Copy the staged themes tree onto the resolved ESP.
# ---------------------------------------------------------------------------

trap esp_cleanup EXIT

RESOLVED="$(resolve_refind_dir)" || {
    echo "No EFI System Partition with rEFInd on it could be found, and no system ESP is mounted."
    echo "Install rEFInd first, then install the themes."
    exit 3
}
REFIND_DIR="${RESOLVED%%|*}"
HOW="${RESOLVED#*|}"
TARGET="$REFIND_DIR/themes"

esp_make_writable "$REFIND_DIR"

mkdir -p "$TARGET" 2> /dev/null || {
    echo "Could not create $TARGET -- the EFI System Partition may be mounted read-only."
    exit 4
}

# Enumerate the themes to install as the invoking user (root never reads under
# the user's home): a theme is a first-level directory whose theme.conf is a
# non-empty regular file.
THEMES=()
while IFS= read -r -d '' dir; do
    name="$(basename "$dir")"
    case "$name" in .*) continue ;; esac
    runuser -u "$RUN_USER" -- test -f "$SRC/$name/theme.conf" 2> /dev/null || continue
    runuser -u "$RUN_USER" -- test -s "$SRC/$name/theme.conf" 2> /dev/null || continue
    THEMES+=("$name")
done < <(runuser -u "$RUN_USER" -- \
    find "$SRC" -mindepth 1 -maxdepth 1 -type d -print0 2> /dev/null)

if [ "${#THEMES[@]}" -eq 0 ]; then
    echo "No themes were found in $SRC."
    echo "Re-run the GUI installer (install-GUI.sh) to stage the shipped themes."
    exit 6
fi

# Free-space check before anything is written: the source size (as measured by
# the user) plus headroom for the staging copy that briefly coexists with the
# live tree.
NEEDED_KB="$(runuser -u "$RUN_USER" -- du -sk -- "$SRC" 2> /dev/null | cut -f1)"
case "$NEEDED_KB" in '' | *[!0-9]*) NEEDED_KB=16384 ;; esac
AVAIL_KB="$(df -Pk -- "$TARGET" 2> /dev/null | awk 'NR==2 {print $4}')"
case "$AVAIL_KB" in '' | *[!0-9]*) AVAIL_KB=0 ;; esac
if [ "$AVAIL_KB" -lt "$((NEEDED_KB + 4096))" ]; then
    echo "Not enough free space on the EFI System Partition: need about $((NEEDED_KB / 1024 + 5)) MB, ${AVAIL_KB} KB available."
    echo "Nothing was changed."
    exit 5
fi

# Best-effort sweep of staging/backup directories an earlier interrupted run
# left behind (the EXIT trap cannot run across SIGKILL or power loss). Only
# the exact ".install-staging-*" / ".old-*" shapes created below are matched.
for stale in "$TARGET"/.install-staging-* "$TARGET"/.old-*; do
    [ -d "$stale" ] && rm -rf -- "$stale" 2> /dev/null
done

STAGING_DIR="$(mktemp -d "$TARGET/.install-staging-XXXXXX")" || {
    echo "Could not create a staging directory in $TARGET -- the ESP may be full or read-only."
    exit 5
}

# Read as the user, extract as root into the ESP staging directory. tar's
# defaults refuse absolute and ".." member names, and vfat cannot represent
# symlinks, so the extraction is confined to the staging directory.
if ! runuser -u "$RUN_USER" -- tar -C "$SRC" -cf - -- "${THEMES[@]}" 2> /dev/null \
    | tar -C "$STAGING_DIR" -xf - --no-same-owner 2> /dev/null; then
    echo "Failed while copying the themes to $TARGET -- the ESP may be full."
    echo "The live themes were not changed."
    exit 5
fi

# Swap each staged theme directory into place. The live tree is only touched
# after the full staging copy above succeeded, and each theme is replaced by
# directory renames on the same filesystem: current -> .old-<name>, staged ->
# live, then the .old copy is dropped. A failure mid-swap leaves every other
# theme either fully old or fully new, never half-copied.
INSTALLED=0
for name in "${THEMES[@]}"; do
    [ -d "$STAGING_DIR/$name" ] || {
        echo "Staged copy of '$name' is missing; the live themes were not changed further."
        exit 5
    }
    rm -rf -- "$TARGET/.old-$name" 2> /dev/null
    if [ -e "$TARGET/$name" ]; then
        mv -- "$TARGET/$name" "$TARGET/.old-$name" 2> /dev/null || {
            echo "Could not replace the existing theme '$name'."
            exit 5
        }
    fi
    if ! mv -- "$STAGING_DIR/$name" "$TARGET/$name" 2> /dev/null; then
        # Put the previous version back so the theme is not lost entirely.
        mv -- "$TARGET/.old-$name" "$TARGET/$name" 2> /dev/null
        echo "Failed while publishing the theme '$name'."
        exit 5
    fi
    rm -rf -- "$TARGET/.old-$name" 2> /dev/null
    INSTALLED=$((INSTALLED + 1))
done

# Flush to the ESP before any temporary mount goes away.
sync
echo "Installed $INSTALLED theme(s) to $TARGET"
echo "(chosen as $HOW)"
exit 0
