# **GUI README**

To install the GUI perform these steps.

```
git clone https://github.com/jlobue10/SteamDeck_rEFInd/
cd SteamDeck_rEFInd
chmod +x install-GUI.sh
./install-GUI.sh
```
This GUI installation will take care of installing all of the necessary dependendcies to successfully compile the GUI from source, then compile the GUI and ask whether or not to place a shortcut on the desktop. If the GUI compile fails for some reason, you will get a pop-up notifying you of that error. Something in your installation did not finish correctly if you get this error pop-up. Common errors include a dependency failing to install, or your `pacman` repositories from your config file are pointing to something other than `*-rel`.

The dependencies that are installed by `pacman` are `base-devel glibc linux-api-headers qt5-base`. Additionally, the `hwinfo` package is installed because it is required by the code for the autodetecting of the SD card or USB Windows installation options (`hwinfo` obtains this partition UUID information without sudo permissions).

A successful GUI installation will look similar to this from your Konsole output.

![GUI_compile_success](https://user-images.githubusercontent.com/9971433/217302271-5192bae7-3fea-4ee1-86f0-12bb9e91797b.png)

The GUI setup installs all of the necessary files to the `/home/deck/.SteamDeck_rEFInd/GUI/` folder. Inside this folder, you will also find the `background.png, os_icon1.png, os_icon2.png, os_icon3.png, and os_icon4.png` files. These 5 files will also be copied to the `/esp` partition when the 'Install Config' button is pressed (in addition to the `refind.conf` file).

If this is your first time installing rEFInd on your Steam Deck, you will want to press the 'Install rEFInd' button. I recommend leaving `Pacman` selected from the drop down for this installation. If you already have a functional rEFInd setup, you can safely skip this step (in most instances).

The Line edits are read-only and you must use the combo box file dialogs (browse buttons) to change background and icon PNG files (at least for now). This simplifies the code, as it requires minimal to no error checking when selecting the PNG files for the background or OS icons.

The background should be a 1,280x800 PNG file. Please use a program like GIMP to size appropriately and export as a PNG file. The icons should be 128x128 PNGs. Other resolutions may also be supported unofficially, but I do not recommend it, and results may vary. The images are restricted to PNG format because those tend to play nicely with rEFInd, whereas other formats tend to be hit and miss.

The four boot options (3 and 4 are optional) represent the icons as they will appear on the bootloader screen from left to right.

Once the boot options, background and icons are chosen (can be left blank for default background and icons) click 'Create Config.' You can manually check (and edit) the config file if you want located at `/home/deck/.SteamDeck_rEFInd/GUI/refind.conf` . If you change the background or OS icons, please click 'Create Config' button again, as it copies those files to the 'staging area' for when 'Install Config' is clicked and run (requires sudo password to install files to the `/esp` partition).

The 'Use Firmware_bootnum' option is a SteamOS only option that requires the SteamOS EFI entry to be present when the config file is created. This can be checked with `efibootmgr`. This option was added so that the SteamOS icon is visible between the handoff of rEFInd to SteamOS loading (otherwise it's a blank screen).

Feedback is welcome. I have tested this thoroughly enough to release it. Enjoy!

![rEFInd_GUI](https://user-images.githubusercontent.com/9971433/214604232-f97f9b91-9736-4cfb-95b2-cb2b78546760.png)

GUI configuration example (my default config for my personal Steam Deck)

Please review this [video tutorial by Deck Wizard](https://www.youtube.com/watch?v=zEpcBWX9K_o) to see if it answers any questions that you may have before posting an issue.

Also, thanks again to the [original rEFInd developer](https://www.rodsbooks.com/refind/), who this GUI customization software would not be possible without. My script and GUI just make rEFInd installation customization easy for the Steam Deck, rEFInd itself performs the complicated bootloader tasks.
