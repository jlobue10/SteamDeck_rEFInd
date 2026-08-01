# Star Wars Theme for rEFInd Bootloader

A high-resolution theme for the **[rEFInd Boot Manager](https://rodsbooks.com/refind/)**, where you can choose between your Light side and your Dark side (Perfect for Dual booted systems).

![Preview of the Star Wars Anakin rEFInd theme, showing boot options over a detailed space background.](preview.jpeg)

## 💻 Comprehensive Setup Guide

This section provides instructions for both installing rEFInd and applying this theme.

### Part 1: Installing the rEFInd Boot Manager (If needed)

Follow these steps on a Linux system (like Ubuntu) to install rEFInd.

1.  **Update and Install rEFInd:**
    ```bash
    sudo apt install refind
    ```
2.  **Run the Installation Script:**
    This command copies the necessary files to your EFI System Partition and creates the NVRAM boot entry.
    ```bash
    sudo refind-install
    ```
3.  **Verify and Reboot:**
    Check your boot order to ensure rEFInd is the default. Then, reboot your system.

### Part 2: Applying the Star Wars Theme

After rEFInd is installed, you can apply the theme by moving the files into the correct location and updating the configuration.

#### 1. Locate the rEFInd EFI Directory

Identify the location of your rEFInd installation. This is typically mounted at:

* **Linux:** `/boot/efi/EFI/refind/`

We will refer to this path as `$REFIND_DIR`.

#### 2. Clone the Repository

Navigate to the `themes` directory inside your rEFInd installation and clone this repository.

## Sources

* **Background Image (Anakin Skywalker):** The high-resolution background image used for the theme.
    * **Link:** [Anakin Skywalker Background Image](https://images.hdqwalls.com/download/anakin-skywalker-3r-2560x1024.jpg)
* **Icons:** The base icons used for boot entries (Windows, Ubuntu, etc.) are sourced from the community.
    * **Link:** [SLywnow rEFInd Icon Themes](https://github.com/SLywnow/hi-themes-refind)


