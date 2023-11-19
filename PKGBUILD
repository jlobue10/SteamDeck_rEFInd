# Maintainer: Jon LoBue <jlobue10@gmail.com>

_pkgname=SteamDeck_rEFInd
pkgname=${_pkgname}
pkgver=1.3.1
pkgrel=1
pkgdesc='SteamDeck rEFInd installer and customization GUI'
arch=('x86_64')
url="https://github.com/jlobue10/SteamDeck_rEFInd"
license=('MIT')
depends=()
makedepends=('cmake' 'gcc' 'glibc' 'make' 'qt5-base' 'qt5-tools')
source=(
    "SteamDeck_rEFInd::git+https://github.com/jlobue10/SteamDeck_rEFInd.git"
)
md5sums=(
    'SKIP'
)

prepare() {
    cd $_pkgname
    cd GUI/src
    mkdir -p build
}

build() {
    cd $_pkgname/GUI/src/build
    cmake ..
    make
}

package() {
	install -Dm755 "${srcdir}/$_pkgname/GUI/src/build/SteamDeck_rEFInd" "${pkgdir}/usr/bin/SteamDeck_rEFInd"
	install -Dm777 "${srcdir}/$_pkgname/SteamDeck_rEFInd.desktop" "${pkgdir}/usr/share/applications/SteamDeck_rEFInd.desktop"
	install -Dm644 "${srcdir}/$_pkgname/SteamDeck_rEFInd.png" "${pkgdir}/usr/share/icons/hicolor/256x256/apps/SteamDeck_rEFInd.png"
 	install -Dm777 "${srcdir}/$_pkgname/scripts/install_config_from_GUI.sh" "${pkgdir}/usr/bin/install_config_from_GUI.sh"
 	install -Dm777 "${srcdir}/$_pkgname/scripts/pacman_install.sh" "${pkgdir}/usr/bin/pacman_install.sh"
  	install -Dm777 "${srcdir}/$_pkgname/scripts/sourceforge_install.sh" "${pkgdir}/usr/bin/sourceforge_install.sh"
        install -Dm777 "${srcdir}/$_pkgname/scripts/rEFInd_bg_randomizer.sh" "${pkgdir}/usr/bin/rEFInd_bg_randomizer.sh"
        install -Dm777 "${srcdir}/$_pkgname/scripts/restore_EFI_entries.sh" "${pkgdir}/usr/bin/restore_EFI_entries.sh"
        install -Dm644 "${srcdir}/$_pkgname/systemd/rEFInd_bg_randomizer.service" "${pkgdir}/etc/systemd/system/rEFInd_bg_randomizer.service"
        install -Dm644 "${srcdir}/$_pkgname/systemd/bootnext-refind.service" "${pkgdir}/etc/systemd/system/bootnext-refind.service"
}

post_install() {
	systemctl daemon-reload
	# Start and enable the bootnext-refind service
	systemctl start bootnext-refind.service
	systemctl enable bootnext-refind.service
}
