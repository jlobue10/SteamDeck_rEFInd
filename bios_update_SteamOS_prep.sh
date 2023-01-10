#!/bin/bash
# A simple script to prep for SteamOS initiated BIOS updates

sudo systemctl disable --now bootnext-refind.service
sudo efibootmgr -N

echo -e "\nSteamOS is ready for BIOS updates to occur."
