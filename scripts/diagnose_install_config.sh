#!/bin/bash

# Read-only diagnostic for "Install Config succeeds but nothing changes at
# boot" (v3.4.0 native-helper flow). Gathers, in one run, everything needed
# to tell WHERE the config was written vs WHERE the booting rEFInd reads:
#
#   1. the firmware's boot entries (efibootmgr -v) and which one the
#      resolver's two NVRAM tiers would match,
#   2. every ESP-typed partition, with the mtime + SHA-256 of each copy of
#      EFI/refind/refind.conf (and any fallback EFI/BOOT/bootx64.efi),
#   3. the staged source config under ~/.local, so stale copies stand out,
#   4. the helper's version handshake + passwordless sudo probe state,
#   5. the tail of the GUI's install log.
#
# It never writes to any ESP: unmounted candidates are probe-mounted
# ro,nosuid,nodev,noexec on private temp dirs and unmounted again (the same
# flags the helper's resolver uses). Run it with sudo in desktop mode:
#
#   sudo ~/.local/SteamDeck_rEFInd/scripts/diagnose_install_config.sh
#
# and attach the full output to the issue/report.

set -u

if [ "$(id -u)" -ne 0 ]; then
    echo "Run this with sudo (ESPs are mounted 0700 root:root)." >&2
    exit 2
fi

ESP_TYPE_GUID=c12a7328-f81f-11d2-ba4b-00a0c93ec93b
HELPER=/etc/SteamDeck_rEFInd/SteamDeck_rEFInd_helper
# Under sudo $HOME is root's; the staged config belongs to the invoking user.
USER_HOME="$HOME"
if [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != "root" ]; then
    USER_HOME="$(getent passwd "$SUDO_USER" | cut -d: -f6)"
fi
SRC_DIR="$USER_HOME/.local/SteamDeck_rEFInd/GUI"

TMPMNT_LIST="$(mktemp)"
cleanup() {
    local m
    while read -r m; do
        [ -n "$m" ] || continue
        umount "$m" 2>/dev/null
        rmdir "$m" 2>/dev/null
    done < "$TMPMNT_LIST"
    rm -f "$TMPMNT_LIST"
}
trap cleanup EXIT

section() { printf '\n===== %s =====\n' "$1"; }

# Trigger the systemd automounts before reading mount state: a plain stat of
# the mount point does NOT trigger them (AT_NO_AUTOMOUNT), the trailing /.
# does, even without permission on the mounted fs.
for mp in /esp /efi /boot/efi /boot; do
    stat "$mp/." > /dev/null 2>&1
done

section "system"
date
cat /sys/class/dmi/id/product_name 2>/dev/null
uname -r
if [ -x "$HELPER" ]; then
    printf 'helper: %s (version %s)\n' "$HELPER" "$("$HELPER" --version 2>/dev/null)"
else
    echo "helper: MISSING at $HELPER"
fi
if [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != "root" ]; then
    if sudo -n -u "$SUDO_USER" true 2>/dev/null &&
        runuser -u "$SUDO_USER" -- sudo -n -l "$HELPER" install-config >/dev/null 2>&1; then
        echo "sudoers: passwordless install-config rule OK for $SUDO_USER"
    else
        echo "sudoers: passwordless install-config probe FAILED for $SUDO_USER"
    fi
fi

section "firmware boot entries (efibootmgr -v)"
efibootmgr -v 2>&1

section "resolver NVRAM match (what the helper's tier 1 would pick)"
EFIBOOT_OUT="$(efibootmgr -v 2>/dev/null)"
MATCH_GUID=""
for tier in loader label; do
    [ -n "$MATCH_GUID" ] && break
    while read -r line; do
        if [ "$tier" = loader ]; then
            grep -qiE '\\EFI\\refind\\refind[^\\]*\.efi' <<< "$line" || continue
        else
            # efibootmgr >= 18 appends a tab + device path after the label.
            sed -nE 's/^Boot[0-9A-Fa-f]{4}\*? +([^\t]*).*/\1/p' <<< "$line" \
                | grep -qx "rEFInd" || continue
        fi
        MATCH_GUID="$(sed -nE 's/.*HD\([0-9]+,GPT,([0-9a-fA-F-]{36}),.*/\1/p' <<< "$line")"
        if [ -n "$MATCH_GUID" ]; then
            printf 'tier %s matched: %s\n' "$tier" "$line"
            printf 'partition GUID: %s\n' "${MATCH_GUID,,}"
            break
        fi
    done <<< "$EFIBOOT_OUT"
done
[ -n "$MATCH_GUID" ] || echo "no rEFInd entry matched by loader path or exact label"

section "block devices"
lsblk -o PATH,SIZE,PARTTYPE,PARTUUID,FSTYPE,LABEL,MOUNTPOINTS 2>&1

section "staged source config ($SRC_DIR)"
if [ -f "$SRC_DIR/refind.conf" ]; then
    ls -l "$SRC_DIR/refind.conf" "$SRC_DIR"/background.png \
        "$SRC_DIR"/os_icon?.png "$SRC_DIR"/active_theme.conf 2>/dev/null
    sha256sum "$SRC_DIR/refind.conf"
    echo "-- menuentry/include/showtools lines:"
    grep -nE '^[[:space:]]*(menuentry|include|showtools|default_selection|resolution)' \
        "$SRC_DIR/refind.conf"
else
    echo "NO staged refind.conf -- Create Config has not run for this user"
fi

# Every ESP-typed partition: where each copy of the config lives and whether
# it matches the staged source.
while read -r dev parttype partuuid; do
    [ "${parttype,,}" = "$ESP_TYPE_GUID" ] || continue
    section "ESP $dev (PARTUUID $partuuid)"
    # findmnt lists the autofs row (SOURCE systemd-1) before the real vfat
    # row on automounted paths; -S <dev> is immune.
    mp="$(findmnt -no TARGET -S "$dev" 2>/dev/null | head -1)"
    mounted_how="already mounted at $mp"
    if [ -z "$mp" ]; then
        mp="$(mktemp -d)"
        if mount -o ro,nosuid,nodev,noexec "$dev" "$mp" 2>/dev/null; then
            printf '%s\n' "$mp" >> "$TMPMNT_LIST"
            mounted_how="probe-mounted ro at $mp"
        else
            rmdir "$mp" 2>/dev/null
            echo "could not mount (skipped)"
            continue
        fi
    fi
    echo "$mounted_how"
    [ "${partuuid,,}" = "${MATCH_GUID,,}" ] && [ -n "$MATCH_GUID" ] \
        && echo "*** this is the ESP the firmware's rEFInd entry points at ***"
    if [ -d "$mp/EFI/refind" ]; then
        ls -l --time-style=full-iso "$mp/EFI/refind/" 2>/dev/null | head -25
        if [ -f "$mp/EFI/refind/refind.conf" ]; then
            sha256sum "$mp/EFI/refind/refind.conf"
            if [ -f "$SRC_DIR/refind.conf" ]; then
                if cmp -s "$mp/EFI/refind/refind.conf" "$SRC_DIR/refind.conf"; then
                    echo ">>> refind.conf here MATCHES the staged source"
                else
                    echo ">>> refind.conf here DIFFERS from the staged source"
                fi
            fi
        else
            echo "no refind.conf in EFI/refind"
        fi
        [ -d "$mp/EFI/refind/themes" ] \
            && ls -l --time-style=full-iso "$mp/EFI/refind/themes/active_theme.conf" 2>/dev/null
    else
        echo "no EFI/refind directory"
    fi
    # A rEFInd parked on the fallback path boots without any Boot#### entry
    # and reads EFI/BOOT/refind.conf -- a write to EFI/refind never reaches it.
    if [ -f "$mp/EFI/BOOT/bootx64.efi" ]; then
        ls -l --time-style=full-iso "$mp/EFI/BOOT/" 2>/dev/null | head -10
        if grep -qi refind "$mp/EFI/BOOT/bootx64.efi" 2>/dev/null; then
            echo ">>> EFI/BOOT/bootx64.efi looks like a rEFInd binary (fallback-path install)"
        fi
    fi
done < <(lsblk -rno PATH,PARTTYPE,PARTUUID 2>/dev/null)

section "GUI install log (last 40 lines)"
LOGDIR="$SRC_DIR/logs"
if [ -d "$LOGDIR" ]; then
    # shellcheck disable=SC2012
    latest="$(ls -1t "$LOGDIR" 2>/dev/null | head -1)"
    if [ -n "$latest" ]; then
        echo "-- $LOGDIR/$latest:"
        tail -40 "$LOGDIR/$latest"
    fi
else
    echo "no log directory at $LOGDIR"
fi

section "done"
echo "Attach everything above to the report."
