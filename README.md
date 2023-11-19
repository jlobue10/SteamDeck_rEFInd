# SteamDeck_rEFInd

Please feel free to donate and support me at the following link. Donations are not required, nor are they expected. I will continue to work on this repository and potential future variations, with or without donations.
<a href="https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=2PSSUQVX33L6N">
  <img src="https://raw.githubusercontent.com/stefan-niedermann/paypal-donate-button/master/paypal-donate-button.png" alt="Donate with PayPal" />
</a>

## SteamOS 3.5 seems to have broken quite a bit with the installer. I'm actively debugging and looking to repackage the whole process (probably into a PKGBUILD based delivery). Stay tuned...

This is a simple rEFInd install script for the Steam Deck meant to provide easy dual boot setup when using both SteamOS and Windows on the internal NVMe. Since the initial version of this script, optional support has been added for Windows from the SD card, Batocera from the SD card and an example boot stanza for Ubuntu (or other Ubuntu based flavors / distros). The options really are pretty limitless, but require some understanding and manual edits to the `refind.conf` file.

## [**The GUI is available and is the recommended installation method**](https://github.com/jlobue10/SteamDeck_rEFInd/tree/main/GUI)

If you want to try out the GUI, perform these steps.

```
git clone https://github.com/jlobue10/SteamDeck_rEFInd
cd SteamDeck_rEFInd
chmod +x install-GUI.sh
./install-GUI.sh
```

The GUI files will be created in the `/home/deck/.SteamDeck_rEFInd/GUI/` folder, including a desktop shortcut. Please give me feedback and enjoy!

## **Prerequisites and Setup (no GUI method)**

This installation script assumes that there are valid EFI boot entries for both Windows and SteamOS on the esp partition. For SteamOS there should be a valid EFI boot file located at `/esp/efi/steamos/steamcl.efi` . For Windows, there should be a valid EFI boot file located at `/esp/efi/Microsoft/Boot/bootmgfw.efi` . If you are missing either of these, or they do not function as intended, do not proceed with the installation script unless you know how to edit the boot entries in the refind.conf file to point to your correct EFI boot files for the OSes. You can confirm this by pressing Volume Up and Power buttons, then going to boot from file and selecting these manually. They should boot correctly into their respective OSes, otherwise do not proceed with the installation script (or proceed at your own risk).

It should be noted that if you follow the typical setup method for dual boot, those EFI files should be in the appropriate place and valid (accurate assumption in most cases).

The icons can be replaced with any 128x128 icons that a user desires. Please just make sure to update the refind.conf file for the correct icon names, if deviating from what's provided here.
The background can also be any 1,280 x 800 properly formatted picture. Same as with the icons, if you plan to use a different background, make sure you update the applicable line(s) in refind.conf for your background image. PNG format seems to work best for the backgrounds, with the fewest compatability issues.

**I recommend making any changes to the icons or background picture and refind.conf file _before running the installation script_.**

## **Installation instructions (no GUI method)**

If you've cloned my repository in the past and want to make sure that you have the latest updates, from the SteamDeck_rEFInd folder on a command line run `git status`. If you see that any files are missing or that any files need updates, you can either delete the directory and re-clone, or run `git reset --hard origin/main`. You can double check afterwards with another `git status`, which should say "up to date" if nothing is missing or changed.

From a SteamOS command line in desktop mode, run these commands one after the other.

```
git clone https://github.com/jlobue10/SteamDeck_rEFInd/
cd SteamDeck_rEFInd
chmod +x SteamDeck_rEFInd_install.sh
./SteamDeck_rEFInd_install.sh
```

Alternatively, if the `pacman` repositories experience an issue during the `pacman` based installation, you can run these last two lines instead, for a `pacman` free installation method. This method uses rEFInd downloaded from Sourceforge.
```
chmod +x refind_install_no_pacman.sh
./refind_install_no_pacman.sh
```

If all went well, you should have rEFInd setup with SteamOS as the default loading OS. Feel free to adjust the timeout from 5 seconds to whatever desired value in the refind.conf file. This is how long you will have to choose your OS before the default OS loads. A value of -1 for the timeout will automatically boot the default OS unless a button or trackpad is interacted with in the pre-boot sequence, after powering on. Select the desired OS using the right trackpad and the R2 (trigger) button, or with the D-Pad and A button.

For additional configuration options, please refer to the rEFInd official documentation. My supplied config file uses manual OS boot stanzas on purpose to control the icon order from left to right. This feature is something that I plan to take advantage of in the config file generation (and installation) GUI that I am developing. Please feel free to deviate from this and use rEFInd's ability to detect EFI files and OSes to boot, if you want. The config file has a lot of potential options that I encourage people to explore.

## **Disable Windows EFI entry or run the Windows' side script**

This honestly should be in a bold flashing neon light, as it is one of the most commonly missed issues. This step is still required for SteamOS 3.4+. Unfortunately, this still requires booting from the SteamOS recovery USB or another live Linux distro.

Steps:
```
efibootmgr
## Take note of the Windows EFI four digit number and replace the XXXX in the following command with that number.
sudo efibootmgr -b XXXX -A
```

## **Restoring _missing_ EFI entries**

This functionality has been automated with the `systemd` service, if the `systemd` service is enabled.

If the EFI entries for SteamOS and rEFInd have been deleted by a BIOS update, then just manually boot into SteamOS from the 'Boot from file' option in BIOS.

## **Note about systemd service**

***The GUI now has buttons to easily enable or disable the `systemd` service ('Sysd On' and 'Sysd Off').***

Due to the nature of SteamOS' partition structure (redundant rootfs-A and rootfs-B partitions), it may be necessary to occasionally double check whether the systemd service is still active and functioning properly. These redundant partitions are likely used for branch changes and/ or updates (or in case of failure of one or the other... not entirely sure). A useful command to check whether the systemd service is functioning properly is this.

`sudo systemctl status bootnext-refind.service`

If the status is anything other than active and enabled, it's possible that you may need to recopy the systemd service to `/etc/systemd/system/bootnext-refind.service` with sudo permissions. As this is a rare issue, I don't feel it's necessary to check for and automate this. If you had to recopy the systemd service onto the other redundant (now active root) partition, then you will also want to run this to start the service and enable it for future boots into SteamOS.

`sudo systemctl enable --now bootnext-refind.service`

## **Necessary steps for _reinstalling Windows_**

You will need to re-enable the Windows EFI boot entry to allow the Windows installation process to complete unhindered. Replace YYYY in the following command with the Windows EFI entry number to re-enable the Windows EFI boot entry. The script will disable it again later, if it is re-ran after successful installation. Please be aware that this disabling step requires SteamOS recovery image for SteamOS 3.4+ (at least for now).

`sudo efibootmgr -b YYYY -a`

## **Additional Windows considerations _(corrupted display on boot into Windows)_**

There is a newer, better fix than what was previously documented that actually prevents this issue in the first place. You can run a specific `bcdedit` command from either a command prompt or powershell (both require as administrator). This command should be run as soon as possible on a new Windows installation if a user plans to use rEFInd.

Command prompt command:

`bcdedit.exe -set {globalsettings} highestmode on`

Powershell command:

`bcdedit /set "{globalsettings}" highestmode on`

## **Optional Windows from Micro SD card instructions**

***This is automated in the GUI. Just make sure the Windows SD card is inserted for the 'Create Config' step and install the config afterwards. The manual steps below will still also work, but they are less convenient than the GUI method.***

The updated `refind.conf` file has a manual stanza now for a Micro SD card Windows boot option. Make sure to disable the other "Windows" boot option by adding a `disabled` line in that "Windows" stanza. We need to make 2 edits to the "Windows SD card" stanza to make the Micro SD card Windows boot properly from rEFInd. First, we need to find out the Micro SD card's EFI system partition UUID. I decided to use KDE Partition Manager to find out this information for my Micro SD card. See the following picture for the highlighted partition UUID.


![SD_Windows_Part_UUID](https://user-images.githubusercontent.com/9971433/204991179-dc98df86-71ff-4016-8253-ca74eac50d91.png)


Replace the `volume REPLACE_THIS_WITH_SD_CARD_EFI_PARTITION_UUID` line with your appropriate UUID. For my example, this line becomes `volume 2FB0D40F-C809-4C67-8B50-136D93B78543` . Then we also must delete the `disabled` line at the end of the stanza. The Micro SD card Windows rEFInd entry should now be active (after these 2 steps). In my brief test case, I found it necessary to press a key to avoid disk checking upon boot. I'm not sure if this is common for Windows from the SD card, as this is not my normal setup. It's just something to be aware of. If you miss pressing this interrupt key, the screen may look corrupted until the disk check completes and Windows continues to boot.

## **Disabling and/ or uninstalling rEFInd**

If you've tried rEFInd and decide you just don't want to use it any more, you can delete the rEFInd EFI entry with this command (replace XXXX with rEFInd EFI entry number).

`sudo efibootmgr -b XXXX -B`

To uninstall the package and files that came with the `pacman` installed rEFInd package run.

```
sudo steamos-readonly disable
# These next two commands may not be necessary, but they don't hurt anything either
sudo pacman-key --init
sudo pacman-key --populate archlinux
# The following command performs the pacman refind package removal
sudo pacman -R --noconfirm refind
sudo steamos-readonly enable
```

Once refind is uninstalled, the GUI can be removed with the following commands:
```
rm -rf ~/SteamDeck_rEFInd
rm -rf ~/.SteamDeck_rEFInd
rm -f ~/Desktop/refind_GUI.desktop
```

The non-pacman installation script files would be a little bit more complicated to delete, but not too difficult if somebody insists on steps for it. Since those files only take up a very small amount of space on the 5GB root partition (not taking up any space for games on the `/home` partition), I am not going to go over that in more detail here.

To remove the rEFInd directory from the `/esp` partition **_(be forewarned that making a mistake here and deleting the wrong files or folders on the `/esp` partition could render your Steam Deck unbootable and in need of the recovery image. Consider this a fair warning and me taking no responsibility for user error here.)_** run this command.

```
sudo rm -rf /esp/efi/refind/
```

## **[SteamOS reinstallation Considerations](https://github.com/jlobue10/SteamDeck_rEFInd/issues/49)**

## **References**

[rEFInd Boot Manager reference](https://www.rodsbooks.com/refind/ "rEFInd Boot Manager")

[efibootmgr reference](https://linux.die.net/man/8/efibootmgr "efibootmgr")

## **Future plans**

I have an unreleased repo meant for laptops and desktops with generic Linux and Windows rEFind dual boot support (with support for secure boot). I may release this at some point, but there's no ETA. This is mostly coded up [here](https://github.com/jlobue10/rEFInd_GUI) but it needs some attention for finishing touches. Useful pull and merge requests would be appreciated.

## **Acknowledgements**

Special thanks to **[DeckWizard](https://www.youtube.com/c/DeckWizard)** for extensive testing and feedback.

Special thanks to Reddit user **ChewyYui** for solving the annoying Windows graphical glitch and helping to figure out the SteamOS splash screen setting from the SteamOS manual boot stanza.

Credit to GitHub user **CryoByte33** (maker of [steam-deck-utilities](https://github.com/CryoByte33/steam-deck-utilities)) for zenity additions to my own code.

Also thank you to GitHub user **YoshiAye** for the updated background that I made default for the GUI installation.

## **Additional comments**

If you have an idea for code, script, or GUI improvement, please reach out to me. I am all for making this repository as good as possible. If you are going to use some aspect of my code for your own design, please give some credit or acknowledgment for the original code.
