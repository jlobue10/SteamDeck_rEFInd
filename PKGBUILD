# Maintainer: Jon LoBue <jlobue10@gmail.com>

_pkgname=SteamDeck_rEFInd
pkgname=${_pkgname}-git
pkgver=1.3.0
pkgrel=1
pkgdesc='SteamDeck rEFInd installer and customization GUI'
arch=('x86_64')
url="https://github.com/jlobue10/SteamDeck_rEFInd"
license=('MIT')
depends=()
makedepends=('cmake' 'gcc' 'glibc' 'lib32-glibc' 'qt5-base')
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
}
