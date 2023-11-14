#!/bin/bash
cp /home/deck/.SteamDeck_rEFInd/GUI/{refind.conf,background.png,os_icon1.png,os_icon2.png,os_icon3.png,os_icon4.png} /esp/efi/refind/ 2>/dev/null

ANS=$?
if [[ $ANS == 0 ]]; then
    zenity --info --title="Success" --text="`printf "The refind.conf config file, OS icons and background image\nwere successfully moved to the refind folder on the /esp partition."`" --width=500 2>/dev/null
else
    zenity --error --title="Password Error" --text="`printf "Incorrect password provided, or some files were not found for installation.\nPlease try again providing the correct sudo password,\nand ensuring that the refind.conf config file, 4 OS icons, and background image\nexist in the /home/deck/.SteamDeck_rEFInd/GUI/ directory."`" --width=600 2>/dev/null
    exit 1
fi
