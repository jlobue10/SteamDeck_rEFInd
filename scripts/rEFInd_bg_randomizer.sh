#!/bin/bash
# Invoked only by systemd/rEFInd_bg_randomizer.service as root, so $HOME is not
# reliably the deck user's home here -- keep this path hardcoded to match the
# service's own hardcoded assumption (see systemd/rEFInd_bg_randomizer.service).
RAND_BG=$(ls /home/deck/.local/SteamDeck_rEFInd/backgrounds | grep .png | shuf -n1)
sudo cp /home/deck/.local/SteamDeck_rEFInd/backgrounds/$RAND_BG /esp/efi/refind/background.png
