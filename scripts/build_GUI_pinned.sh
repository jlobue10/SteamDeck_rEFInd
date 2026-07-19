#!/bin/bash

# Build the GUI against a toolchain matching SteamOS, using podman.
#
# SteamOS has no compiler (cmake/gcc/make are all absent, and the rootfs is
# immutable), and building against current rolling Arch produces a binary that
# aborts at load on every Deck:
#   libQt6Core.so.6: version `Qt_6.11' not found
# because Qt symbol version nodes are not backward compatible. So the build
# happens in a container pinned to an Arch Linux Archive snapshot whose
# qt6-base/glibc match SteamOS. Keep these in sync with the same-named values
# in .github/workflows/arch-release.yml.
#
# Usage: scripts/build_GUI_pinned.sh [output-dir]     (default: ./build-pinned)

set -euo pipefail

ARCH_SNAPSHOT='2025/07/30'   # qt6-base 6.9.1-5, glibc 2.41 — SteamOS 3.8.16
BOOTSTRAP_DATE='2025.07.01'  # self-consistent root from the same era
IMAGE='arch-pinned:steamos'

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$(realpath -m "${1:-$REPO_ROOT/build-pinned}")"

if ! command -v podman > /dev/null; then
    echo "Error: podman is required (preinstalled on SteamOS). Aborting." >&2
    exit 1
fi

mkdir -p "$OUT"
WORK="$(mktemp -d)"
trap 'podman unshare rm -rf "$WORK" 2>/dev/null; rm -rf "$WORK"' EXIT

if ! podman image exists "$IMAGE"; then
    echo -e "Fetching pinned Arch root (${BOOTSTRAP_DATE})...\n"
    curl -fsSL -o "$WORK/bootstrap.tar.zst" \
        "https://archive.archlinux.org/iso/${BOOTSTRAP_DATE}/archlinux-bootstrap-${BOOTSTRAP_DATE}-x86_64.tar.zst"
    # Extract and repack inside a user namespace: the tarball carries
    # root-owned files and restrictive directory modes that a plain
    # unprivileged tar cannot write.
    mkdir -p "$WORK/rootfs"
    podman unshare bash -euo pipefail -c "
        tar --use-compress-program=unzstd -xf '$WORK/bootstrap.tar.zst' -C '$WORK/rootfs'
        tar -C '$WORK/rootfs/root.x86_64' -cf '$WORK/rootfs.tar' .
    "
    podman import "$WORK/rootfs.tar" "$IMAGE"
fi

echo -e "Building GUI...\n"
# Signature checking is off because the archive is served over HTTPS and keys
# valid at snapshot time are marked disabled in today's keyring.
podman run --rm -v "$REPO_ROOT:/src:ro" -v "$OUT:/out" "$IMAGE" \
    bash -euo pipefail -c "
        A='https://archive.archlinux.org/repos/${ARCH_SNAPSHOT}'
        printf '[options]\nArchitecture = auto\nSigLevel = Never\n[core]\nServer = %s/\$repo/os/\$arch\n[extra]\nServer = %s/\$repo/os/\$arch\n' \"\$A\" \"\$A\" > /etc/pacman.conf
        pacman -Syu --noconfirm > /dev/null
        pacman -S --noconfirm --needed cmake gcc make qt6-base qt6-tools > /dev/null
        pacman -Q glibc qt6-base gcc
        cp -r /src/GUI /build-gui
        cd /build-gui/src && mkdir -p build && cd build
        cmake .. -DCMAKE_BUILD_TYPE=Release > /dev/null
        make -j\$(nproc)
        install -Dm755 SteamDeck_rEFInd /out/SteamDeck_rEFInd
    "

# The regression this script exists to prevent: a binary that cannot start on
# a Deck. SteamOS 3.8.x tops out at Qt_6.9. Read the real .gnu.version_r
# requirements rather than scraping strings, and fail closed — a Qt Widgets
# binary always carries a Qt_6.x version node, so finding none means the check
# did not work rather than that the binary is clean.
if ! command -v readelf > /dev/null; then
    echo "Error: readelf (binutils) is required for the ABI check. Aborting." >&2
    exit 1
fi
NEED="$(readelf -V "$OUT/SteamDeck_rEFInd" | grep -oE 'Qt_6\.[0-9]+' | sort -uV | tail -1 || true)"
if [ -z "$NEED" ]; then
    echo "Error: no Qt version node found in $OUT/SteamDeck_rEFInd; ABI check did not run." >&2
    exit 1
fi
echo -e "\nBuilt $OUT/SteamDeck_rEFInd (requires $NEED)"
if [ "$(printf '%s\nQt_6.9\n' "$NEED" | sort -V | tail -1)" != "Qt_6.9" ]; then
    echo "Error: binary requires $NEED, newer than SteamOS's Qt_6.9; it would not start." >&2
    exit 1
fi

echo "To install it over the current one:"
echo "  cp -f $OUT/SteamDeck_rEFInd \$HOME/.local/SteamDeck_rEFInd/GUI/SteamDeck_rEFInd"
