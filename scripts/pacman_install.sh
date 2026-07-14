#!/bin/bash
# A simple Steam Deck rEFInd automated install script using Pacman
# Please make sure that a password exists for the deck user before running
(
	echo 0
	echo "# Installation started: Password prompt..."
	PASSWD="$(zenity --password --title="Enter sudo password" 2>/dev/null)"
	if ! printf '%s\n' "$PASSWD" | sudo -S -v 2>/dev/null; then
		zenity --error --title="Password Error" --text="Incorrect password provided.\nPlease try again providing the correct sudo password." --width=400 2>/dev/null
		echo 100
		echo "# Installation Failed. Please try again with correct sudo password"
		exit 1
	fi
	unset PASSWD
	sudo steamos-readonly disable
	echo 20
	echo "# Initializing Pacman repositories..."
	sudo pacman-key --init
	sudo pacman-key --populate archlinux
	echo 25
	echo "# Installing rEFInd package..."
	sudo pacman -Sy --noconfirm --needed refind
	sudo refind-install
	echo 50
	echo "# Installing files to /esp partition..."
	sudo cp -rf /boot/efi/EFI/refind/ /esp/efi
	if sudo test -f /esp/efi/refind/refind.conf; then
		sudo mv /esp/efi/refind/refind.conf /esp/efi/refind/refind-bkp.conf
	fi
	sudo cp -f "$HOME/.local/SteamDeck_rEFInd/GUI/refind.conf" /esp/efi/refind/refind.conf
	sudo cp -rf "$HOME/.local/SteamDeck_rEFInd/backgrounds/" /esp/efi/refind
	sudo cp -rf "$HOME/.local/SteamDeck_rEFInd/icons/" /esp/efi/refind
	echo 65
	echo "# Installing Xbox 360 controller driver..."
	# SkorionOS Xbox 360 USB controller UEFI driver: dropping it into rEFInd's
	# drivers_x64 folder makes wired/docked Xbox-style gamepads usable in the
	# boot menu. The driver auto-creates its own config at
	# \EFI\Xbox360\config.ini on first boot, so only the .efi is needed here.
	# NOTE: temporarily fetched from the jlobue10 fork (adds Legion Go 2 PIDs +
	# Ally lockup fix); revert to SkorionOS once upstream PR #6 is merged/released.
	XBOX360_DRV_URL="https://github.com/jlobue10/UsbXbox360Dxe/releases/latest/download/UsbXbox360Dxe.efi"
	XBOX360_DRV_TMP="$(mktemp)"
	sudo mkdir -p /esp/efi/refind/drivers_x64
	if curl -fsSL "$XBOX360_DRV_URL" -o "$XBOX360_DRV_TMP" 2>/dev/null \
		|| wget -q -O "$XBOX360_DRV_TMP" "$XBOX360_DRV_URL"; then
		sudo cp -f "$XBOX360_DRV_TMP" /esp/efi/refind/drivers_x64/UsbXbox360Dxe.efi
	else
		echo "# Warning: failed to download UsbXbox360Dxe.efi; skipping controller driver."
	fi
	rm -f "$XBOX360_DRV_TMP"
	echo 75
	echo "# Updating EFI boot entries..."
	# Resolve the ESP's disk and partition number from /esp instead of
	# hardcoding /dev/nvme0n1: 64GB Decks boot from eMMC (/dev/mmcblk0),
	# where the hardcoded path created broken entries. `lsblk -no PKNAME`
	# has been observed returning empty (util-linux 2.42), so fall back to
	# sysfs, where a partition's parent directory is its disk.
	# Diagnostics go to stderr: stdout is zenity's progress protocol.
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
	# Recreate the SteamOS entry if it is missing (SteamOS updates can drop
	# it). efibootmgr >= 18 appends "\t<device path>" after the label even
	# without -v, so label matches must allow an optional tab suffix.
	if ! efibootmgr | grep -qE '^Boot[0-9A-Fa-f]{4}\*? +SteamOS'; then
		sudo efibootmgr -c -d "$ESP_DISK" -p "$ESP_PARTNUM" -L "SteamOS" -l '\EFI\steamos\steamcl.efi' >/dev/null 2>&1 \
			|| echo "Warning: could not recreate the SteamOS boot entry." >&2
	fi
	# refind-install just created its own "rEFInd Boot Manager" entry;
	# remove it up front so the firmware list never carries it alongside
	# our "rEFInd" entry. Only that exact label is deleted pre-create --
	# plain "rEFInd" entries from previous installs are kept until the
	# new entry verifiably exists (see below).
	while read -r _num; do
		echo "Deleting refind-install's rEFInd Boot Manager entry Boot$_num..." >&2
		sudo efibootmgr -b "$_num" -B >/dev/null 2>&1 \
			|| echo "Warning: could not delete Boot$_num." >&2
	done < <(efibootmgr | sed -nE 's/^Boot([0-9A-Fa-f]{4})\*? +rEFInd Boot Manager(\t.*)?$/\1/p')
	# Create the new entry BEFORE deleting old rEFInd entries, so a failed
	# create can never leave the Deck with no rEFInd entry at all.
	NEW_BOOTNUM=""
	if CREATE_OUT="$(sudo efibootmgr -c -d "$ESP_DISK" -p "$ESP_PARTNUM" -L "rEFInd" -l '\EFI\refind\refind_x64.efi' 2>&1)"; then
		# efibootmgr -c puts the new entry first in BootOrder; use that
		# to identify it so the cleanup below never deletes it.
		NEW_BOOTNUM="$(efibootmgr | sed -nE 's/^BootOrder: ([0-9A-Fa-f]{4}).*/\1/p')"
		if [ -n "$NEW_BOOTNUM" ] && efibootmgr | sed -nE 's/^Boot([0-9A-Fa-f]{4})\*? +rEFInd(\t.*)?$/\1/p' | grep -qx "$NEW_BOOTNUM"; then
			while read -r _num; do
				[ "$_num" = "$NEW_BOOTNUM" ] && continue
				echo "Deleting old rEFInd entry Boot$_num..." >&2
				sudo efibootmgr -b "$_num" -B >/dev/null 2>&1 \
					|| echo "Warning: could not delete Boot$_num." >&2
			done < <(efibootmgr | sed -nE 's/^Boot([0-9A-Fa-f]{4})\*? +rEFInd.*/\1/p')
		else
			NEW_BOOTNUM=""
			echo "Warning: could not identify the new rEFInd entry; skipping cleanup of old entries." >&2
		fi
	else
		echo "ERROR: creating the rEFInd boot entry failed:" >&2
		printf '%s\n' "$CREATE_OUT" >&2
		echo "Existing rEFInd entries (if any) were left in place." >&2
	fi
	WINDOWS_BOOTNUM="$(efibootmgr | sed -nE 's/^Boot([0-9A-Fa-f]{4})\*? +Windows.*/\1/p' | head -1)"
	if [ -n "$WINDOWS_BOOTNUM" ]; then
		sudo efibootmgr -b "$WINDOWS_BOOTNUM" -A >/dev/null 2>&1 \
			|| echo "Warning: could not deactivate the Windows boot entry." >&2
	fi
	echo 90
	echo "# Enabling bootnext-refind service..."
	sudo cp -f "$HOME/.local/SteamDeck_rEFInd/bootnext-refind.service" /etc/systemd/system/bootnext-refind.service
	sudo systemctl daemon-reload
	sudo systemctl enable --now bootnext-refind.service
	if [ -n "$NEW_BOOTNUM" ]; then
		sudo efibootmgr -n "$NEW_BOOTNUM" >/dev/null 2>&1 \
			|| echo "Warning: could not set rEFInd as the next boot." >&2
	fi
	sudo steamos-readonly enable
	echo 100
	echo "# Installation finished."
) | zenity --title "Installing rEFInd with Pacman" --progress --no-cancel --width=500 2>/dev/null

# Verify the result from live NVRAM and show it both in the terminal (the GUI
# runs this in a transient xterm -- keep it open so the status can be read)
# and as a zenity dialog.
echo
echo "==================== Installation summary ===================="
FINAL_LIST="$(efibootmgr)"
printf '%s\n' "$FINAL_LIST"
echo "---------------------------------------------------------------"
REFIND_NUMS="$(printf '%s\n' "$FINAL_LIST" | sed -nE 's/^Boot([0-9A-Fa-f]{4})\*? +rEFInd.*/\1/p')"
FIRST_BOOT="$(printf '%s\n' "$FINAL_LIST" | sed -nE 's/^BootOrder: ([0-9A-Fa-f]{4}).*/\1/p')"
if [ -z "$REFIND_NUMS" ]; then
	echo "*** FAILED: no rEFInd entry exists in the firmware boot list. ***"
	echo "*** rEFInd will NOT be offered at boot -- see errors above.   ***"
	zenity --error --title="rEFInd installation failed" --width=450 \
		--text="No rEFInd boot entry exists in the firmware boot list.\nrEFInd will NOT be offered at boot. See the terminal window for details." 2>/dev/null
elif printf '%s\n' "$REFIND_NUMS" | grep -qx "$FIRST_BOOT"; then
	echo "SUCCESS: rEFInd is installed and first in the boot order."
	zenity --info --title="rEFInd installed" --width=400 \
		--text="rEFInd is installed and first in the boot order." 2>/dev/null
else
	echo "WARNING: a rEFInd entry exists but is NOT first in the boot order"
	echo "(boot order starts with Boot$FIRST_BOOT). The bootnext-refind"
	echo "service, if enabled, will still select rEFInd on the next boot."
	zenity --warning --title="rEFInd installed with warnings" --width=450 \
		--text="A rEFInd boot entry exists but is NOT first in the boot order." 2>/dev/null
fi
if [ -t 0 ]; then
	echo
	read -rp "Press Enter to close this window..."
fi
