# **GUI README**

More details will be coming soon. The basics are that the GUI installer will create all of the necessary files in the

`/home/deck/.SteamDeck_rEFInd/GUI/` folder. 

The Line edits are read-only and you must use the combo box file dialogs (browse buttons) to change background and icon PNG files (at least for now).

The background should be a 1,280x800 PNG file. Please use a program like GIMP to size appropriately and 'export as PNG file'. The icons should be 128x128 PNGs.

Once the boot options, background and icons are chosen (can be left blank for default background and icons) click 'Create config.' You can manually check the config file if you want located at `/home/deck/.SteamDeck_rEFInd/GUI/refind.conf` . If you change the background or OS icons, please click 'Create Config' again, as it copies those files to the 'staging area' for when 'Install Config' is clicked and run (requires sudo password to install files to the `/esp` partition).

Feedback is welcome. I have tested this thoroughly enough to release it. Enjoy!

![rEFInd_GUI](https://user-images.githubusercontent.com/9971433/214604232-f97f9b91-9736-4cfb-95b2-cb2b78546760.png)

GUI configuration example (my default config for my personal Steam Deck)
