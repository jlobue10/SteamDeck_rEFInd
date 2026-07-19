#!/bin/bash
# A simple Steam Deck rEFInd automated install script

passwd --status deck | tee ~/deck_passwd_status.txt
awk '{
	if($2 =="P")
    {
		print "Password is already set.";
	}
    else
    {
		print "Password has not been set. Please set password for deck user now.";
		system("passwd");
	}
}' ~/deck_passwd_status.txt

sudo steamos-readonly disable
sudo pacman-key --init
sudo pacman-key --populate archlinux
sudo pacman -Sy --noconfirm --needed refind
sudo refind-install

yes | sudo cp -rf /boot/efi/EFI/refind/ /esp/efi
# Renaming default rEFInd config file to keep for reference and backup
if sudo test -f /esp/efi/refind/refind.conf; then
	sudo mv /esp/efi/refind/refind.conf /esp/efi/refind/refind-bkp.conf
fi
CURRENT_WD=$(pwd)
yes | sudo cp $CURRENT_WD/refind-GUI.conf /esp/efi/refind/refind.conf
yes | sudo cp -rf $CURRENT_WD/backgrounds/ /esp/efi/refind
yes | sudo cp -rf $CURRENT_WD/icons/ /esp/efi/refind

# SkorionOS Xbox 360 USB controller UEFI driver: dropping it into rEFInd's
# drivers_x64 folder makes wired/docked Xbox-style gamepads usable in the
# boot menu. The driver auto-creates its own config at \EFI\Xbox360\config.ini
# on first boot, so only the .efi is needed here.
# NOTE: temporarily fetched from the jlobue10 fork (adds Legion Go 2 PIDs +
# Ally lockup fix); revert to SkorionOS once upstream PR #7 is merged/released.
XBOX360_DRV_URL="https://github.com/jlobue10/UsbXbox360Dxe/releases/latest/download/UsbXbox360Dxe.efi"
XBOX360_DRV_TMP="$(mktemp)"
echo "Downloading UsbXbox360Dxe.efi controller driver..."
sudo mkdir -p /esp/efi/refind/drivers_x64
if curl -fsSL "$XBOX360_DRV_URL" -o "$XBOX360_DRV_TMP" 2>/dev/null \
	|| wget -q -O "$XBOX360_DRV_TMP" "$XBOX360_DRV_URL"; then
	sudo cp -f "$XBOX360_DRV_TMP" /esp/efi/refind/drivers_x64/UsbXbox360Dxe.efi
else
	echo "Warning: failed to download UsbXbox360Dxe.efi; skipping controller driver." >&2
fi
rm -f "$XBOX360_DRV_TMP"

# TouchI2cDxe touchscreen UEFI driver (jlobue10/TouchI2cDxe): both Decks'
# FocalTech touch panels (OLED "Galileo" and, since driver v1.2.0, LCD
# "Jupiter") are HID-over-I2C, which a USB driver structurally cannot see;
# this driver produces AbsolutePointer so the rEFInd menu is touch-usable,
# including rotating the portrait touch matrix onto rEFInd's landscape mode.
# Like the controller driver, download failure is non-fatal.
DECK_PRODUCT="$(cat /sys/class/dmi/id/product_name 2>/dev/null)"
if [ "$DECK_PRODUCT" = "Galileo" ] || [ "$DECK_PRODUCT" = "Jupiter" ]; then
	TOUCH_DRV_URL="https://github.com/jlobue10/TouchI2cDxe/releases/latest/download/TouchI2cDxe.efi"
	TOUCH_DRV_TMP="$(mktemp)"
	echo "Downloading TouchI2cDxe.efi touchscreen driver..."
	if curl -fsSL "$TOUCH_DRV_URL" -o "$TOUCH_DRV_TMP" 2>/dev/null \
		|| wget -q -O "$TOUCH_DRV_TMP" "$TOUCH_DRV_URL"; then
		sudo cp -f "$TOUCH_DRV_TMP" /esp/efi/refind/drivers_x64/TouchI2cDxe.efi
	else
		echo "Warning: failed to download TouchI2cDxe.efi; skipping touchscreen driver." >&2
	fi
	rm -f "$TOUCH_DRV_TMP"
fi

echo "Updating EFI boot entries..."
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

# Recreate the SteamOS entry if it is missing (SteamOS updates can drop it).
# efibootmgr >= 18 appends "\t<device path>" after the label even without -v,
# so label matches must allow an optional tab suffix.
if ! efibootmgr | grep -qE '^Boot[0-9A-Fa-f]{4}\*? +SteamOS'; then
	sudo efibootmgr -c -d "$ESP_DISK" -p "$ESP_PARTNUM" -L "SteamOS" -l '\EFI\steamos\steamcl.efi' >/dev/null 2>&1 \
		|| echo "Warning: could not recreate the SteamOS boot entry." >&2
fi

# refind-install just created its own "rEFInd Boot Manager" entry; remove it
# up front so the firmware list never carries it alongside our "rEFInd"
# entry. Only that exact label is deleted pre-create -- plain "rEFInd"
# entries from previous installs are kept until the new entry verifiably
# exists (see below).
while read -r _num; do
	echo "Deleting refind-install's rEFInd Boot Manager entry Boot$_num..."
	sudo efibootmgr -b "$_num" -B >/dev/null 2>&1 \
		|| echo "Warning: could not delete Boot$_num." >&2
done < <(efibootmgr | sed -nE 's/^Boot([0-9A-Fa-f]{4})\*? +rEFInd Boot Manager(\t.*)?$/\1/p')
# Create the new entry BEFORE deleting old rEFInd entries, so a failed create
# can never leave the Deck with no rEFInd entry at all.
NEW_BOOTNUM=""
if CREATE_OUT="$(sudo efibootmgr -c -d "$ESP_DISK" -p "$ESP_PARTNUM" -L "rEFInd" -l '\EFI\refind\refind_x64.efi' 2>&1)"; then
	# efibootmgr -c puts the new entry first in BootOrder; use that to
	# identify it so the cleanup below never deletes it.
	NEW_BOOTNUM="$(efibootmgr | sed -nE 's/^BootOrder: ([0-9A-Fa-f]{4}).*/\1/p')"
	if [ -n "$NEW_BOOTNUM" ] && efibootmgr | sed -nE 's/^Boot([0-9A-Fa-f]{4})\*? +rEFInd(\t.*)?$/\1/p' | grep -qx "$NEW_BOOTNUM"; then
		while read -r _num; do
			[ "$_num" = "$NEW_BOOTNUM" ] && continue
			echo "Deleting old rEFInd entry Boot$_num..."
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

# Disable Windows EFI boot entry (rEFInd will chainload it instead)
WINDOWS_BOOTNUM="$(efibootmgr | sed -nE 's/^Boot([0-9A-Fa-f]{4})\*? +Windows.*/\1/p' | head -1)"
if [ -n "$WINDOWS_BOOTNUM" ]; then
	sudo efibootmgr -b "$WINDOWS_BOOTNUM" -A >/dev/null 2>&1 \
		|| echo "Warning: could not deactivate the Windows boot entry." >&2
fi

# Granting executable permissions to EFI entry restore script
chmod +x $CURRENT_WD/scripts/restore_EFI_entries.sh
mkdir -p $HOME/.local/SteamDeck_rEFInd/GUI
cp $CURRENT_WD/scripts/restore_EFI_entries.sh $HOME/.local/SteamDeck_rEFInd/

# Adding Systemctl daemon for rEFInd to be next boot priority
# Credit goes to Reddit user lucidludic for the idea :)
yes | sudo cp $CURRENT_WD/systemd/bootnext-refind.service /etc/systemd/system/bootnext-refind.service
sudo systemctl daemon-reload
sudo systemctl enable --now bootnext-refind.service
if [ -n "$NEW_BOOTNUM" ]; then
	sudo efibootmgr -n "$NEW_BOOTNUM" >/dev/null 2>&1 \
		|| echo "Warning: could not set rEFInd as the next boot." >&2
fi

# Clean up temporary files, created for code clarity
yes | rm $HOME/deck_passwd_status.txt

sudo steamos-readonly enable

# Verify the result from live NVRAM rather than trusting the steps above.
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
elif printf '%s\n' "$REFIND_NUMS" | grep -qx "$FIRST_BOOT"; then
	echo "SUCCESS: rEFInd is installed and first in the boot order."
else
	echo "WARNING: a rEFInd entry exists but is NOT first in the boot order"
	echo "(boot order starts with Boot$FIRST_BOOT). The bootnext-refind"
	echo "service will still select rEFInd on the next boot."
fi
echo -e "\nrEFInd has now been installed (assuming pacman is functional).\n"
