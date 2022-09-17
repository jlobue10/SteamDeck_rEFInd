#!/bin/bash
# A simple Steam Deck rEFInd automated install script

passwd --status deck | tee ~/deck_passwd_status.txt
# Run passwd command if no passwd is set
awk '{
	if($2 =="P")
    	{
		print "Password is already set."
	}
    	else
    	{
		passwd
	}
    }' ~/deck_passwd_status.txt

sudo btrfs property set -ts / ro false
sudo pacman-key --init
sudo pacman-key --populate archlinux
yes | sudo pacman -S refind
sudo refind-install

efibootmgr | tee ~/efibootlist.txt
grep -A0 'Windows' ~/efibootlist.txt | tee ~/windows_boot.txt
WINDOWS_BOOTNUM="$(grep -Eo '[0-9]{1,}' ~/windows_boot.txt)"
# Disable Windows EFI boot entry
sudo efibootmgr -b $WINDOWS_BOOTNUM -A
#grep -A0 'rEFInd Boot Manager' ~/efibootlist.txt | tee ~/rEFInd_boot.txt
grep -A0 'rEFInd' ~/efibootlist.txt | tee ~/rEFInd_boot.txt
REFIND_BOOTNUM="$(grep -Eo '[0-9]{1,}' ~/rEFInd_boot.txt)"
# Delete rEFInd EFI boot entry from rEFInd-install... will be re-added later pointing to esp partition
sudo efibootmgr -b $REFIND_BOOTNUM -B
yes | sudo cp -rf /boot/efi/EFI/refind/ /esp/efi
# Renaming default rEFInd config file to keep for reference and backup
sudo mv /esp/efi/refind/refind.conf /esp/efi/refind/refind-bkp.conf
CURRENT_WD=$(pwd)
yes | sudo cp $CURRENT_WD/refind.conf /esp/efi/refind/refind.conf
yes | sudo cp -rf $CURRENT_WD/themes/ /esp/efi/refind
yes | sudo cp -rf $CURRENT_WD/icons/ /esp/efi/refind
sudo efibootmgr -c -d /dev/nvme0n1 -p 1 -L "rEFInd" -l \\efi\\refind\\refind_x64.efi

# Clean up temporary files
yes | rm ~/deck_passwd_status.txt
yes | rm ~/efibootlist.txt
yes | rm ~/windows_boot.txt
yes | rm ~/rEFInd_boot.txt

sudo btrfs property set -ts / ro true
echo "rEFInd has now been installed."
