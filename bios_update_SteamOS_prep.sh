#!/bin/bash
# A simple script to prep for SteamOS initiated BIOS updates

sudo systemctl stop bootnext-refind.service
sudo systemctl disable bootnext-refind.service
sudo efibootmgr -N

echo -e "\nSteamOS is ready for BIOS updates to occur."
