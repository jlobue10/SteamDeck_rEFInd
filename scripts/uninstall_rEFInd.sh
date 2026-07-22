#!/bin/bash
# Fully removes the Deck-side rEFInd install -- the counterpart of
# Windows/GUI/uninstall_rEFInd.ps1 (and the sibling rEFInd_GUI repo's
# uninstall_rEFInd.sh):
#   - disables bootnext-refind and rEFInd_bg_randomizer first (bootnext-refind
#     recreates rEFInd boot entries on every boot, so it must go before any
#     NVRAM cleanup)
#   - deletes the rEFInd boot entries that target the Deck's ESP (rEFInd
#     entries pointing at another ESP -- e.g. a Windows-side install on an SD
#     card -- are reported and left alone)
#   - re-activates the Windows boot entry the installers deactivated
#   - removes EFI/refind and EFI/Xbox360 from the ESP and refind-install's
#     /boot/refind_linux.conf (pass --keep-esp-files to keep the files and
#     only undo the services/boot entries)
#   - with --remove-app, also removes the SteamDeck_rEFInd package (pacman,
#     inside the steamos-readonly bracket), ~/.local/SteamDeck_rEFInd,
#     /etc/SteamDeck_rEFInd, and the passwordless-config sudoers rule
#
# Run as the deck user: ./uninstall_rEFInd.sh [--keep-esp-files] [--remove-app]
# (privileged steps use sudo per command, like the install scripts).

KEEP_ESP_FILES=0
REMOVE_APP=0
for arg in "$@"; do
	case "$arg" in
		--keep-esp-files) KEEP_ESP_FILES=1 ;;
		--remove-app) REMOVE_APP=1 ;;
		-h|--help)
			sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'
			exit 0
			;;
		*)
			echo "Unknown option: $arg (try --help)" >&2
			exit 1
			;;
	esac
done

RefindLoaderRegex='\\refind\\refind[^\\]*\.efi'

echo "Removing the Deck-side rEFInd install..."

# 1. Disable the systemd units first: bootnext-refind re-creates the rEFInd
# boot entry (restore_EFI_entries.sh) on every boot, which would undo the
# cleanup below on the next reboot.
for unit in bootnext-refind.service rEFInd_bg_randomizer.service; do
	if systemctl is-enabled "$unit" >/dev/null 2>&1; then
		sudo systemctl disable --now "$unit" >/dev/null 2>&1
		echo "Disabled $unit."
	fi
done

# 2. Resolve the Deck's ESP (mounted at /esp on SteamOS).
# /esp may sit behind a systemd automount (SteamOS 3.9): resolving /esp/.
# establishes the vfat mount; the autofs row findmnt then lists first is
# skipped by taking the last (most recent) FSTYPE row.
stat /esp/. >/dev/null 2>&1
ESP_MP="$(findmnt -no TARGET --target /esp 2>/dev/null | head -1)"
case "$(findmnt -no FSTYPE --target /esp 2>/dev/null | tail -1)" in
	vfat|msdos|fat) ;; *) ESP_MP="" ;;
esac
ESP_PARTUUID=""
if [ -n "$ESP_MP" ]; then
	ESP_DEV="$(findmnt -no SOURCE "$ESP_MP" 2>/dev/null | grep -m1 "^/dev/")"
	[ -n "$ESP_DEV" ] && ESP_PARTUUID="$(lsblk -rno PARTUUID "$ESP_DEV" 2>/dev/null | head -1 | tr 'A-F' 'a-f')"
fi
if [ -z "$ESP_MP" ] || [ -z "$ESP_PARTUUID" ]; then
	echo "Warning: could not resolve the Deck's EFI System Partition;" >&2
	echo "skipping boot entry and ESP file cleanup." >&2
fi

# 3. Delete rEFInd boot entries that target this ESP; report entries pointing
# elsewhere (e.g. a Windows-side install on removable media) and leave them
# alone. Match by loader path so both "rEFInd" and refind-install's
# "rEFInd Boot Manager" labels are covered.
REMOVED_ENTRIES=""
FOREIGN_ENTRIES=""
# Snapshot the current NVRAM boot entries before changing them: a copy on
# disk makes manual recovery trivial if anything goes sideways.
NVRAM_BK_DIR="$HOME/.local/SteamDeck_rEFInd/nvram-backups"
if mkdir -p "$NVRAM_BK_DIR" 2>/dev/null; then
	efibootmgr -v > "$NVRAM_BK_DIR/efibootmgr-$(date +%Y%m%d-%H%M%S).txt" 2>/dev/null
	# Keep the ten most recent snapshots.
	ls -1t "$NVRAM_BK_DIR"/efibootmgr-*.txt 2>/dev/null | tail -n +11 | xargs -r rm -f
fi

if [ -n "$ESP_PARTUUID" ] && command -v efibootmgr >/dev/null 2>&1; then
	while IFS= read -r line; do
		num="$(printf '%s\n' "$line" | sed -nE 's/^Boot([0-9A-Fa-f]{4})\*?[[:space:]].*/\1/p')"
		[ -n "$num" ] || continue
		printf '%s\n' "$line" | grep -qiE "$RefindLoaderRegex" || continue
		label="$(printf '%s\n' "$line" | sed -E 's/^Boot[0-9A-Fa-f]{4}\*?[[:space:]]+//; s/[[:space:]]*(HD\(|\t).*$//')"
		entry_uuid="$(printf '%s\n' "$line" | grep -oiE 'GPT,[0-9a-fA-F-]{36}' | head -1 | cut -d, -f2 | tr 'A-F' 'a-f')"
		if [ "$entry_uuid" = "$ESP_PARTUUID" ]; then
			if sudo efibootmgr -b "$num" -B >/dev/null 2>&1; then
				echo "Deleted boot entry Boot$num ('$label')."
				REMOVED_ENTRIES="$REMOVED_ENTRIES Boot$num"
			else
				echo "Warning: could not delete boot entry Boot$num ('$label')." >&2
			fi
		else
			FOREIGN_ENTRIES="$FOREIGN_ENTRIES Boot$num('$label')"
		fi
	done < <(efibootmgr -v 2>/dev/null)
	if [ -n "$FOREIGN_ENTRIES" ]; then
		echo "Left untouched (rEFInd on another ESP):$FOREIGN_ENTRIES"
	fi
fi

# 4. Re-activate the Windows boot entry the installers deactivated (inactive
# entries print without the '*' after Boot####).
if command -v efibootmgr >/dev/null 2>&1; then
	while read -r _num; do
		if sudo efibootmgr -b "$_num" -a >/dev/null 2>&1; then
			echo "Re-activated the Windows boot entry Boot$_num."
		else
			echo "Warning: could not re-activate the Windows boot entry Boot$_num." >&2
		fi
	done < <(efibootmgr | sed -nE 's/^Boot([0-9A-Fa-f]{4})[[:space:]]+Windows.*/\1/p')
fi

# 5. Remove rEFInd's files (and the Xbox 360 driver's config dir) from the
# ESP, plus the kernel-options file refind-install drops in /boot (which is
# on the read-only SteamOS rootfs, hence the steamos-readonly bracket).
if [ "$KEEP_ESP_FILES" -eq 0 ] && [ -n "$ESP_MP" ]; then
	for d in "$ESP_MP/EFI/refind" "$ESP_MP/EFI/Xbox360"; do
		if sudo test -d "$d"; then
			sudo rm -rf "$d"
			echo "Removed ${d#"$ESP_MP"/} from the EFI System Partition."
		fi
	done
	if sudo test -f /boot/refind_linux.conf; then
		sudo steamos-readonly disable
		sudo rm -f /boot/refind_linux.conf
		sudo steamos-readonly enable
		echo "Removed /boot/refind_linux.conf."
	fi
fi

# 6. Remove the pacman-installed refind package (the Sourceforge install path
# leaves no package; its rootfs files are covered by the ESP cleanup above).
if pacman -Qq refind >/dev/null 2>&1; then
	sudo steamos-readonly disable
	sudo pacman -R --noconfirm refind
	sudo steamos-readonly enable
	echo "Removed the refind pacman package."
fi

# 7. Optionally remove the GUI app itself.
if [ "$REMOVE_APP" -eq 1 ]; then
	echo "Removing the SteamDeck_rEFInd app..."
	if pacman -Qq SteamDeck_rEFInd >/dev/null 2>&1; then
		sudo steamos-readonly disable
		sudo pacman -R --noconfirm SteamDeck_rEFInd
		sudo steamos-readonly enable
	else
		echo "No installed SteamDeck_rEFInd package found; removing files only."
	fi
	rm -rf "$HOME/.local/SteamDeck_rEFInd"
	rm -f "$HOME/Desktop/SteamDeck_rEFInd.desktop" "$HOME/Desktop/refind_GUI.desktop"
	# The passwordless-config pieces install-GUI.sh put on the /etc overlay
	# (remove the sudoers rule before the root-owned script it whitelists).
	sudo rm -f /etc/sudoers.d/zz_SteamDeck_rEFInd_install_config
	sudo rm -rf /etc/SteamDeck_rEFInd
	echo "Removed the app data, /etc/SteamDeck_rEFInd, the sudoers rule, and desktop shortcuts."
fi

# Summary, read back from live NVRAM.
echo
echo "==================== Uninstall summary ===================="
if command -v efibootmgr >/dev/null 2>&1; then
	efibootmgr
	echo "------------------------------------------------------------"
fi
if [ -n "$REMOVED_ENTRIES" ]; then
	echo "Removed entries:$REMOVED_ENTRIES"
else
	echo "No rEFInd boot entries for the Deck's ESP were present."
fi
echo "Done."
