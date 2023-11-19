#!/bin/bash
# An alternate,  without pacman, rEFInd installation
# Please make sure that a password exists for the deck user before running

CURRENT_WD=$(pwd)
cd ~/Downloads
wget https://sourceforge.net/projects/refind/files/0.14.0.2/refind-bin-gnuefi-0.14.0.2.zip
unzip -a refind-bin-gnuefi-0.14.0.2.zip

sudo steamos-readonly disable
sudo mkdir -p /esp/efi/refind
sudo cp -f ~/Downloads/refind-bin-0.14.0.2/refind/refind_x64.efi /esp/efi/refind/
sudo cp -rf ~/Downloads/refind-bin-0.14.0.2/refind/drivers_x64/ /esp/efi/refind
sudo cp -rf ~/Downloads/refind-bin-0.14.0.2/refind/tools_x64/ /esp/efi/refind
sudo ./refind-bin-0.14.0.2/refind-install
sudo cp -rf ~/Downloads/refind-bin-0.14.0.2/refind/icons/ /esp/efi/refind
sudo cp -rf ~/Downloads/refind-bin-0.14.0.2/fonts/ /esp/efi/refind
sudo cp -f $CURRENT_WD/refind.conf /esp/efi/refind/refind.conf
sudo cp -rf $CURRENT_WD/themes/ /esp/efi/refind
sudo cp -rf $CURRENT_WD/icons/ /esp/efi/refind

efibootmgr | tee ~/efibootlist.txt
WINDOWS_BOOTNUM="$(grep -A0 'Windows' ~/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)"
# Disable Windows EFI boot entry
sudo efibootmgr -b $WINDOWS_BOOTNUM -A
REFIND_BOOTNUM="$(grep -A0 'rEFInd Boot Manager' ~/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)"
# Delete rEFInd EFI boot entry from rEFInd-install... will be re-added later pointing to esp partition
sudo efibootmgr -b $REFIND_BOOTNUM -B
# Checking for duplicate rEFInd EFI boot entry, from previous script runs (or other sources)
REFIND_BOOTNUM_ALT="$(grep -A0 'rEFInd' ~/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)"
STEAMOS_BOOTNUM="$(grep -A0 'SteamOS' ~/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)"

# Deleting duplicate rEFInd boot entry, if one was found
re='^[0-9]+$'
if [[ $REFIND_BOOTNUM_ALT =~ $re ]]; then
	sudo efibootmgr -b $REFIND_BOOTNUM_ALT -B
fi

if ! [[ $STEAMOS_BOOTNUM =~ $re ]]; then
	# Recreate the missing SteamOS EFI entry (if missing)
	sudo efibootmgr -c -d /dev/nvme0n1 -p 1 -L "SteamOS" -l \\EFI\\steamos\\steamcl.efi
fi

# Manually adding rEFInd EFI boot entry
sudo efibootmgr -c -d /dev/nvme0n1 -p 1 -L "rEFInd" -l \\EFI\\refind\\refind_x64.efi

# Clean up temporary files, created for code clarity
yes | rm ~/efibootlist.txt

sudo steamos-readonly enable

echo -e "\nrEFInd has now been installed from Sourceforge.\n"
