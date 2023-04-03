# SteamDeck_rEFInd
This is a simple rEFInd install script for the Steam Deck meant to provide easy dual boot setup when using both SteamOS and Windows on the internal NVMe. Since the initial version of this script, optional support has been added for Windows from the SD card, Batocera from the SD card and an example boot stanza for Ubuntu (or other Ubuntu based flavors / distros). The options really are pretty limitless, but require some understanding and manual edits to the `refind.conf` file.

## [**The GUI is available and is the recommended installation method**](https://github.com/jlobue10/SteamDeck_rEFInd/tree/main/GUI)

If you want to try out the GUI, perform these steps.

```
git clone https://github.com/jlobue10/SteamDeck_rEFInd/
cd SteamDeck_rEFInd
chmod +x install-GUI.sh
./install-GUI.sh
```
The GUI files will be created in the `/home/deck/.SteamDeck_rEFInd/GUI/` folder, including a desktop shortcut. Please give me feedback and enjoy!

## **Script Updates and _improvements_**

A systemctl daemon to always prioritize rEFInd as the top boot priority has been added. This will continue to work in the future, unless a firmware update or SteamOS permissions issue blocks setting the 'Boot Next' setting with `efibootmgr`. This is one half of a solution that mostly disregards whether the Windows EFI entry can be disabled or not by the script. Disabling the Windows EFI entry, either with EasyUEFI from Windows or from command line with the SteamOS recovery image is still recommended, especially if you are setting up a triple boot (with Batocera on SD card for instance). The other half of this solution is to install the `bootsequence-rEFInd-first.ps1` file (under the Windows directory here in this repository) as a task from Windows Task Scheduler. Save this `bootsequence-rEFInd-first.ps1` file somewhere to be referenced and used by Task Scheduler. Special thanks to [Reddit user lucidludic for this method and explanation](https://www.reddit.com/r/steamdeck_linux/comments/zb3l7k/comment/iyrxnzs/?utm_source=share&utm_medium=web2x&context=3). Open Task Scheduler, right-click on Task Scheduler Library and create a new folder named something like "rEFInd" then select the folder. Click "Create Basic Task", give it an appropriate name and description and click Next. Set the task to start "When I log on" and click Next, leave "Start a program" selected and click Next. In the Program/script text box enter the following (or use Browse):

`C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe`

In the "Add arguments (optional)" text box enter the following (replacing the path to point to your .ps1 script file):

`-executionpolicy bypass -file C:\PATH\TO\bootsequence-rEFInd-first.ps1`

Click Next, select the checkbox "Open the Properties dialog..." and click Finish. In the Properties window for the new task, in General enable "Run with highest privileges" and "Hidden", and set "Configure for:" to Windows 10. Enable the "Run whether user is logged on or not," and ensure that "Do not store password," is checked.  Switch to the Conditions tab and ensure that "Start the task only if the computer is on AC power" is disabled. Click OK to close the Properties window.

To check that it works, right-click the task and click Run. You may briefly see a PowerShell window appear, but this should not happen when it is normally scheduled. Open a Powershell Terminal using Run as Administrator and run this command again:

`bcdedit /enum FIRMWARE`

Under the Firmware Boot Manager ({fwbootmgr}) entry you should now see a new bootsequence value with your rEFInd GUID. Restart and rEFInd should automatically boot. Switch back to Windows, log in, and either repeat the above or simply restart to check that your automated task is working correctly.

With this 2 part workaround installed, switching between SteamOS branches has also become seamless and largely worry free (from the systemctl daemon). You can now switch freely between Stable, Beta, and Preview without the need to re-run the script. The one issue that remains and will likely not be solvable without re-running the script in the future is BIOS (UEFI firmware) updates. I confirmed this for my script earlier today by downgrading from 113 to 110, reinstalling rEFInd and then re-upgrading to BIOS 113. The 113 BIOS update completely deleted the SteamOS and rEFInd EFI entries and reactivated the Windows EFI entry. I do not know if there's a way around this going forward, other than changes from InsydeH2O and/ or Valve to the actual provided UEFI firmware (which comes with the BIOS update). If somebody else figures out a method that survives a BIOS update, then that is really good and should be applauded. For now, I've updated this script to be as easy as possible in the case of a BIOS update breaking something, but there are some additional preparation steps going forward in order to not interfere with future BIOS updates.

## **BIOS Update _preparation_**

***This is easily accomplished now from the GUI by clicking the Sysd Off button and entering the `sudo` password in the popped up xterm.***

To prepare for a BIOS update from the SteamOS side, whether alongside a major SteamOS version update (or manually) or some other source like the Preview branch, you will need to run the `bios_update_SteamOS_prep.sh` script or just manually copy and paste the three `sudo` commands from it into a SteamOS desktop command line. If using the script, make sure to give it executable permissions first. From inside the `SteamDeck_rEFInd` folder, run:

```
chmod +x bios_update_SteamOS_prep.sh
./bios_update_SteamOS_prep.sh
```

To prepare for a Windows side BIOS update, you will need to disable the task that you set up earlier, and remove the rEFInd priority Boot Sequence setting. Disabling the task from Task Scheduler is as simple as finding the task, right clicking and selecting 'disable'. To remove the Boot Sequence setting, just run the provided `bios_install_prep.ps1` script from Powershell as Administrator. Download the [DeckFlash_Win.7z from here](https://content.pvabel.net/devices/SteamDeck/UEFI/Flasher/) provided by [Reddit user feherneoh](https://www.reddit.com/r/SteamDeck/comments/tyuu84/comment/i6dzjwf/?utm_source=share&utm_medium=web2x&context=3) and the latest Steam Deck BIOS file from [here](https://gitlab.com/evlaV/jupiter-hw-support/-/tree/master/usr/share/jupiter_bios). Place the `.fd` BIOS file in the same directory as the unzipped InsydeH2O utility folder, and make sure it's the only BIOS update file in there. Run `H2OFFT-Wx64.exe` and the `.fd` file from the same folder will be installed once you press OK. This can also work to downgrade a BIOS version, if necessary. After the reboot and BIOS update installation is successful, re-enable the scheduled task and manually re-run the task one time (subsequent boots with it enabled won't require a manual run).

**_Please read_ regarding future BIOS updates**

UEFI firmware changes coming alongside some recent BIOS updates have caused some issues with the rEFInd script and installation method. In fact, even some people who were not using rEFInd as part of their dual boot method experienced some issues from the most recent BIOS update(s). The reason is that the firmware seems to be reactivating Windows EFI entries that were previously disabled, or simply creating new active Windows EFI entries (if its EFI file is found by firmware on the `/esp` partition). When this happens, Windows assumes the highest boot priority. There is no current way around this that I know of, but my most recent improvements to the script solve some of these headaches (or at least simplify the fix). Some users have even reported that the EFI entries for SteamOS and rEFInd have been deleted by the firmware as part of a BIOS update. My script now will detect if the SteamOS EFI entry has been deleted, and re-add it if necessary. The script had already been adding the rEFInd entry as part of its normal functionality (and deleting one potential duplicate rEFInd EFI entry, nice for consecutive script runs). If you find that after a BIOS update, Windows has assumed the top boot priority, shutdown the Steam Deck and power on with the Volume Up & Power button combo to get to the BIOS screen. Go to Boot From File (bottom left option) and boot from `/esp/efi/steamos/steamcl.efi`. Get back to the SteamOS desktop command line and re-run the script. It should now work fine, even if the script is unsuccessful in disabling the Windows EFI entry. Disabling the Windows EFI entry is still recommended though, by one of the 2 aforementioned methods, either using a command line from the [SteamOS recovery image](https://help.steampowered.com/en/faqs/view/1b71-edf2-eb6d-2bb3), or EasyUEFI from Windows. If you're using the SteamOS recovery image command line method, run `efibootmgr` and take note of the Windows EFI entry boot number and replace XXXX in the following command with that number.

`sudo efibootmgr -b XXXX -A`

## **Prerequisites and Setup**

This installation script assumes that there are valid EFI boot entries for both Windows and SteamOS on the esp partition. For SteamOS there should be a valid EFI boot file located at `/esp/efi/steamos/steamcl.efi` . For Windows, there should be a valid EFI boot file located at `/esp/efi/Microsoft/Boot/bootmgfw.efi` . If you are missing either of these, or they do not function as intended, do not proceed with the installation script unless you know how to edit the boot entries in the refind.conf file to point to your correct EFI boot files for the OSes. You can confirm this by pressing Volume Up and Power buttons, then going to boot from file and selecting these manually. They should boot correctly into their respective OSes, otherwise do not proceed with the installation script (or proceed at your own risk).

It should be noted that if you follow the typical setup method for dual boot, those EFI files should be in the appropriate place and valid (accurate assumption in most cases).

The icons can be replaced with any 128x128 icons that a user desires. Please just make sure to update the refind.conf file for the correct icon names, if deviating from what's provided here.
The background can also be any 1,280 x 800 properly formatted picture. Same as with the icons, if you plan to use a different background, make sure you update the applicable line(s) in refind.conf for your background image. PNG format seems to work best for the backgrounds, with the fewest compatability issues.

**I recommend making any changes to the icons or background picture and refind.conf file _before running the installation script_.**

## **Installation instructions**

If you've cloned my repository in the past and want to make sure that you have the latest updates, from the SteamDeck_rEFInd folder on a command line run `git status`. If you see that any files are missing or that any files need updates, you can either delete the directory and re-clone, or run `git reset --hard origin/main`. You can double check afterwards with another `git status`, which should say "up to date" if nothing is missing or changed.

From a SteamOS command line in desktop mode, run these commands one after the other.

```
git clone https://github.com/jlobue10/SteamDeck_rEFInd/
cd SteamDeck_rEFInd
chmod +x SteamDeck_rEFInd_install.sh
./SteamDeck_rEFInd_install.sh
```

Alternatively, if the `pacman` repositories experience an issue during the `pacman` based installation, you can run these last two lines instead, for a `pacman` free installation method.
```
chmod +x refind_install_no_pacman.sh
./refind_install_no_pacman.sh
```

If all went well, you should have rEFInd setup with SteamOS as the default loading OS. Feel free to adjust the timeout from 5 seconds to whatever desired value in the refind.conf file. This is how long you will have to choose your OS before the default OS loads. A value of -1 for the timeout will automatically boot the default OS unless a button or trackpad is interacted with in the pre-boot sequence, after powering on. Select the desired OS using the right trackpad and the R2 (trigger) button, or with the D-Pad and A button.

For additional configuration options, please refer to the rEFInd official documentation. My supplied config file uses manual OS boot stanzas on purpose to control the icon order from left to right. This feature is something that I plan to take advantage of in the config file generation (and installation) GUI that I am developing. Please feel free to deviate from this and use rEFInd's ability to detect EFI files and OSes to boot, if you want. The config file has a lot of potential options that I encourage people to explore.

## **Restoring _missing_ EFI entries**

In case either the SteamOS or rEFInd EFI entries are deleted (for instance by a BIOS update), you can just run the provided `restore_EFI_entries.sh` script. This script will detect if either EFI entry is missing and only re-add missing entries (no duplicates created).

This functionality has been automated with the `systemd` service, if the `systemd` service is enabled.

## **Note about systemd service**

***The GUI now has buttons to easily enable or disable the `systemd` service ('Sysd On' and 'Sysd Off').***

Due to the nature of SteamOS' partition structure (redundant rootfs-A and rootfs-B partitions), it may be necessary to occasionally double check whether the systemd service is still active and functioning properly. These redundant partitions are likely used for branch changes and/ or updates (or in case of failure of one or the other... not entirely sure). A useful command to check whether the systemd service is functioning properly is this.

`sudo systemctl status bootnext-refind.service`

If the status is anything other than active and enabled, it's possible that you may need to recopy the systemd service to `/etc/systemd/system/bootnext-refind.service` with sudo permissions. As this is a rare issue, I don't feel it's necessary to check for and automate this. If you had to recopy the systemd service onto the other redundant (now active root) partition, then you will also want to run this to start the service and enable it for future boots into SteamOS.

`sudo systemctl enable --now bootnext-refind.service`

## **Necessary steps for _reinstalling Windows_**

If you plan on reinstalling Windows after running this script, you will need to disable the rEFInd EFI boot entry beforehand so that rEFInd does not interfere with the Windows installation process. You can do this from SteamOS desktop mode in a command line with two steps. As of the SteamOS 3.4 update, these commands may require booting from the SteamOS recovery image (to be successful).

`efibootmgr`

Take note and remember the boot entry for rEFInd and replace XXXX below with that number.

`sudo efibootmgr -b XXXX -A`

You will also need to re-enable the Windows EFI boot entry to allow the Windows installation process to complete unhindered. Replace YYYY in the following command with the Windows EFI entry number to re-enable the Windows EFI boot entry. The script will disable it again later, if it is re-ran after successful installation. Please be aware that this disabling step requires SteamOS recovery image for SteamOS 3.4+ (at least for now).

`sudo efibootmgr -b YYYY -a`

## **Additional Windows considerations _(corrupted display on boot into Windows)_**

There is a newer, better fix than what was previously documented that actually prevents this issue in the first place. You can run a specific `bcdedit` command from either a command prompt or powershell (both require as administrator). This command should be run as soon as possible on a new Windows installation if a user plans to use rEFInd.

Command prompt command:

`bcdedit.exe -set {globalsettings} highestmode on`

Powershell command:

`bcdedit /set "{globalsettings}" highestmode on`

(**_Old "fix"_**) If you encounter an issue while booting up where the Windows display is corrupted to the point that it's basically unusable (not the same as the dotted vertical lines scrolling), there is a workaround to fix the issue. Boot into SteamOS and edit the `refind.conf` file using the command `sudo nano /esp/efi/refind/refind.conf` from a command line. Make sure all `resolution` lines are commented out in the `refind.conf` config file (line begins with a `#`). When this is done, press `Ctrl+x` followed by `y` then `Enter` to save and exit. If this is successful, then on next reboot, the rEFInd screen will be rotated in portrait mode. Boot into Windows, fix any resolution discrepancies (should be 1,280 x 800 for main Steam Deck display) and save those changes. You should now be able to go back into SteamOS, edit the `refind.conf` config file again and make sure that `resolution 3` is uncommented (`#` at line beginning deleted) for normal use.

## **Optional Windows from Micro SD card instructions**

***This is automated in the GUI. Just make sure the Windows SD card is inserted for the 'Create Config' step and install the config afterwards. The manual steps below will still also work, but they are less convenient than the GUI method.***

The updated `refind.conf` file has a manual stanza now for a Micro SD card Windows boot option. Make sure to disable the other "Windows" boot option by adding a `disabled` line in that "Windows" stanza. We need to make 2 edits to the "Windows SD card" stanza to make the Micro SD card Windows boot properly from rEFInd. First, we need to find out the Micro SD card's EFI system partition UUID. I decided to use KDE Partition Manager to find out this information for my Micro SD card. See the following picture for the highlighted partition UUID.


![SD_Windows_Part_UUID](https://user-images.githubusercontent.com/9971433/204991179-dc98df86-71ff-4016-8253-ca74eac50d91.png)


Replace the `volume REPLACE_THIS_WITH_SD_CARD_EFI_PARTITION_UUID` line with your appropriate UUID. For my example, this line becomes `volume 2FB0D40F-C809-4C67-8B50-136D93B78543` . Then we also must delete the `disabled` line at the end of the stanza. The Micro SD card Windows rEFInd entry should now be active (after these 2 steps). In my brief test case, I found it necessary to press a key to avoid disk checking upon boot. I'm not sure if this is common for Windows from the SD card, as this is not my normal setup. It's just something to be aware of. If you miss pressing this interrupt key, the screen may look corrupted until the disk check completes and Windows continues to boot.

## **Disabling and/ or uninstalling rEFInd**

If you've tried rEFInd and decide you just don't want to use it any more, you can disable the rEFInd EFI entry with this command (replace XXXX with rEFInd EFI entry number).

`sudo efibootmgr -b XXXX -A`

If you want to delete the rEFInd EFI entry run this following command.

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

The non-pacman installation script files would be a little bit more complicated to delete, but not too difficult if somebody insisted on it. Since those files only take up a very small amount of space on the 5GB root partition (not taking up any space for games on the `/home` partition), I am not going to go over that in more detail here.

To remove the rEFInd directory from the `/esp` partition **_(be forewarned that making a mistake here and deleting the wrong files or folders on the `/esp` partition could render your Steam Deck unbootable and in need of the recovery image. Consider this a fair warning and me taking no responsibility for user error here.)_** run this command.

```
sudo rm -rf /esp/efi/refind/
```

## **[SteamOS reinstallation Considerations](https://github.com/jlobue10/SteamDeck_rEFInd/issues/49)**

## **References**

[rEFInd Boot Manager reference](https://www.rodsbooks.com/refind/ "rEFInd Boot Manager")

[efibootmgr reference](https://linux.die.net/man/8/efibootmgr "efibootmgr")

## **Future plans**

The GUI has been thoroughly tested and released. I'm working on adding a check for update function to the GUI. As this is just a minor QoL improvement and not any functional "improvement," the priority is not that high, and there's no specific ETA for when this will be working.

There's still an elusive failure to compile error or other `libc` dependency issue that affects a small number of users. I suspect that on those Steam Decks, there's a an actual SteamOS installation issue that a reinstall of SteamOS will fix. I understand that not everyone would want to perform the reinstallation though, so I will work on figuring out how to successfully compile a version of the GUI with static dependencies. This static version will end up being a larger binary executable and will not be the recommended version unless users are affected by the compile and failure to launch errors of the normal (dynamic library) version. This is a work in progress and will likely be initially released with version 1.1.9.

I also have an unreleased repo meant for laptops and desktops with generic Linux and Windows rEFind dual boot support (with support for secure boot). I may release this at some point, but there's no ETA.

## **Acknowledgements**

Special thanks to **[DeckWizard](https://www.youtube.com/c/DeckWizard)** for extensive testing and feedback.

Special thanks to Reddit user **ChewyYui** for solving the annoying Windows graphical glitch and helping to figure out the SteamOS splash screen setting from the SteamOS manual boot stanza.

Credit to GitHub user **CryoByte33** (maker of [steam-deck-utilities](https://github.com/CryoByte33/steam-deck-utilities)) for zenity additions to my own code. I inspected his working examples to incorporate into some aspects of the GUI installation pop-ups.

Also thank you to GitHub user **YoshiAye** for the updated background that I made default for the GUI installation.

## **Additional comments**

If you have an idea for code, script, or GUI improvement, please reach out to me. I am all for making this repository as good as possible. If you are going to use some aspect of my code for your own design, please give some credit or acknowledgment for the original code.
