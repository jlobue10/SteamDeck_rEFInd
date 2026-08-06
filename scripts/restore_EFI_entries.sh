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

# SteamOS 3.9's stack (efibootmgr 18 / efivar 39 / kernel 6.16) can no longer
# rewrite an EXISTING Boot#### variable in place: `efibootmgr -b NNNN -a/-A`
# fails even as root, while creates (-c), deletes (-B) and new variables (-n)
# keep working. Two consequences here: an entry that is already active must
# not be rewritten at all (the old unconditional -a is exactly what failed
# this unit on healthy entries), and when a write is genuinely needed it only
# goes through after loosening the efivars file's mode (0644 is restored
# immediately afterwards; verified on real hardware).
EFIVARS_DIR="/sys/firmware/efi/efivars"
EFI_GLOBAL_GUID="8be4df61-93ca-11d2-aa0d-00e098032b8c"

entry_is_active() {
	printf '%s\n' "$NVRAM_VERBOSE" | grep -qiE "^Boot${1}\*"
}

activate_entry() {
	local bootnum="$1" var="$EFIVARS_DIR/Boot${bootnum}-$EFI_GLOBAL_GUID" rc=0
	entry_is_active "$bootnum" && return 0
	if ! sudo efibootmgr -b "$bootnum" -a >/dev/null 2>&1; then
		sudo chmod 666 "$var" 2>/dev/null || return 1
		sudo efibootmgr -b "$bootnum" -a >/dev/null 2>&1 || rc=1
		sudo chmod 644 "$var" 2>/dev/null
		[ "$rc" -eq 0 ] || return 1
	fi
	refresh_nvram || return 1
	entry_is_active "$bootnum"
}

# Return every Boot#### ID whose actual device path targets this Deck ESP and
# the requested loader. Labels are deliberately ignored: they are editable,
# and a foreign/stale entry called rEFInd or SteamOS must not shadow this ESP.
# HD( is matched anywhere after the label's tab, not anchored to it: entries
# stored with a full device path (PciRoot(...)/Pci(...)/.../HD(...)) are just
# as valid as the bare HD(...) form efibootmgr >= 18 prints, and anchoring
# to the tab would make this script recreate (duplicate) such entries. This
# mirrors the deliberately unanchored HD\( match in the GUI's C++ regex.
loader_entry_ids() {
	local loader_re="$1"
	printf '%s\n' "$NVRAM_VERBOSE" \
		| grep -iE "^Boot[0-9A-Fa-f]{4}\\*?[[:space:]]+[^${TAB}]*${TAB}.*HD\\([0-9]+,GPT,${ESP_PARTUUID},[^)]*\\)/(File\\()?${loader_re}(\\)|[0-9A-Fa-f]{8}|$)" \
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

# Fallback for rEFInd only: rEFInd may legitimately live on an SD card or the
# Windows ESP rather than the Deck ESP (the pre-UUID-matching script supported
# that via its label match). If no entry matches the Deck ESP, accept an
# ACTIVE entry (the * is mandatory here) whose loader path ends in
# \EFI\refind\refind_x64.efi on ANY HD( partition, preferring one listed in
# BootOrder. Such an entry is only USED for BootNext; it is never created,
# deleted, activated, or otherwise modified.
find_refind_fallback_entry() {
	local candidates
	candidates="$(printf '%s\n' "$NVRAM_VERBOSE" \
		| grep -iE "^Boot[0-9A-Fa-f]{4}\\*[[:space:]]+[^${TAB}]*${TAB}.*HD\\([0-9]+,GPT,[0-9A-Fa-f-]+,[^)]*\\)/(File\\()?\\\\EFI\\\\refind\\\\refind_x64\\.efi(\\)|[0-9A-Fa-f]{8}|$)" \
		| sed -nE 's/^Boot([0-9A-Fa-f]{4}).*/\1/p' \
		| tr 'a-f' 'A-F')"
	[ -n "$candidates" ] || return 1
	first_in_boot_order "$candidates"
}

# Best-effort cleanup after a create that could not be re-verified (e.g. a
# future efibootmgr output format the matcher does not understand): find the
# entry that did not exist before the create and carries the exact label, and
# delete it. Without this the boot-time unit would add one duplicate entry
# per boot, forever; with it the failure is self-limiting.
delete_unverified_entry() {
	local label="$1" pre_ids="$2" id
	id="$(printf '%s\n' "$NVRAM_VERBOSE" \
		| grep -E "^Boot[0-9A-Fa-f]{4}\\*?[[:space:]]+${label}(${TAB}|$)" \
		| sed -nE 's/^Boot([0-9A-Fa-f]{4}).*/\1/p' \
		| tr 'a-f' 'A-F' \
		| grep -vxF -f <(printf '%s\n' "$pre_ids") \
		| tail -1)"
	if [ -n "$id" ]; then
		echo "Deleting the unverifiable new $label entry Boot$id so repeated runs cannot pile up duplicates." >&2
		sudo efibootmgr -b "$id" -B >/dev/null 2>&1
	fi
}

ENSURED_BOOTNUM=""
ENSURED_VIA_FALLBACK=0
ensure_loader_entry() {
	local label="$1" loader_path="$2" loader_re="$3" loader_file="$4" fallback_fn="${5:-}" bootnum pre_ids
	ENSURED_VIA_FALLBACK=0
	bootnum="$(find_loader_entry "$loader_re")"
	if [ -z "$bootnum" ] && [ -n "$fallback_fn" ]; then
		if bootnum="$("$fallback_fn")" && [ -n "$bootnum" ]; then
			echo "NOTE: no $label entry matches the Deck ESP; using existing non-Deck-ESP $label entry Boot$bootnum (e.g. SD card or Windows ESP) for BootNext without modifying it." >&2
			ENSURED_BOOTNUM="$bootnum"
			ENSURED_VIA_FALLBACK=1
			return 0
		fi
		bootnum=""
	fi
	if [ -z "$bootnum" ]; then
		# sudo: SteamOS mounts ESPs 0700 root:root, so an unprivileged test
		# (e.g. the documented manual recovery run as deck) cannot see the
		# loader and would wrongly report it missing.
		if ! sudo test -s "$loader_file"; then
			echo "ERROR: $label loader is missing or empty at $loader_file; refusing to create a broken NVRAM entry." >&2
			return 1
		fi
		echo "Creating the missing $label entry for the Deck ESP..." >&2
		pre_ids="$(printf '%s\n' "$NVRAM_VERBOSE" | sed -nE 's/^Boot([0-9A-Fa-f]{4}).*/\1/p' | tr 'a-f' 'A-F')"
		sudo efibootmgr -c -d "$ESP_DISK" -p "$ESP_PARTNUM" -L "$label" -l "$loader_path" >&2 \
			|| return 1
		refresh_nvram || return 1
		bootnum="$(find_loader_entry "$loader_re")"
		if [ -z "$bootnum" ]; then
			echo "ERROR: the new $label entry could not be verified against the Deck ESP." >&2
			delete_unverified_entry "$label" "$pre_ids"
			return 1
		fi
	fi
	if ! activate_entry "$bootnum"; then
		echo "ERROR: could not activate $label entry Boot$bootnum." >&2
		return 1
	fi
	ENSURED_BOOTNUM="$bootnum"
}

refresh_nvram || exit 1

# A SteamOS-side failure must not block restoring rEFInd or setting BootNext:
# warn and carry on. Only a rEFInd-side failure prevents BootNext below.
STEAMOS_BOOTNUM=""
if ensure_loader_entry "SteamOS" '\EFI\steamos\steamcl.efi' \
	'\\EFI\\steamos\\steamcl\.efi' /esp/efi/steamos/steamcl.efi; then
	STEAMOS_BOOTNUM="$ENSURED_BOOTNUM"
else
	echo "WARNING: could not verify or restore the SteamOS entry; continuing with rEFInd and BootNext anyway." >&2
fi

ensure_loader_entry "rEFInd" '\EFI\refind\refind_x64.efi' \
	'\\EFI\\refind\\refind_x64\.efi' /esp/efi/refind/refind_x64.efi \
	find_refind_fallback_entry || exit 1
REFIND_BOOTNUM="$ENSURED_BOOTNUM"
REFIND_VIA_FALLBACK="$ENSURED_VIA_FALLBACK"

# Force only the verified rEFInd entry for the next boot.
sudo efibootmgr -n "$REFIND_BOOTNUM" \
	|| { echo "ERROR: could not set verified rEFInd entry Boot$REFIND_BOOTNUM as BootNext." >&2; exit 1; }

STEAMOS_MSG="SteamOS Boot$STEAMOS_BOOTNUM"
[ -n "$STEAMOS_BOOTNUM" ] || STEAMOS_MSG="SteamOS (NOT verified, see warning above)"
REFIND_MSG="rEFInd Boot$REFIND_BOOTNUM for the Deck ESP"
[ "$REFIND_VIA_FALLBACK" = 0 ] || REFIND_MSG="rEFInd Boot$REFIND_BOOTNUM on a non-Deck-ESP partition"
echo -e "\nVerified $STEAMOS_MSG and $REFIND_MSG; BootNext is rEFInd.\n"
