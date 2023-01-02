# SteamDeck_rEFInd
This is a simple rEFInd install script for the Steam Deck meant to provide easy dual boot setup when using both SteamOS and Windows on the internal NVMe.

## **Script Updates and _improvements_ (Jan. 1, 2023) and outstanding issues**

A systemctl daemon to always prioritize rEFInd as the top boot priority has been added. This will continue to work in the future, unless a firmware update or SteamOS permissions issue blocks setting the 'Boot Next' setting with `efibootmgr`. This is one half of a solution that mostly disregards whether the Windows EFI entry can be disabled or not by the script. Disabling the Windows EFI entry, either with EasyUEFI from Windows or from command line with the SteamOS recovery image is still recommended, especially if you are setting up a triple boot (with Batocera on SD card for instance). The other half of this solution is to install the `bootsequencer-rEFInd-first.ps1` file (under the Windows directory here in this repository) as a task from Windows Task Scheduler. Save this `bootsequencer-rEFInd-first.ps1` file somewhere to be referenced and used by Task Scheduler. Special thanks to Reddit user lucidludic for this method and explanation. Open Task Scheduler, right-click on Task Scheduler Library and create a new folder named something like "rEFInd" then select the folder. Click "Create Basic Task", give it an appropriate name and description and click Next. Set the task to start "When I log on" and click Next, leave "Start a program" selected and click Next. In the Program/script text box enter the following (or use Browse):

`C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe`

In the "Add arguments (optional)" text box enter the following (replacing the path to point to your .ps1 script file):

`-executionpolicy bypass -file C:\PATH\TO\bootsequence-steamos.ps1`

Click Next, select the checkbox "Open the Properties dialog..." and click Finish. In the Properties window for the new task, in General enable "Run with highest privileges" and "Hidden", and set "Configure for:" to Windows 10. Switch to the Conditions tab and ensure that "Start the task only if the computer is on AC power" is disabled. Click OK to close the Properties window.

To check that it works, right-click the task and click Run. You may briefly see a PowerShell window appear, but this should not happen when it is normally scheduled. Open a Powershell Terminal using Run as Administrator and run this command again:

`bcdedit /enum FIRMWARE`

Under Firmware Boot Manager you should now see a new bootsequence value with your rEFInd GUID. Restart and rEFInd should automatically boot. Switch back to Windows, log in, and either repeat the above or simply restart to check that your automated task is working correctly.

With this 2 part workaround installed, switching between SteamOS branches has also become seamless and largely worry free (from the systemctl daemon). You can now switch freely between Stable, Beta, and Preview without the need to re-run the script. The one issue that remains and will likely not be solvable without re-running the script in the future is BIOS (UEFI firmware) updates. I confirmed this for my script earlier today by downgrading from 113 to 110, reinstalling rEFInd and then re-upgrading to BIOS 113. The 113 BIOS update completely deleted the SteamOS and rEFInd EFI entries and reactivated the Windows EFI entry. I do not know if there's a way around this going forward, other than changes from InsydeH2O and/ or Valve to the actual provided UEFI firmware (which comes with the BIOS update). If somebody else figures out a method that survives a BIOS update, then that is really good and should be applauded. For now, I've updated this script to be as easy as possible in the case of a BIOS update breaking something, but there are some additional preparation steps going forward to not interfere with future BIOS updates.

## **BIOS Update _preparation_**

To prepare for a BIOS update from the SteamOS side, whether alongside a major SteamOS version update (or manually) or some other source like the Preview branch, you will need to run the bios_update_SteamOS_prep.sh script or just manually copy and paste the three `sudo` commands from it into a SteamOS desktop command line. If using the script, make sure to give it executable permissions first. From inside the SteamDeck_rEFInd folder, run:

```
chmod +x bios_update_SteamOS_prep.sh
./bios_update_SteamOS_prep.sh
```

To prepare for a Windows side BIOS update, you will need to disable the task that you set up earlier, and remove the rEFInd priority Boot Sequence setting. Disabling the task from Task Scheduler is as simple as finding the task, right clicking and selecting 'disable'. To remove the Boot Sequence setting, just run the provided bios_install_prep.ps1 script from Powershell as Administrator. Download the DeckFlash_Win.7z from [here](https://content.pvabel.net/devices/SteamDeck/UEFI/Flasher/) provided by [Reddit user feherneoh](https://www.reddit.com/r/SteamDeck/comments/tyuu84/comment/i6dzjwf/?utm_source=share&utm_medium=web2x&context=3) and the latest Steam Deck BIOS file from [here](https://gitlab.com/evlaV/jupiter-hw-support/-/tree/master/usr/share/jupiter_bios). Place the .fd BIOS file in the same directory as the unzipped InsydeH2O utility folder and make sure it's the only one in there. Run H2OFFT-Wx64.exe and the .fd file from the same folder will be installed if you press OK in the utility. This can also work to downgrade a BIOS version, if necessary. After the reboot and BIOS update installation is successful, re-enable the scheduled task and re-run it one time (subsequent boots with it enabled won't require a manual run).

**_Please read_ regarding future BIOS updates**

UEFI firmware changes coming alongside some recent BIOS updates have caused some issues with the rEFInd script and installation method. In fact, even some people who were not using rEFInd as part of their dual boot method experienced some issues from the most recent BIOS update(s). The reason is that the firmware seems to be reactivating Windows EFI entries that were previously disabled, or simply creating new active Windows EFI entries (if its EFI file is found by firmware on the `/esp` partition). When this happens, Windows assumes the highest boot priority. There is no current way around this that I know of, but my most recent improvements to the script solve some of these headaches (or at least simplify the fix). Some users have even reported that the EFI entries for SteamOS and rEFInd have been deleted by the firmware as part of a BIOS update. My script now will detect if the SteamOS EFI entry has been deleted, and re-add it if necessary. The script had already been adding the rEFInd entry as part of its normal functionality (and deleting one potential duplicate rEFInd EFI entry, nice for subsequent script runs). If you find that after a BIOS update, Windows has assumed the top boot priority, shutdown the Steam Deck and power on with the Volume Up & Power button combo to get into the BIOS screen. Go to Boot From File (bottom left option) and boot from `/esp/efi/steamos/steamcl.efi`. Get back to the SteamOS desktop command line and re-run the script. It should now work fine, even if the script is unsuccessful in disabling the Windows EFI entry. Disabling the Windows EFI entry is still recommended though by one of the 2 aforementioned methods, either using a command line from the [SteamOS recovery image](https://help.steampowered.com/en/faqs/view/1b71-edf2-eb6d-2bb3), or EasyUEFI from Windows. If you're using the SteamOS recovery image command line method, run `efibootmgr` and take note of the Windows EFI entry boot number and replace XXXX in the following command with that number.

`sudo efibootmgr -b XXXX -A`

## **Prerequisites and Setup**

This installation script assumes that there are valid EFI boot entries for both Windows and SteamOS on the esp partition. For SteamOS there should be a valid EFI boot file located at /esp/efi/steamos/steamcl.efi . For Windows, there should be a valid EFI boot file located at /esp/efi/Microsoft/Boot/bootmgfw.efi . If you are missing either of these, or they do not function as intended, do not proceed with the installation script unless you know how to edit the boot entries in the refind.conf file to point to your correct EFI boot files for the OSes. You can confirm this by pressing Volume Up and Power buttons, then going to boot from file and selecting these manually. They should boot correctly into their respective OSes, otherwise do not proceed with the installation script (or proceed at your own risk).

It should be noted that if you follow the typical setup method for dual boot, those EFI files should be in the appropriate place and valid (accurate assumption in most cases).

The icons can be replaced with any 128x128 icons that a user desires. Please just make sure to update the refind.conf file for the correct icon names, if deviating from what's provided here.
The background can also be any 1,280 x 800 properly formatted picture. Same as with the icons, if you plan to use a different background, make sure you update the applicable line(s) in refind.conf for your background image. PNG format seems to work best for the backgrounds, with the fewest compatability issues.

**I recommend making any changes to the icons or background picture and refind.conf file _before running the installation script_.**

## **Basic Installation instructions** 

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

## **Necessary steps for _reinstalling Windows_**

If you plan on reinstalling Windows after running this script, you will need to disable the rEFInd EFI boot entry beforehand so that rEFInd does not interfere with the Windows installation process. You can do this from SteamOS desktop mode in a command line with two steps. As of the SteamOS 3.4 update, these commands may require booting from the SteamOS recovery image (to be successful).

`efibootmgr`

Take note and remember the boot entry for rEFInd and replace XXXX below with that number.

`sudo efibootmgr -b XXXX -A`

You will also need to re-enable the Windows EFI boot entry to allow the Windows installation process to complete unhindered. Replace YYYY in the following command with the Windows EFI entry number to re-enable the Windows EFI boot entry. The script will disable it again later, if it is re-ran after successful installation. Please be aware that this disabling step requires SteamOS recovery image for SteamOS 3.4+ (at least for now).

`sudo efibootmgr -b YYYY -a`

## **Additional Windows considerations _(corrupted display on boot into Windows)_**

If you encounter an issue while booting up where the Windows display is corrupted to the point that it's basically unusable, there is a workaround to fix the issue. Boot into SteamOS and edit the `refind.conf` file using the commands `sudo steamos-readonly disable` then `sudo nano /esp/efi/refind/refind.conf` from a command line. Make sure all `resolution` lines are commented out in the `refind.conf` config file (line begins with a `#`). When this is done, press `Ctrl+x` followed by `y` to save and exit. If this is successful, then on next reboot, the rEFInd screen will be rotated in portrait mode. Boot into Windows, fix any resolution discrepancies (should be 1,280 x 800 for main Steam Deck display) and save those changes. You should now be able to go back into SteamOS, edit the `refind.conf` config file again and make sure that `resolution 3` is uncommented (`#` at line beginning deleted) for use. Once you've confirmed normal operation again, please use the `sudo steamos-readonly enable` command from a SteamOS command line.

## **Optional Windows from Micro SD card instructions**

The updated `refind.conf` file has a manual stanza now for a Micro SD card Windows boot option. Make sure to disable the other "Windows" boot option by adding a `disabled` line in that "Windows" stanza. We need to make 2 edits to the "Windows SD card" stanza to make the Micro SD card Windows boot properly from rEFInd. First, we need to find out the Micro SD card's EFI system partition UUID. I decided to use KDE Partition Manager to find out this information for my Micro SD card. See the following picture for the highlighted partition UUID.


![SD_Windows_Part_UUID](https://user-images.githubusercontent.com/9971433/204991179-dc98df86-71ff-4016-8253-ca74eac50d91.png)


Replace the `volume REPLACE_THIS_WITH_SD_CARD_EFI_PARTITION_UUID` line with your appropriate UUID. For my example, this line becomes `volume 2FB0D40F-C809-4C67-8B50-136D93B78543` . Then we also must delete the `disabled` line at the end of the stanza. The Micro SD card Windows rEFInd entry should now be active (after these 2 steps). In my brief test case, I found it necessary to press a key to avoid disk checking upon boot. I'm not sure if this is common for Windows from the SD card, as this is not my normal setup. It's just something to be aware of. If you miss pressing this interrupt key, the screen may look corrupted until the disk check completes and Windows continues to boot.

## **References**

[rEFInd Boot Manager reference](https://www.rodsbooks.com/refind/ "rEFInd Boot Manager")

[efibootmgr reference](https://linux.die.net/man/8/efibootmgr "efibootmgr")

## **Future plans**

I have started working on a small GUI to make customization of rEFInd for a given user even easier. I realize that not everyone is comfortable with command line and config file editing. This GUI will allow selecting a new background, different icons per OS, custom boot order and priority, timeout value and whether or not the mouse is enabled for the rEFInd screen. Any feedback on desired features for this GUI is welcome. Thanks for using my script.
