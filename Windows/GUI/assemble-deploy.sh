#!/bin/bash
# Assembles deploy/ (final runtime layout) from a built exe. Shared by the CI
# workflow and local installer builds.
#   Usage: assemble-deploy.sh <path-to-built-SteamDeck_rEFInd.exe> [deploy-dir]
set -euo pipefail

EXE="$1"
DEPLOY="${2:-deploy}"
BUILD_DIR="$(dirname "$EXE")"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"   # <repo>/Windows/GUI
REPO="$(cd "$SCRIPT_DIR/../.." && pwd)"

rm -rf "$DEPLOY"
mkdir -p "$DEPLOY"
# windeployqt can exit non-zero over a missing translations catalog on MSYS2;
# that is harmless here since copydeps.sh below completes the DLL closure.
windeployqt6 "$EXE" --dir "$DEPLOY" --no-translations --compiler-runtime >/dev/null 2>&1 || true
cp "$EXE" "$DEPLOY/"
cp "$BUILD_DIR"/*.dll "$DEPLOY/" 2>/dev/null || true
for d in platforms styles imageformats iconengines tls networkinformation generic; do
    [ -d "$BUILD_DIR/$d" ] && cp -r "$BUILD_DIR/$d" "$DEPLOY/"
done
bash "$SCRIPT_DIR/copydeps.sh" "$DEPLOY" >/dev/null

# The GUI-build PowerShell scripts live in Windows/GUI/ but ship to the runtime
# data dir as .../windows/ (lowercase) to avoid the Windows/ case collision.
mkdir -p "$DEPLOY/windows" "$DEPLOY/GUI"
cp "$SCRIPT_DIR"/*.ps1 "$DEPLOY/windows/"
cp -r "$REPO/icons" "$DEPLOY/"
cp -r "$REPO/backgrounds" "$DEPLOY/"
cp "$REPO/refind-GUI.conf" "$DEPLOY/GUI/refind.conf"

echo "deploy assembled at $DEPLOY"
echo "  exe:         $(ls -1 "$DEPLOY"/SteamDeck_rEFInd.exe)"
echo "  dll count:   $(ls -1 "$DEPLOY"/*.dll | wc -l)"
echo "  plugin dirs: $(cd "$DEPLOY" && ls -d platforms styles imageformats 2>/dev/null | tr '\n' ' ')"
echo "  ps1 scripts: $(ls -1 "$DEPLOY"/windows/*.ps1 | wc -l)"
echo "  seed conf:   $(ls "$DEPLOY"/GUI/refind.conf)"
