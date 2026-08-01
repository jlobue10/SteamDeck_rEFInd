# Glassy
## A minimalistic rEFInd theme

![Glassy](preview.png)

### Usage

 1. Locate your refind EFI directory. This is commonly `/boot/EFI/refind`
    though it will depend on where you mount your ESP and where rEFInd is
    installed.

 2. Create a folder called `themes` inside it, if it doesn't already exist

 3. Clone this repository into the `themes` directory.

 4. To enable the theme add `include themes/rEFInd-glassy/theme.conf` at the end of
    `refind.conf`.
    
 5. You may need to change the default resolution of the rEFInd menu (see refind.conf).
    
 6. The background can easily be replaced by any .png-file with a resolution of 1920x1080.

Entries should be autodetected and shown with the proper icons.

Manual entries can be done via `menuentry` option (see refind.conf for examples).

### Attributions
> This theme is based on the [Sunset](https://gitlab.com/realmain/rEFInd-sunset) theme.

> Contains OS Icons from [munlik's](https://github.com/munlik/refind-theme-regular) repository.
