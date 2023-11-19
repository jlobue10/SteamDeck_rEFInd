#!/bin/bash
# An alternate,  without pacman, rEFInd installation
# Please make sure that a password exists for the deck user before running

xterm -e "
CURRENT_WD=$(pwd) &&
 cd ~/Downloads &&
 wget https://sourceforge.net/projects/refind/files/0.14.0.2/refind-bin-gnuefi-0.14.0.2.zip &&
 unzip -a refind-bin-gnuefi-0.14.0.2.zip &&
 sudo steamos-readonly disable &&
 sudo mkdir -p /esp/efi/refind &&
 sudo cp -f ~/Downloads/refind-bin-0.14.0.2/refind/refind_x64.efi /esp/efi/refind/ &&
 sudo cp -rf ~/Downloads/refind-bin-0.14.0.2/refind/drivers_x64/ /esp/efi/refind &&
 sudo cp -rf ~/Downloads/refind-bin-0.14.0.2/refind/tools_x64/ /esp/efi/refind &&
 sudo ./refind-bin-0.14.0.2/refind-install &&
 sudo cp -rf ~/Downloads/refind-bin-0.14.0.2/refind/icons/ /esp/efi/refind &&
 sudo cp -rf ~/Downloads/refind-bin-0.14.0.2/fonts/ /esp/efi/refind &&
 sudo cp -f $CURRENT_WD/refind.conf /esp/efi/refind/refind.conf &&
 sudo cp -rf $CURRENT_WD/themes/ /esp/efi/refind &&
 sudo cp -rf $CURRENT_WD/icons/ /esp/efi/refind &&
 efibootmgr | tee ~/efibootlist.txt &&
 WINDOWS_BOOTNUM=\"$(grep -A0 'Windows' ~/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)\" &&
 sudo efibootmgr -b $WINDOWS_BOOTNUM -A &&
 REFIND_BOOTNUM=\"$(grep -A0 'rEFInd Boot Manager' ~/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)\" &&
 sudo efibootmgr -b $REFIND_BOOTNUM -B &&
 REFIND_BOOTNUM_ALT=\"$(grep -A0 'rEFInd' ~/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)\" &&
 STEAMOS_BOOTNUM=\"$(grep -A0 'SteamOS' ~/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)\" &&
 re='^[0-9]+$' &&
 if [[ $REFIND_BOOTNUM_ALT =~ $re ]]; then sudo efibootmgr -b $REFIND_BOOTNUM_ALT -B fi &&
 if ! [[ $STEAMOS_BOOTNUM =~ $re ]]; then sudo efibootmgr -c -d /dev/nvme0n1 -p 1 -L "SteamOS" -l \\EFI\\steamos\\steamcl.efi fi &&
 sudo efibootmgr -c -d /dev/nvme0n1 -p 1 -L "rEFInd" -l \\EFI\\refind\\refind_x64.efi &&
 yes | rm ~/efibootlist.txt &&
 sudo steamos-readonly enable &&
 echo -e \"\nrEFInd has now been installed from Sourceforge.\n; $SHELL\""
