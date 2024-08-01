#!/bin/bash
# An alternate,  without pacman, rEFInd installation (from Sourceforge)
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
	echo 20
	echo "# Downloading rEFInd zip file..."
	cd $HOME/Downloads
	wget https://sourceforge.net/projects/refind/files/0.14.2/refind-bin-gnuefi-0.14.2.zip
	echo 25
	echo "# Unzipping rEFInd zip..."
	unzip -o refind-bin-gnuefi-0.14.2.zip
	sudo steamos-readonly disable
	sudo mkdir -p /esp/efi/refind
	yes | sudo cp -f $HOME/Downloads/refind-bin-0.14.2/refind/refind_x64.efi /esp/efi/refind/
	yes | sudo cp -rf $HOME/Downloads/refind-bin-0.14.2/refind/drivers_x64/ /esp/efi/refind
	yes | sudo cp -rf $HOME/Downloads/refind-bin-0.14.2/refind/tools_x64/ /esp/efi/refind
	echo 50
	echo "# Installing rEFInd files..."
	yes | sudo ./refind-bin-0.14.2/refind-install
	yes | sudo cp -rf $HOME/Downloads/refind-bin-0.14.2/refind/icons/ /esp/efi/refind
	yes | sudo cp -rf $HOME/Downloads/refind-bin-0.14.2/fonts/ /esp/efi/refind
	yes | sudo cp $HOME/.local/SteamDeck_rEFInd/GUI/refind.conf /esp/efi/refind/refind.conf
	yes | sudo cp -rf $HOME/.local/SteamDeck_rEFInd/backgrounds/ /esp/efi/refind
	yes | sudo cp -rf $HOME/.local/SteamDeck_rEFInd/icons/ /esp/efi/refind
	efibootmgr | tee $HOME/efibootlist.txt
	echo 65
	echo "# Fixing EFI entries..."
	WINDOWS_BOOTNUM="$(grep -A0 'Windows' $HOME/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)"
	sudo efibootmgr -b $WINDOWS_BOOTNUM -A
	REFIND_BOOTNUM="$(grep -A0 'rEFInd Boot Manager' $HOME/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)"
	sudo efibootmgr -b $REFIND_BOOTNUM -B
	REFIND_BOOTNUM_ALT="$(grep -A0 'rEFInd' $HOME/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)"
	STEAMOS_BOOTNUM="$(grep -A0 'SteamOS' $HOME/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)"

	re='^[0-9]+$'
	if [[ $REFIND_BOOTNUM_ALT =~ $re ]]; then
		sudo efibootmgr -b $REFIND_BOOTNUM_ALT -B
	fi

	if ! [[ $STEAMOS_BOOTNUM =~ $re ]]; then
		sudo efibootmgr -c -d /dev/nvme0n1 -p 1 -L "SteamOS" -l \\EFI\\steamos\\steamcl.efi
	fi
	echo 80
	echo "# Finishing up..."
	sudo efibootmgr -c -d /dev/nvme0n1 -p 1 -L "rEFInd" -l \\EFI\\refind\\refind_x64.efi
	rm $HOME/efibootlist.txt
	sudo steamos-readonly enable
	echo 100
	echo "# Installation completed successfully."
) | zenity --title "Installing rEFInd from Sourceforge" --progress --no-cancel --width=500 2>/dev/null
