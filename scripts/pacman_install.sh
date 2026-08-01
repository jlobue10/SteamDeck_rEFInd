#!/bin/bash
# A simple Steam Deck rEFInd automated install script using Pacman
# Please make sure that a password exists for the deck user before running
(
	READONLY_DISABLED=0
	restore_readonly() {
		status=$?
		trap - EXIT
		if [ "$READONLY_DISABLED" -eq 1 ]; then
			if ! sudo steamos-readonly enable; then
				echo "CRITICAL: SteamOS read-only mode could not be restored. Run 'sudo steamos-readonly enable' before rebooting." >&2
				exit 70
			fi
		fi
		exit "$status"
	}
	trap restore_readonly EXIT
	set -e

	echo 0
	echo "# Installation started: Password prompt..."
	PASSWD="$(zenity --password --title="Enter sudo password" 2>/dev/null)" || PASSWD=""
	if ! printf '%s\n' "$PASSWD" | sudo -S -v 2>/dev/null; then
		zenity --error --title="Password Error" --text="Incorrect password provided.\nPlease try again providing the correct sudo password." --width=400 2>/dev/null
		echo 100
		echo "# Installation Failed. Please try again with correct sudo password"
		exit 1
	fi
	unset PASSWD
	if ! sudo steamos-readonly disable; then
		echo "# Installation failed: could not disable SteamOS read-only mode."
		exit 1
	fi
	READONLY_DISABLED=1
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
	# Themes are optional (only used when refind.conf includes
	# themes/active_theme.conf), so a failed copy warns instead of aborting.
	sudo cp -rf "$HOME/.local/SteamDeck_rEFInd/themes/" /esp/efi/refind \
		|| echo "# Warning: could not copy themes/ to the ESP; theming will be unavailable."
	echo 65
	echo "# Installing Xbox 360 controller driver..."
	# SkorionOS Xbox 360 USB controller UEFI driver: dropping it into rEFInd's
	# drivers_x64 folder makes wired/docked Xbox-style gamepads usable in the
	# boot menu. The driver auto-creates its own config at
	# \EFI\Xbox360\config.ini on first boot, so only the .efi is needed here.
	# NOTE: temporarily fetched from the jlobue10 fork (adds Legion Go 2 PIDs +
	# Ally lockup fix); revert to SkorionOS once upstream PR #7 is merged/released.
	XBOX360_DRV_URL="https://github.com/jlobue10/UsbXbox360Dxe/releases/latest/download/UsbXbox360Dxe.efi"
	XBOX360_DRV_TMP="$(mktemp)"
	sudo mkdir -p /esp/efi/refind/drivers_x64
	# A truncated or HTML error-page download must not reach the ESP:
	# require a non-empty file with the PE "MZ" signature before copying.
	if { curl -fsSL "$XBOX360_DRV_URL" -o "$XBOX360_DRV_TMP" 2>/dev/null \
		|| wget -q -O "$XBOX360_DRV_TMP" "$XBOX360_DRV_URL"; } \
		&& [ -s "$XBOX360_DRV_TMP" ] && [ "$(head -c2 "$XBOX360_DRV_TMP")" = "MZ" ]; then
		sudo cp -f "$XBOX360_DRV_TMP" /esp/efi/refind/drivers_x64/UsbXbox360Dxe.efi
	else
		echo "# Warning: failed to download UsbXbox360Dxe.efi; skipping controller driver."
	fi
	rm -f "$XBOX360_DRV_TMP"
	# TouchI2cDxe touchscreen UEFI driver (jlobue10/TouchI2cDxe): both Decks'
	# FocalTech touch panels (OLED "Galileo" and, since driver v1.2.0, LCD
	# "Jupiter") are HID-over-I2C, which a USB driver structurally cannot
	# see; this driver produces AbsolutePointer so the rEFInd menu is
	# touch-usable, including rotating the portrait touch matrix onto
	# rEFInd's landscape mode. Like the controller driver, download failure
	# is non-fatal.
	DECK_PRODUCT="$(cat /sys/class/dmi/id/product_name 2>/dev/null)" || true
	if [ "$DECK_PRODUCT" = "Galileo" ] || [ "$DECK_PRODUCT" = "Jupiter" ]; then
		echo "# Installing touchscreen driver..."
		TOUCH_DRV_URL="https://github.com/jlobue10/TouchI2cDxe/releases/latest/download/TouchI2cDxe.efi"
		TOUCH_DRV_TMP="$(mktemp)"
		# A truncated or HTML error-page download must not reach the ESP:
		# require a non-empty file with the PE "MZ" signature before copying.
		if { curl -fsSL "$TOUCH_DRV_URL" -o "$TOUCH_DRV_TMP" 2>/dev/null \
			|| wget -q -O "$TOUCH_DRV_TMP" "$TOUCH_DRV_URL"; } \
			&& [ -s "$TOUCH_DRV_TMP" ] && [ "$(head -c2 "$TOUCH_DRV_TMP")" = "MZ" ]; then
			sudo cp -f "$TOUCH_DRV_TMP" /esp/efi/refind/drivers_x64/TouchI2cDxe.efi
		else
			echo "# Warning: failed to download TouchI2cDxe.efi; skipping touchscreen driver."
		fi
		rm -f "$TOUCH_DRV_TMP"
	fi
	echo 75
	echo "# Updating EFI boot entries..."
	# Snapshot the current NVRAM boot entries before changing them: a copy on
	# disk makes manual recovery trivial if anything goes sideways.
	NVRAM_BK_DIR="$HOME/.local/SteamDeck_rEFInd/nvram-backups"
	if mkdir -p "$NVRAM_BK_DIR" 2>/dev/null; then
		# Best-effort: a failed snapshot must not abort the install (set -e).
		efibootmgr -v > "$NVRAM_BK_DIR/efibootmgr-$(date +%Y%m%d-%H%M%S).txt" 2>/dev/null || true
		# Keep the ten most recent snapshots.
		ls -1t "$NVRAM_BK_DIR"/efibootmgr-*.txt 2>/dev/null | tail -n +11 | xargs -r rm -f
	fi
	# Resolve the ESP's disk and partition number from /esp instead of
	# hardcoding /dev/nvme0n1: 64GB Decks boot from eMMC (/dev/mmcblk0),
	# where the hardcoded path created broken entries. `lsblk -no PKNAME`
	# has been observed returning empty (util-linux 2.42), so fall back to
	# sysfs, where a partition's parent directory is its disk.
	# Diagnostics go to stderr: stdout is zenity's progress protocol.
	ESP_DEV="$(findmnt -no SOURCE /esp 2>/dev/null | grep -m1 "^/dev/")" || true
	ESP_PART="$(basename "$ESP_DEV")"
	ESP_PARTNUM="$(cat "/sys/class/block/$ESP_PART/partition" 2>/dev/null)" || true
	ESP_PARTUUID="$(lsblk -rno PARTUUID "$ESP_DEV" 2>/dev/null | head -1)"
	if [ -z "$ESP_PARTUUID" ]; then
		ESP_PARTUUID="$(blkid -s PARTUUID -o value "$ESP_DEV" 2>/dev/null)" || true
	fi
	ESP_PARENT="$(lsblk -no PKNAME "$ESP_DEV" 2>/dev/null | head -1)"
	if [ -z "$ESP_PARENT" ] && [ -n "$ESP_PART" ]; then
		ESP_PARENT="$(basename "$(dirname "$(readlink -f "/sys/class/block/$ESP_PART")")")"
	fi
	ESP_DISK="/dev/$ESP_PARENT"
	if [ ! -b "$ESP_DISK" ] || [ -z "$ESP_PARTNUM" ] ||
		! [[ "$ESP_PARTUUID" =~ ^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$ ]]; then
		echo "ERROR: could not safely resolve the ESP's disk, partition, and PARTUUID from /esp; refusing to modify NVRAM." >&2
		exit 1
	fi
	# Recreate the SteamOS entry if it is missing (SteamOS updates can drop
	# it). efibootmgr >= 18 appends "\t<device path>" after the label even
	# without -v, so label matches must allow an optional tab suffix.
	if ! efibootmgr | grep -qE '^Boot[0-9A-Fa-f]{4}\*? +SteamOS'; then
		sudo efibootmgr -c -d "$ESP_DISK" -p "$ESP_PARTNUM" -L "SteamOS" -l '\EFI\steamos\steamcl.efi' >/dev/null 2>&1 \
			|| echo "Warning: could not recreate the SteamOS boot entry." >&2
	fi
	# Keep every existing rEFInd/Windows fallback until a new entry has been
	# matched to both the expected ESP PARTUUID and loader path.
	NEW_BOOTNUM=""
	REFIND_READY=0
	if ! sudo test -s /esp/efi/refind/refind_x64.efi ||
		! sudo test -s /esp/efi/refind/refind.conf; then
		echo "ERROR: the replacement rEFInd loader or configuration is missing/empty; existing boot entries were left active." >&2
		exit 1
	fi
	if CREATE_OUT="$(sudo efibootmgr -c -d "$ESP_DISK" -p "$ESP_PARTNUM" -L "rEFInd" -l '\EFI\refind\refind_x64.efi' 2>&1)"; then
		# efibootmgr -c puts the new entry first in BootOrder; use that
		# to identify it, then verify the verbose firmware device path.
		CANDIDATE_BOOTNUM="$(efibootmgr | sed -nE 's/^BootOrder: ([0-9A-Fa-f]{4}).*/\1/p')"
		NVRAM_VERBOSE="$(efibootmgr -v 2>/dev/null)" || NVRAM_VERBOSE=""
		if [ -n "$CANDIDATE_BOOTNUM" ] && printf '%s\n' "$NVRAM_VERBOSE" | grep -qiE \
			"^Boot${CANDIDATE_BOOTNUM}\\*?[[:space:]]+rEFInd[[:space:]]+HD\\([0-9]+,GPT,${ESP_PARTUUID},[^)]*\\)/(File\\()?\\\\EFI\\\\refind\\\\refind_x64\\.efi"; then
			NEW_BOOTNUM="$CANDIDATE_BOOTNUM"
			REFIND_READY=1
			while read -r _num; do
				[ "$_num" = "$NEW_BOOTNUM" ] && continue
				echo "Deleting old rEFInd entry Boot$_num..." >&2
				sudo efibootmgr -b "$_num" -B >/dev/null 2>&1 \
					|| echo "Warning: could not delete Boot$_num." >&2
			done < <(efibootmgr | sed -nE 's/^Boot([0-9A-Fa-f]{4})\*? +rEFInd.*/\1/p')
		else
			echo "ERROR: the new rEFInd entry could not be verified against the target ESP and loader path." >&2
			echo "Existing rEFInd and Windows entries were left active." >&2
		fi
	else
		echo "ERROR: creating the rEFInd boot entry failed:" >&2
		printf '%s\n' "$CREATE_OUT" >&2
		echo "Existing rEFInd and Windows entries were left active." >&2
	fi
	if [ "$REFIND_READY" -ne 1 ]; then
		exit 1
	fi
	WINDOWS_BOOTNUM="$(efibootmgr | sed -nE 's/^Boot([0-9A-Fa-f]{4})\*? +Windows Boot Manager(\t.*)?$/\1/p' | head -1)"
	if [ -n "$WINDOWS_BOOTNUM" ] && [ -n "$NEW_BOOTNUM" ]; then
		sudo efibootmgr -b "$WINDOWS_BOOTNUM" -A >/dev/null 2>&1 \
			|| echo "Warning: could not deactivate the Windows boot entry." >&2
	fi
	echo 90
	echo "# Enabling bootnext-refind service..."
	sudo systemctl daemon-reload
	sudo systemctl enable --now bootnext-refind.service
	if [ -n "$NEW_BOOTNUM" ]; then
		sudo efibootmgr -n "$NEW_BOOTNUM" >/dev/null 2>&1 \
			|| echo "Warning: could not set rEFInd as the next boot." >&2
	fi
	if ! sudo steamos-readonly enable; then
		echo "# Installation failed: could not restore SteamOS read-only mode."
		exit 1
	fi
	READONLY_DISABLED=0
	echo 100
	echo "# Installation finished."
) | zenity --title "Installing rEFInd with Pacman" --progress --no-cancel --width=500 2>/dev/null
INSTALL_STATUS=${PIPESTATUS[0]}
if [ "$INSTALL_STATUS" -ne 0 ]; then
	if [ "$INSTALL_STATUS" -eq 70 ]; then
		echo "Installation aborted and SteamOS read-only mode could not be restored." >&2
		zenity --error --title="rEFInd installation failed" --width=450 \
			--text="Installation stopped and read-only mode could not be restored. Run 'sudo steamos-readonly enable' before rebooting." 2>/dev/null
	else
		echo "Installation aborted safely; SteamOS read-only protection is intact."
		zenity --error --title="rEFInd installation failed" --width=450 \
			--text="Installation stopped before completion. SteamOS read-only protection is intact. See the terminal window for details." 2>/dev/null
	fi
	exit "$INSTALL_STATUS"
fi

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
