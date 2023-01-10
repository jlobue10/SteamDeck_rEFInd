#!/bin/bash
# A simple script to restore missing SteamOS and/ or rEFInd EFI entries

efibootmgr | tee ~/efirestorelist.txt
STEAMOS_RESTORE_NUM="$(grep -A0 'SteamOS' ~/efirestorelist.txt | grep -Eo '[0-9]{1,4}' | head -1)"
REFIND_RESTORE_NUM="$(grep -A0 'rEFInd' ~/efirestorelist.txt | grep -Eo '[0-9]{1,4}' | head -1)"

re='^[0-9]+$'
if ! [[ $STEAMOS_RESTORE_NUM =~ $re ]]; then
	# Recreate the missing SteamOS EFI entry
	sudo efibootmgr -c -d /dev/nvme0n1 -p 1 -L "SteamOS" -l \\EFI\\steamos\\steamcl.efi
fi

if ! [[ $REFIND_RESTORE_NUM =~ $re ]]; then
	# Recreate the missing rEFInd EFI entry
	sudo efibootmgr -c -d /dev/nvme0n1 -p 1 -L "rEFInd" -l \\EFI\\refind\\refind_x64.efi
fi

yes | rm ~/efirestorelist.txt
echo -e "\nMissing EFI entries for SteamOS and/ or rEFInd have been restored.\n"
