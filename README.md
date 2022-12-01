# SteamDeck_rEFInd
This is a simple rEFInd install script for the Steam Deck meant to provide easy dual boot setup when using both SteamOS and Windows on the internal NVMe.

This simple install script assumes that the SteamOS and Windows boot entries are valid and have not been renamed (folders have correct names as well).
If you've followed a guide telling you to rename any EFI files or folders, please revert those changes before running this script.
The icons can be replaced with any 128x128 icons that a user desires. Please just make sure to update the refind.conf file for the correct icon names, if deviating from what's provided here.
The background can also be any 1,280 x 800 properly formatted picture. Same as with the icons, if you plan to use a different background, make sure you update the applicable line in refind.conf for your background image.
I recommend making any changes to the icons or background picture and refind.conf file before running the installation script.

**Prerequisites**:
This installation script assumes that there are valid EFI boot entries for both Windows and SteamOS on the esp partition. For SteamOS there should be a valid EFI boot file located at /esp/efi/steamos/steamcl.efi . For Windows, there should be a valid EFI boot file located at /esp/efi/Microsoft/Boot/bootmgfw.efi . If you are missing either of these, or they do not function as intended, do not proceed with the installation script unless you know how to edit the boot entries in the refind.conf file to point to your correct EFI boot files for the OSes. You can confirm this by pressing Volume Up and Power buttons, then going to boot from file and selecting these manually. They should boot correctly into their respective OSes, otherwise do not proceed with the installation script (or proceed at your own risk).

Assuming you have the 2 valid SteamOS and Windows EFI boot files, continue and run the following steps for installation.

**Basic Installation instructions** (assuming from a SteamOS command line in desktop mode). Run these commands one after the other.

`git clone https://github.com/jlobue10/SteamDeck_rEFInd/`

`cd SteamDeck_rEFInd`

`chmod +x SteamDeck_rEFInd_install.sh`

`./SteamDeck_rEFInd_install.sh`

If all went well, you should have rEFInd setup with SteamOS as the default loading OS. Feel free to adjust the timeout from 5 seconds to whatever desired value in the refind.conf file. This is how long you will have to choose your OS before the default OS loads. Select the desired OS using the right trackpad and the R2 (trigger) button.

**Extra information and considerations**
If you plan on reinstalling Windows after running this script, you will need to disable the rEFInd EFI boot entry beforehand so that rEFInd does not interfere with the Windows installation process. You can do this from SteamOS desktop mode in a command line with two steps.

`efibootmgr`

Take note and remember the boot entry for rEFInd and replace XXXX below with that number.

`sudo efibootmgr -b XXXX -A`

One more step will be required to allow the Windows installation process to complete unhindered. Replace YYYY in the following command with the Windows EFI entry number to re-enable the Windows EFI boot entry. The script will disable it again later, if it is re-ran after successful installation.

`sudo efibootmgr -b YYYY -a`

**SteamOS branch considerations**

When using SteamOS branches other than the `Stable` branch, it's possible to run into some issues preventing a successful run of the script and installation of rEFInd. My recommendation for setting up any Steam Deck would be to always have Windows and SteamOS (`Stable` branch) both setup and working before the initial run of the script. If you try to change to any SteamOS branch other than `Stable` before the first run of the rEFInd installation script, the step where the Windows EFI boot entry is disabled may not complete successfully and therefore rEFInd will not be able to work. For this reason, run the script from the SteamOS `Stable` branch first before switching away from the `Stable` branch. In addition, a new issue popped up with SteamOS 3.4 where you may have to manually boot SteamOS from its EFI boot file, in order to run the rEFInd installation script one more time to restore rEFInd.

**Additional Windows considerations (corrupted display on boot into Windows)**

If you encounter an issue while booting up where the Windows display is corrupted to the point that it's basically unusable, there is a workaround to fix the issue. Boot into SteamOS and edit the `refind.conf` file using the commands `sudo steamos-readonly disable` then `sudo nano /esp/efi/refind/refind.conf` from a command line. Make sure all `resolution` lines are commented out in the `refind.conf` config file (line begins with a `#`). When this is done, press `Ctrl+x` followed by `y` to save and exit. If this is successful, then on next reboot, the rEFInd screen will be rotated in portrait mode. Boot into Windows, fix any resolution discrepancies (should be 1,280 x 800 for main Steam Deck display) and save those changes. You should now be able to go back into SteamOS, edit the `refind.conf` config file again and make sure that `resolution 3` is uncommented (`#` at line beginning deleted) for use. Once you've confirmed normal operation again, please use the `sudo steamos-readonly enable` command from a SteamOS command line.

**Optional Windows from Micro SD card instructions**

The updated `refind.conf` file has a manual stanza now for a Micro SD card Windows boot option. Make sure to disable the other "Windows" boot option by adding a `disabled` line in that "Windows" stanza. We need to make 2 edits to the "Windows SD card" stanza to make the Micro SD card Windows boot properly from rEFInd. First, we need to find out the Micro SD card's EFI system partition UUID. I decided to use KDE Partition Manager to find out this information for my Micro SD card. See the following picture for the highlighted partition UUID.


![SD_Windows_Part_UUID](https://user-images.githubusercontent.com/9971433/204991179-dc98df86-71ff-4016-8253-ca74eac50d91.png)


Replace the `volume REPLACE_THIS_TEXT_WITH_SD_CARD_EFI_PARTITION_UUID` line with your appropriate UUID. For my example, this line becomes `volume 2FB0D40F-C809-4C67-8B50-136D93B78543` . Then we also must delete the `disabled` line at the end of the stanza. The Micro SD card Windows rEFInd entry should now be active (after these 2 steps). In my brief test case, I found it necessary to press a key to avoid disk checking upon boot. I'm not sure if this is common for Windows from the SD card, as this is not my normal setup. It's just something to be aware of. If you miss pressing this interrupt key, the screen may look corrupted until the disk check completes and Windows continues to boot.

**References**

[rEFInd Boot Manager reference](https://www.rodsbooks.com/refind/ "rEFInd Boot Manager")

[efibootmgr reference](https://linux.die.net/man/8/efibootmgr "efibootmgr")
