#!/bin/bash
# A simple script to restore missing SteamOS and/ or rEFInd EFI entries

# Resolve the ESP's disk and partition number from /esp instead of hardcoding
# /dev/nvme0n1: 64GB Decks boot from eMMC (/dev/mmcblk0), where the hardcoded
# path created broken entries. `lsblk -no PKNAME` has been observed returning
# empty (util-linux 2.42), so fall back to sysfs, where a partition's parent
# directory is its disk.
ESP_DEV="$(findmnt -no SOURCE /esp 2>/dev/null | head -1)"
ESP_PART="$(basename "$ESP_DEV")"
ESP_PARTNUM="$(cat "/sys/class/block/$ESP_PART/partition" 2>/dev/null)"
ESP_PARENT="$(lsblk -no PKNAME "$ESP_DEV" 2>/dev/null | head -1)"
if [ -z "$ESP_PARENT" ] && [ -n "$ESP_PART" ]; then
	ESP_PARENT="$(basename "$(dirname "$(readlink -f "/sys/class/block/$ESP_PART")")")"
fi
ESP_DISK="/dev/$ESP_PARENT"
if [ ! -b "$ESP_DISK" ] || [ -z "$ESP_PARTNUM" ]; then
	echo "Warning: could not resolve the ESP's disk from /esp; falling back to /dev/nvme0n1 partition 1." >&2
	ESP_DISK="/dev/nvme0n1"
	ESP_PARTNUM=1
fi

# efibootmgr >= 18 appends "\t<device path>" after the label even without -v,
# so match on the start of the label rather than anchoring the whole line.
if ! efibootmgr | grep -qE '^Boot[0-9A-Fa-f]{4}\*? +SteamOS'; then
	# Recreate the missing SteamOS EFI entry
	sudo efibootmgr -c -d "$ESP_DISK" -p "$ESP_PARTNUM" -L "SteamOS" -l '\EFI\steamos\steamcl.efi'
fi

if ! efibootmgr | grep -qE '^Boot[0-9A-Fa-f]{4}\*? +rEFInd'; then
	# Recreate the missing rEFInd EFI entry
	sudo efibootmgr -c -d "$ESP_DISK" -p "$ESP_PARTNUM" -L "rEFInd" -l '\EFI\refind\refind_x64.efi'
fi

# Forcing rEFInd to have bootnext top priority, just in case Windows EFI entry is active
REFIND_BOOTNUM="$(efibootmgr | sed -nE 's/^Boot([0-9A-Fa-f]{4})\*? +rEFInd.*/\1/p' | head -1)"
if [ -n "$REFIND_BOOTNUM" ]; then
	sudo efibootmgr -n "$REFIND_BOOTNUM"
else
	echo "Warning: no rEFInd entry found to set as next boot." >&2
fi

echo -e "\nMissing EFI entries for SteamOS and/ or rEFInd have been restored.\n"
