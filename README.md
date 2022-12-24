# SteamDeck_rEFInd
This is a simple rEFInd install script for the Steam Deck meant to provide easy dual boot setup when using both SteamOS and Windows on the internal NVMe.

**_Please read_ regarding latest SteamOS 3.4 stable branch**

The latest stable release, SteamOS 3.4 has a firmware issue (for some users) that force reinstalls a Windows EFI entry and places it as the top boot priority. If you had rEFInd working properly before the 3.4 update (12/21/2022) and you are experiencing this issue, the simplest method to restore rEFInd is to boot from the [Steam Deck recovery image](https://help.steampowered.com/en/faqs/view/1b71-edf2-eb6d-2bb3), re-clone and re-run my installation script. This should work for most people. What you are looking for primarily is that the asterisk is gone next to the Windows EFI entry after running the script (which denotes an inactive entry). Alternatively, you could also just run `efibootmgr` take note of the Windows EFI entry boot number and replace XXXX in the following command with that number.

`sudo efibootmgr -b XXXX -A`

If you are setting up dual boot for the first time on a Steam Deck with version 3.4+, after installing Windows and its APU driver, the most straightforward method at this time is to boot into SteamOS desktop mode, run the script as instructed below and then either re-run the script afterwards while booted from the SteamOS recovery image, or simply manually disable the Windows EFI entry (or entries if multiple) as outlined above. I've considered making a new recovery image specific version of the script (necessary since mount points are different requiring different or additional commands), but for now I've held off on that. I'd hope moreso that Valve can undo this firmware change (in a future release) that is blocking the script from fully working without the additional step (in recovery mode or from [Windows with EasyUEFI trial](https://github.com/jlobue10/SteamDeck_rEFInd/issues/12#issuecomment-1362533289)).

Also, if after the 3.4 update your SteamOS EFI entry is missing, you can re-add it from a SteamOS desktop command line with the following `efibootmgr` command. You can use boot from file (/esp/efi/steamos/steamcl.efi) in the BIOS menu (Volume Up + Power) to get into SteamOS manually.

`sudo efibootmgr -c -d /dev/nvme0n1 -p 1 -L "SteamOS" -l \\efi\\steamos\\steamcl.efi`

**Prerequisites and Setup**

This installation script assumes that there are valid EFI boot entries for both Windows and SteamOS on the esp partition. For SteamOS there should be a valid EFI boot file located at /esp/efi/steamos/steamcl.efi . For Windows, there should be a valid EFI boot file located at /esp/efi/Microsoft/Boot/bootmgfw.efi . If you are missing either of these, or they do not function as intended, do not proceed with the installation script unless you know how to edit the boot entries in the refind.conf file to point to your correct EFI boot files for the OSes. You can confirm this by pressing Volume Up and Power buttons, then going to boot from file and selecting these manually. They should boot correctly into their respective OSes, otherwise do not proceed with the installation script (or proceed at your own risk).

The icons can be replaced with any 128x128 icons that a user desires. Please just make sure to update the refind.conf file for the correct icon names, if deviating from what's provided here.
The background can also be any 1,280 x 800 properly formatted picture. Same as with the icons, if you plan to use a different background, make sure you update the applicable line in refind.conf for your background image. PNG format seems to work best for the backgrounds, with the fewest compatability issues.

**I recommend making any changes to the icons or background picture and refind.conf file _before running the installation script_.**

**Basic Installation instructions** 

From a SteamOS command line in desktop mode, run these commands one after the other.

```
git clone https://github.com/jlobue10/SteamDeck_rEFInd/
cd SteamDeck_rEFInd
chmod +x SteamDeck_rEFInd_install.sh
./SteamDeck_rEFInd_install.sh
```

Alternatively, if the `pacman` repositories experience an issue, you can run these last two lines instead, for a `pacman` free installation method.
```
chmod +x refind_install_no_pacman.sh
./refind_install_no_pacman.sh
```

If all went well, you should have rEFInd setup with SteamOS as the default loading OS. Feel free to adjust the timeout from 5 seconds to whatever desired value in the refind.conf file. This is how long you will have to choose your OS before the default OS loads. A value of -1 for the timeout will automatically boot the default OS unless a button or trackpad is interacted with in the pre-boot sequence, after powering on. Select the desired OS using the right trackpad and the R2 (trigger) button, or with the D-Pad and A button.

**Extra information and considerations _(Reinstalling Windows)_**

If you plan on reinstalling Windows after running this script, you will need to disable the rEFInd EFI boot entry beforehand so that rEFInd does not interfere with the Windows installation process. You can do this from SteamOS desktop mode in a command line with two steps.

`efibootmgr`

Take note and remember the boot entry for rEFInd and replace XXXX below with that number.

`sudo efibootmgr -b XXXX -A`

One more step will be required to allow the Windows installation process to complete unhindered. Replace YYYY in the following command with the Windows EFI entry number to re-enable the Windows EFI boot entry. The script will disable it again later, if it is re-ran after successful installation (this step requires SteamOS recovery image for SteamOS 3.4).

`sudo efibootmgr -b YYYY -a`

**Additional Windows considerations _(corrupted display on boot into Windows)_**

If you encounter an issue while booting up where the Windows display is corrupted to the point that it's basically unusable, there is a workaround to fix the issue. Boot into SteamOS and edit the `refind.conf` file using the commands `sudo steamos-readonly disable` then `sudo nano /esp/efi/refind/refind.conf` from a command line. Make sure all `resolution` lines are commented out in the `refind.conf` config file (line begins with a `#`). When this is done, press `Ctrl+x` followed by `y` to save and exit. If this is successful, then on next reboot, the rEFInd screen will be rotated in portrait mode. Boot into Windows, fix any resolution discrepancies (should be 1,280 x 800 for main Steam Deck display) and save those changes. You should now be able to go back into SteamOS, edit the `refind.conf` config file again and make sure that `resolution 3` is uncommented (`#` at line beginning deleted) for use. Once you've confirmed normal operation again, please use the `sudo steamos-readonly enable` command from a SteamOS command line.

**Optional Windows from Micro SD card instructions**

The updated `refind.conf` file has a manual stanza now for a Micro SD card Windows boot option. Make sure to disable the other "Windows" boot option by adding a `disabled` line in that "Windows" stanza. We need to make 2 edits to the "Windows SD card" stanza to make the Micro SD card Windows boot properly from rEFInd. First, we need to find out the Micro SD card's EFI system partition UUID. I decided to use KDE Partition Manager to find out this information for my Micro SD card. See the following picture for the highlighted partition UUID.


![SD_Windows_Part_UUID](https://user-images.githubusercontent.com/9971433/204991179-dc98df86-71ff-4016-8253-ca74eac50d91.png)


Replace the `volume REPLACE_THIS_WITH_SD_CARD_EFI_PARTITION_UUID` line with your appropriate UUID. For my example, this line becomes `volume 2FB0D40F-C809-4C67-8B50-136D93B78543` . Then we also must delete the `disabled` line at the end of the stanza. The Micro SD card Windows rEFInd entry should now be active (after these 2 steps). In my brief test case, I found it necessary to press a key to avoid disk checking upon boot. I'm not sure if this is common for Windows from the SD card, as this is not my normal setup. It's just something to be aware of. If you miss pressing this interrupt key, the screen may look corrupted until the disk check completes and Windows continues to boot.

**References**

[rEFInd Boot Manager reference](https://www.rodsbooks.com/refind/ "rEFInd Boot Manager")

[efibootmgr reference](https://linux.die.net/man/8/efibootmgr "efibootmgr")

**Further README formatting updates will come in a future release.**
