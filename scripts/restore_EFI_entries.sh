#!/bin/bash
# A simple script to restore missing SteamOS and/ or rEFInd EFI entries

# Resolve the ESP's disk and partition number from /esp instead of hardcoding
# /dev/nvme0n1: 64GB Decks boot from eMMC (/dev/mmcblk0), where the hardcoded
# path created broken entries. `lsblk -no PKNAME` has been observed returning
# empty (util-linux 2.42), so fall back to sysfs, where a partition's parent
# directory is its disk.
stat /esp/. >/dev/null 2>&1  # establish the systemd automount (SteamOS 3.9)
ESP_DEV="$(findmnt -no SOURCE /esp 2>/dev/null | grep -m1 "^/dev/")"
ESP_PART="$(basename "$ESP_DEV")"
ESP_PARTNUM="$(cat "/sys/class/block/$ESP_PART/partition" 2>/dev/null)"
ESP_PARENT="$(lsblk -no PKNAME "$ESP_DEV" 2>/dev/null | head -1)"
if [ -z "$ESP_PARENT" ] && [ -n "$ESP_PART" ]; then
	ESP_PARENT="$(basename "$(dirname "$(readlink -f "/sys/class/block/$ESP_PART")")")"
fi
ESP_DISK="/dev/$ESP_PARENT"
ESP_PARTUUID="$(lsblk -rno PARTUUID "$ESP_DEV" 2>/dev/null | head -1 | tr 'A-F' 'a-f')"
if [ -z "$ESP_PARTUUID" ]; then
	ESP_PARTUUID="$(blkid -s PARTUUID -o value "$ESP_DEV" 2>/dev/null | head -1 | tr 'A-F' 'a-f')"
fi
if [ ! -b "$ESP_DISK" ] || [ -z "$ESP_PARTNUM" ] || [ -z "$ESP_PARTUUID" ]; then
	echo "ERROR: could not safely resolve the ESP's disk and partition from /esp; refusing to modify NVRAM." >&2
	exit 1
fi

TAB=$'\t'
NVRAM_VERBOSE=""
BOOT_ORDER=""
refresh_nvram() {
	NVRAM_VERBOSE="$(efibootmgr -v 2>/dev/null)" || {
		echo "ERROR: efibootmgr could not read the firmware boot entries." >&2
		return 1
	}
	BOOT_ORDER="$(printf '%s\n' "$NVRAM_VERBOSE" \
		| sed -nE 's/^BootOrder:[[:space:]]*([0-9A-Fa-f,]+).*/\1/p' \
		| head -1 | tr 'a-f' 'A-F')"
}

# Return every Boot#### ID whose actual device path targets this Deck ESP and
# the requested loader. Labels are deliberately ignored: they are editable,
# and a foreign/stale entry called rEFInd or SteamOS must not shadow this ESP.
loader_entry_ids() {
	local loader_re="$1"
	printf '%s\n' "$NVRAM_VERBOSE" \
		| grep -iE "^Boot[0-9A-Fa-f]{4}\\*?[[:space:]]+[^${TAB}]*${TAB}HD\\([0-9]+,GPT,${ESP_PARTUUID},[^)]*\\)/(File\\()?${loader_re}(\\)|[0-9A-Fa-f]{8}|$)" \
		| sed -nE 's/^Boot([0-9A-Fa-f]{4}).*/\1/p' \
		| tr 'a-f' 'A-F'
}

first_in_boot_order() {
	local candidates="$1" id
	for id in ${BOOT_ORDER//,/ }; do
		if printf '%s\n' "$candidates" | grep -qx "$id"; then
			printf '%s\n' "$id"
			return 0
		fi
	done
	printf '%s\n' "$candidates" | sed -n '1p'
}

find_loader_entry() {
	local candidates
	candidates="$(loader_entry_ids "$1")"
	[ -n "$candidates" ] || return 1
	first_in_boot_order "$candidates"
}

ENSURED_BOOTNUM=""
ensure_loader_entry() {
	local label="$1" loader_path="$2" loader_re="$3" loader_file="$4" bootnum
	if [ ! -s "$loader_file" ]; then
		echo "ERROR: $label loader is missing or empty at $loader_file; refusing to create a broken NVRAM entry." >&2
		return 1
	fi
	bootnum="$(find_loader_entry "$loader_re")"
	if [ -z "$bootnum" ]; then
		echo "Creating the missing $label entry for the Deck ESP..." >&2
		sudo efibootmgr -c -d "$ESP_DISK" -p "$ESP_PARTNUM" -L "$label" -l "$loader_path" >&2 \
			|| return 1
		refresh_nvram || return 1
		bootnum="$(find_loader_entry "$loader_re")"
		if [ -z "$bootnum" ]; then
			echo "ERROR: the new $label entry could not be verified against the Deck ESP." >&2
			return 1
		fi
	fi
	if ! sudo efibootmgr -b "$bootnum" -a >/dev/null 2>&1; then
		echo "ERROR: could not activate $label entry Boot$bootnum." >&2
		return 1
	fi
	ENSURED_BOOTNUM="$bootnum"
}

refresh_nvram || exit 1
ensure_loader_entry "SteamOS" '\EFI\steamos\steamcl.efi' \
	'\\EFI\\steamos\\steamcl\.efi' /esp/efi/steamos/steamcl.efi || exit 1
STEAMOS_BOOTNUM="$ENSURED_BOOTNUM"
ensure_loader_entry "rEFInd" '\EFI\refind\refind_x64.efi' \
	'\\EFI\\refind\\refind_x64\.efi' /esp/efi/refind/refind_x64.efi || exit 1
REFIND_BOOTNUM="$ENSURED_BOOTNUM"

# Force only the verified Deck-ESP rEFInd entry for the next boot.
sudo efibootmgr -n "$REFIND_BOOTNUM" \
	|| { echo "ERROR: could not set verified rEFInd entry Boot$REFIND_BOOTNUM as BootNext." >&2; exit 1; }

echo -e "\nVerified SteamOS Boot$STEAMOS_BOOTNUM and rEFInd Boot$REFIND_BOOTNUM for the Deck ESP; BootNext is rEFInd.\n"
