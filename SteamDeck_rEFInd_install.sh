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
yes | sudo pacman -S refind
sudo refind-install

efibootmgr | tee ~/efibootlist.txt
grep -A0 'Windows' ~/efibootlist.txt | tee ~/windows_boot.txt
WINDOWS_BOOTNUM="$(grep -Eo '[0-9]{1,}' ~/windows_boot.txt | head -1)"
# Disable Windows EFI boot entry
sudo efibootmgr -b $WINDOWS_BOOTNUM -A
grep -A0 'rEFInd Boot Manager' ~/efibootlist.txt | tee ~/rEFInd_boot.txt
REFIND_BOOTNUM="$(grep -Eo '[0-9]{1,}' ~/rEFInd_boot.txt | head -1)"
# Delete rEFInd EFI boot entry from rEFInd-install... will be re-added later pointing to esp partition
sudo efibootmgr -b $REFIND_BOOTNUM -B
# Checking for duplicate rEFInd EFI boot entry, from previous script runs (or other sources)
efibootmgr | tee ~/efibootlist2.txt
grep -A0 'rEFInd' ~/efibootlist2.txt | tee ~/rEFInd_boot2.txt
REFIND_BOOTNUM_ALT="$(grep -Eo '[0-9]{1,}' ~/rEFInd_boot2.txt | head -1)"
grep -A0 'SteamOS' ~/efibootlist2.txt | tee ~/SteamOS_boot.txt
STEAMOS_BOOTNUM="$(grep -Eo '[0-9]{1,}' ~/SteamOS_boot.txt | head -1)"

# Deleting duplicate rEFInd boot entry, if one was found
re='^[0-9]+$'
if [[ $REFIND_BOOTNUM_ALT =~ $re ]]; then
	sudo efibootmgr -b $REFIND_BOOTNUM_ALT -B
fi

if ! [[ $STEAMOS_BOOTNUM =~ $re ]]; then
	# Recreate the missing SteamOS EFI entry (if missing)
	sudo efibootmgr -c -d /dev/nvme0n1 -p 1 -L "SteamOS" -l \\efi\\steamos\\steamcl.efi
fi

yes | sudo cp -rf /boot/efi/EFI/refind/ /esp/efi
# Renaming default rEFInd config file to keep for reference and backup
sudo mv /esp/efi/refind/refind.conf /esp/efi/refind/refind-bkp.conf
CURRENT_WD=$(pwd)
yes | sudo cp $CURRENT_WD/refind.conf /esp/efi/refind/refind.conf
yes | sudo cp -rf $CURRENT_WD/themes/ /esp/efi/refind
yes | sudo cp -rf $CURRENT_WD/icons/ /esp/efi/refind
sudo efibootmgr -c -d /dev/nvme0n1 -p 1 -L "rEFInd" -l \\efi\\refind\\refind_x64.efi

# Adding Systemctl daemon for rEFInd to be next boot priority
# Credit goes to Reddit user lucidludic for the idea :)
yes | sudo cp $CURRENT_WD/bootnext-refind.service /etc/systemd/system/bootnext-refind.service
sudo systemctl enable --now bootnext-refind.service

# Clean up temporary files, created for code clarity and readability
# Used primarily in automatically discovering boot entry numbers for efibootmgr commands
yes | rm ~/deck_passwd_status.txt
yes | rm ~/efibootlist.txt
yes | rm ~/efibootlist2.txt
yes | rm ~/windows_boot.txt
yes | rm ~/rEFInd_boot.txt
yes | rm ~/rEFInd_boot2.txt
yes | rm ~/SteamOS_boot.txt

sudo steamos-readonly enable
echo -e "\nrEFInd has now been installed (assuming pacman is functional)."
