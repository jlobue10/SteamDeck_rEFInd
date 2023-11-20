## **GUI README**
Please feel free to donate and support me at the following link. Donations are not required, nor are they expected. I will continue to work on this repository and potential future variations, with or without donations.
<a href="https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=2PSSUQVX33L6N">
  <img src="https://raw.githubusercontent.com/stefan-niedermann/paypal-donate-button/master/paypal-donate-button.png" alt="Donate with PayPal" />
</a>

# **Default Installation**

To install the GUI, make sure that you've already set your `sudo` password and ensure that you are connected to the internet, then perform these steps.

```
cd $HOME && rm -rf $HOME/SteamDeck_rEFInd/ && git clone https://github.com/jlobue10/SteamDeck_rEFInd && cd SteamDeck_rEFInd && chmod +x install-GUI.sh && ./install-GUI.sh
```

The GUI setup installs all of the necessary files to the `/home/deck/.local/SteamDeck_rEFInd/GUI/` folder. Inside this folder, you will also find the `background.png, os_icon1.png, os_icon2.png, os_icon3.png, and os_icon4.png` files. These 5 files will also be copied to the `/esp` partition when the 'Install Config' button is pressed (in addition to the `refind.conf` file).

If this is your first time installing rEFInd on your Steam Deck, you will want to press the 'Install rEFInd' button. I recommend leaving Sourceforge selected from the drop down for this installation, as Sourceforge is the preferred installation source. A `pacman` installation source script is also provided, in case there or if someone just prefers to try to use `pacman` for the rEFInd installation portion. If you already have a functional rEFInd setup, you can safely skip this step (in most instances).

The background should be a 1,280x800 PNG file. Please use a program like GIMP to size appropriately and export as a PNG file. The icons should be 128x128 PNGs (recommended anyways... not a strict requirement). Other resolutions may also be unofficially supported, but I do not recommend it, and results may vary. The images are restricted to PNG format because those tend to play nicely with rEFInd, whereas other formats tend to be hit and miss.

The four boot options (3 and 4 are optional) represent the icons as they will appear on the bootloader screen from left to right.

Once the boot options, background and OS icons are chosen (can be left blank for default background and icons) click 'Create Config.' You can manually check (and edit) the config file if you want located at `/home/deck/.SteamDeck_rEFInd/GUI/refind.conf` . If you change the background or OS icons, please click the 'Create Config' button again, as it copies those files to the 'staging area' for when 'Install Config' is clicked and run (requires `sudo` password to install files to the `/esp` partition).

The 'Use Firmware_bootnum' option is a SteamOS only option that requires the SteamOS EFI entry to be present when the config file is created. This can be checked with `efibootmgr`. This option was added so that the SteamOS icon is visible between the handoff of rEFInd to SteamOS loading (otherwise it's a blank screen).

The Linux distro selection drop down box can be ignored if you're not selecting Linux as one of your boot options. This drop down offers some popular distro choices and helps the code create an appropriate boot Stanza for that choice. Some manual edits may still be required depending on which Linux distro and which particular configuration is used.

Two new buttons were added to either enable or disable the `systemd` service (Sysd On and Sysd Off). These buttons will pop-up an xterm, ask for the `sudo` password and then perform their respective tasks, displaying the `systemd` service status as the last step. The xterm can be safely closed after completion.

`Rand BG On` and `Rand BG Off` background randomization service has been added. This will randomly choose one background from the `/home/deck/.local/SteamDeck_rEFInd/backgrounds/` folder when SteamOS boots and then overwrite the `background.png` file on the `/esp` partition. This can easily be turned on or off with the 2 buttons. If you turn off and want to go back to a set background, please make sure to redo the create config and install config steps as necessary. Please feel free to delete or add additional PNG backgrounds to that folder that the randomizer chooses from.

Ventoy was added as a boot option. This selection will work for either Micro SD card or USB, although not both concurrently, as it will boot whichever one it finds first with the `VTOYEFI` partition label.

Feedback is welcome. I have tested this thoroughly enough to release it. Enjoy!

![refind_GUI_2023](https://user-images.githubusercontent.com/9971433/222976497-ca58d762-669c-4a4d-9300-0557e95f0b67.png)

GUI configuration example (my default config for my personal Steam Deck)

# **Optional**

If you want a quick way to make changes to your rEFInd configuration from SteamOS' Game mode, I'd recommend setting up the Plasma Nested Session and adding its shortcut to Steam. Please see this [website](https://gist.github.com/davidedmundson/8e1732b2c8b539fd3e6ab41a65bcab74) for details. Once you've launched the nested Plasma session, open the rEFInd GUI, make changes, install those config changes and then make sure to click the 'Return to Gaming Mode' shortcut from the desktop to properly leave the nested Plasma session. I've tested this, and it works well.

# **Links and Recognition**

Please review this [video tutorial by Deck Wizard](https://www.youtube.com/watch?v=ubWPIf2DbvE) to see if it answers any questions that you may have before posting an issue.

Also, thanks again to the [original rEFInd developer](https://www.rodsbooks.com/refind/), who this GUI customization software would not be possible without. My script and GUI just make rEFInd installation and customization easy for the Steam Deck; rEFInd itself performs the complicated bootloader tasks.
