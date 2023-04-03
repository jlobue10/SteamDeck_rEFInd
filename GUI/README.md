## **GUI README**

## **Alternative installation method**

Starting with release version `1.1.6`, I am pre-packaging a tarball called `rEFInd_GUI.tar.gz`. If you don't want `pacman` to make any system changes during the GUI installation script, you can download the latest release version `rEFInd_GUI.tar.gz` tarball instead and extract the entire `.SteamDeck_rEFInd` folder to `/home/deck`. Feel free to copy the `rEFInd_GUI.desktop` shortcut to the desktop if desired. This tarball comes with the precompiled rEFInd_GUI binary since on some systems the compile may fail. I have not been able to replicate this or explain why, but I think providing the precompiled GUI binary is a good compromise and may be what some people want anyways.

If you use the precompiled binary and want to use the Windows (SD) or Windows (USB) options as one of your boot methods, then you may need to perform a few additional steps to install the `hwinfo` package. This `hwinfo` program is used by the GUI to obtain the relevant partition GUID information for the SD card and/ or USB hard drive without the need for `sudo` permissions. If you need to manually install `hwinfo`, then perform these steps from a SteamOS command line.

```
sudo steamos-readonly disable
sudo pacman-key --init
sudo pacman-key --populate archlinux
sudo pacman -Sy --noconfirm hwinfo
sudo steamos-readonly enable
```

# **Default Installation**

To install the GUI, make sure that you've already set your `sudo` password and ensure that you are connected to the internet, then perform these steps.

```
git clone https://github.com/jlobue10/SteamDeck_rEFInd/
cd SteamDeck_rEFInd
chmod +x install-GUI.sh
./install-GUI.sh
```
This GUI installation will take care of installing all of the necessary dependencies to successfully compile the GUI from source, then compile the GUI and ask whether or not to place a shortcut on the desktop. If the GUI compile fails for some reason, you will get a pop-up notifying you of that error. Something in your installation did not finish correctly if you get this error pop-up. Common errors include a dependency failing to install, or your `pacman` repositories from your config file are pointing to something other than `*-rel`. This installation method has been extensively tested on the Stable branch of SteamOS. Many installation errors can be solved by just re-running the `install-GUI.sh` script. For more in-depth troubleshooting, if it's required, feel free to post an issue.

The dependencies that are installed by `pacman` are `base-devel glibc linux-api-headers qt5-base`. Additionally, the `hwinfo` package is installed because it is required by the code for the autodetecting of the SD card and/ or USB Windows installation options (`hwinfo` obtains this partition UUID information without `sudo` permissions).

A successful GUI installation will look similar to this from your Konsole output.

![GUI_compile_success](https://user-images.githubusercontent.com/9971433/217302271-5192bae7-3fea-4ee1-86f0-12bb9e91797b.png)

The GUI setup installs all of the necessary files to the `/home/deck/.SteamDeck_rEFInd/GUI/` folder. Inside this folder, you will also find the `background.png, os_icon1.png, os_icon2.png, os_icon3.png, and os_icon4.png` files. These 5 files will also be copied to the `/esp` partition when the 'Install Config' button is pressed (in addition to the `refind.conf` file).

If this is your first time installing rEFInd on your Steam Deck, you will want to press the 'Install rEFInd' button. I recommend leaving `Pacman` selected from the drop down for this installation, as `pacman` is the preferred installation source. A Sourceforge installation source script is also provided, in case there is a `pacman` repository issue, or if someone just prefers to not use `pacman` for the rEFInd installation portion. If you already have a functional rEFInd setup, you can safely skip this step (in most instances).

The Line edit text boxes are read-only and you must use the combo box file dialogs (browse buttons) to change background and icon PNG files (at least for now). This simplifies the code, as it requires minimal to no error checking when selecting the PNG files for the background or OS icons.

The background should be a 1,280x800 PNG file. Please use a program like GIMP to size appropriately and export as a PNG file. The icons should be 128x128 PNGs. Other resolutions may also be unofficially supported, but I do not recommend it, and results may vary. The images are restricted to PNG format because those tend to play nicely with rEFInd, whereas other formats tend to be hit and miss.

The four boot options (3 and 4 are optional) represent the icons as they will appear on the bootloader screen from left to right.

Once the boot options, background and OS icons are chosen (can be left blank for default background and icons) click 'Create Config.' You can manually check (and edit) the config file if you want located at `/home/deck/.SteamDeck_rEFInd/GUI/refind.conf` . If you change the background or OS icons, please click the 'Create Config' button again, as it copies those files to the 'staging area' for when 'Install Config' is clicked and run (requires `sudo` password to install files to the `/esp` partition).

The 'Use Firmware_bootnum' option is a SteamOS only option that requires the SteamOS EFI entry to be present when the config file is created. This can be checked with `efibootmgr`. This option was added so that the SteamOS icon is visible between the handoff of rEFInd to SteamOS loading (otherwise it's a blank screen).

The Linux distro selection drop down box can be ignored if you're not selecting Linux as one of your boot options. This drop down offers some popular distro choices and helps the code create an appropriate boot Stanza for that choice. Some manual edits may still be required depending on which Linux distro and which particular configuration is used.

Two new buttons were added to either enable or disable the `systemd` service (Sysd On and Sysd Off). These buttons will pop-up an xterm, ask for the `sudo` password and then perform their respective tasks, displaying the `systemd` service status as the last step. The xterm can be safely closed after completion.

Ventoy was added as a boot option. This selection will work for either Micro SD card or USB, although not both concurrently, as it will boot whichever one it finds first with the `VTOYEFI` partition label.

Feedback is welcome. I have tested this thoroughly enough to release it. Enjoy!

![refind_GUI_2023](https://user-images.githubusercontent.com/9971433/222976497-ca58d762-669c-4a4d-9300-0557e95f0b67.png)

GUI configuration example (my default config for my personal Steam Deck)

# **Installation issues**

One common installation issue revolves around an error with your Steam Deck's `pacman` repositories. I did not personally encounter this error in any of my testing, but here is a screenshot provided to me by a user who experienced a `pacman` error with the GUI installation. In this case, the `pacman` for this particular Steam Deck was trying to access the beta repositories and resulted in an error (as seen below).

There is a fairly simple fix for this if anyone else is experiencing this. Basically fix your `pacman` config file. You can do this by opening up a Konsole command line and performing:

```
sudo steamos-readonly disable
sudo nano /etc/pacman.conf
```

The `/etc/pacman.conf` file in the Steam Deck from the example screenshot will have these following entries.

```
[jupiter-beta]
[holo-beta]
[core-beta]
[extra-beta]
[community-beta]
[multilib-beta]
```

These lines should be changed from beta to release like this:

```
[jupiter-rel]
[holo-rel]
[core-rel]
[extra-rel]
[community-rel]
[multilib-rel]
```

then press `Ctrl+x` followed by `y` and then `Enter` to save your changes and exit. Retry the GUI installer after this change and it **_SHOULD_** be successful, if this was your only issue.

## **Compile fail issue (example in below screenshot)**

If for whatever reason you still cannot get the GUI to compile due to a C library dependency issue, you will want to grab and use the `rEFInd_GUI_standalone` tarball from the release page and follow the same instructions as above for the "Alternative installation method" (using this standalone version). What's different with the standalone version is that the necessary C static libraries are compiled into the GUI, so they do not rely on the same C language system shared libraries. This ends up in a larger executable file. This has already been proven to work by at least one user.

![rEFInd_compile_error](https://user-images.githubusercontent.com/9971433/229573606-ddd0ab43-b472-4b47-91be-dd208baf1a5c.png)

If you see this error or something similar, please try the standalone version and feel free to leave me any relevant feedback.

# **Optional**

If you want a quick way to make changes to your rEFInd configuration from SteamOS' Game mode, I'd recommend setting up the Plasma Nested Session and adding its shortcut to Steam. Please see this [website](https://gist.github.com/davidedmundson/8e1732b2c8b539fd3e6ab41a65bcab74) for details. Once you've launched the nested Plasma session, open the rEFInd GUI, make changes, install those config changes and then make sure to click the 'Return to Gaming Mode' shortcut from the desktop to properly leave the nested Plasma session. I've tested this, and it works well.

# **Links and Recognition**

Please review this [video tutorial by Deck Wizard](https://www.youtube.com/watch?v=zEpcBWX9K_o) to see if it answers any questions that you may have before posting an issue.

Also, thanks again to the [original rEFInd developer](https://www.rodsbooks.com/refind/), who this GUI customization software would not be possible without. My script and GUI just make rEFInd installation and customization easy for the Steam Deck; rEFInd itself performs the complicated bootloader tasks.
