#!/bin/bash
# A simple Steam Deck rEFInd automated install script using Pacman
# Please make sure that a password exists for the deck user before running
(
	echo 0
	echo "# Installation started: Password prompt..."
	PASSWD="$(zenity --password --title="Enter sudo password" 2>/dev/null)"
	echo "$PASSWD" | sudo -v -S
	ANS=$?
	if [[ $ANS == 1 ]]; then
		zenity --error --title="Password Error" --text="`printf "Incorrect password provided.\nPlease try again providing the correct sudo password."`" --width=400 2>/dev/null
		echo 100
		echo "# Installation Failed. Please try again with correct sudo password"
		exit 1
	fi
	sudo steamos-readonly disable
	echo 20
	echo "# Initializing Pacman repositories..."
	sudo pacman-key --init
	sudo pacman-key --populate archlinux
	echo 25
	echo "# Installing rEFInd package..."
	yes | sudo pacman -S refind
	sudo refind-install
	efibootmgr | tee /home/deck/efibootlist.txt
	WINDOWS_BOOTNUM="$(grep -A0 'Windows' /home/deck/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)"
	sudo efibootmgr -b $WINDOWS_BOOTNUM -A
	REFIND_BOOTNUM="$(grep -A0 'rEFInd Boot Manager' /home/deck/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)"
	sudo efibootmgr -b $REFIND_BOOTNUM -B
	echo 50
	echo "# Fixing EFI entries..."
	REFIND_BOOTNUM_ALT="$(grep -A0 'rEFInd' /home/deck/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)"
	STEAMOS_BOOTNUM="$(grep -A0 'SteamOS' /home/deck/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)"
	re='^[0-9]+$'
	if [[ $REFIND_BOOTNUM_ALT =~ $re ]]; then
		sudo efibootmgr -b $REFIND_BOOTNUM_ALT -B
	fi

	if ! [[ $STEAMOS_BOOTNUM =~ $re ]]; then
		sudo efibootmgr -c -d /dev/nvme0n1 -p 1 -L "SteamOS" -l \\EFI\\steamos\\steamcl.efi
	fi
	echo 75
	echo "# Installing files to /efi partition..."
	sudo cp -rf /boot/efi/EFI/refind/ /esp/efi
	sudo mv /esp/efi/refind/refind.conf /esp/efi/refind/refind-bkp.conf
	sudo cp /home/deck/.SteamDeck_rEFInd/refind.conf /esp/efi/refind/refind.conf
	sudo cp -rf /home/deck/.SteamDeck_rEFInd/backgrounds/ /esp/efi/refind
	sudo cp -rf /home/deck/.SteamDeck_rEFInd/icons/ /esp/efi/refind
	sudo efibootmgr -c -d /dev/nvme0n1 -p 1 -L "rEFInd" -l \\EFI\\refind\\refind_x64.efi
	sudo cp /home/deck/.SteamDeck_rEFInd/bootnext-refind.service /etc/systemd/system/bootnext-refind.service
	sudo systemctl enable --now bootnext-refind.service
	sudo efibootmgr -n $(efibootmgr | grep rEFInd | grep -Eo "[0-9]{1,4}" | head -1)
	rm /home/deck/efibootlist.txt
	sudo steamos-readonly enable
	echo 100
	echo "# Installation completed succesfully."
) | zenity --title "Installing rEFInd with Pacman" --progress --no-cancel --width=500 2>/dev/null
