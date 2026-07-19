#!/bin/bash
# Shared ESP resolution for the scripts that write into an existing rEFInd
# install (install_config_from_GUI.sh, rEFInd_bg_randomizer.sh).
#
# Both used to hardcode /esp/efi/refind/, which is only right when rEFInd lives
# on the running system's ESP. Install rEFInd from Windows onto its own ESP, or
# onto an SD card, and the Windows tooling correctly targets it while the Linux
# side kept writing to /esp -- the "I updated the config and nothing changed at
# boot" failure. This mirrors the resolution order in
# Windows/GUI/install_config_from_GUI.ps1.
#
# Everything here needs root: systemd-gpt-auto-generator mounts ESPs 0700
# root:root, so even reading one to find rEFInd requires it. Source this from
# code already running as root.
#
# Usage:
#   . lib_esp_target.sh
#   trap esp_cleanup EXIT
#   if dir="$(resolve_refind_dir)"; then ... fi     # echoes "<dir>|<how>"

ESP_TYPE_GUID=c12a7328-f81f-11d2-ba4b-00a0c93ec93b

ESP_TMPMNTS=()

esp_cleanup() {
    local m
    for m in "${ESP_TMPMNTS[@]:-}"; do
        [ -n "$m" ] || continue
        umount "$m" 2> /dev/null
        rmdir "$m" 2> /dev/null
    done
    ESP_TMPMNTS=()
}

# Echo a mount point for device $1, mounting it if it isn't mounted already.
esp_ensure_mounted() {
    local dev="$1" mp
    mp="$(findmnt -rno TARGET -S "$dev" 2> /dev/null | head -1)"
    if [ -n "$mp" ]; then
        printf '%s\n' "$mp"
        return 0
    fi
    mp="$(mktemp -d)"
    if mount "$dev" "$mp" 2> /dev/null; then
        ESP_TMPMNTS+=("$mp")
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
