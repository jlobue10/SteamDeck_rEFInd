#!/bin/bash
RAND_BG=$(ls /home/deck/.local/SteamDeck_rEFInd/backgrounds | grep .png | shuf -n1)
sudo cp /home/deck/.local/SteamDeck_rEFInd/backgrounds/$RAND_BG /esp/efi/refind/background.png
