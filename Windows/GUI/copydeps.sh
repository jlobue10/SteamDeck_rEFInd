#!/bin/bash
# Copies the MSYS2/UCRT64 DLL dependency closure of the exe (and its Qt
# plugins) into the target directory, so the app runs on a machine without
# MSYS2 installed. windeployqt handles the Qt DLLs/plugins; this covers the
# MinGW/ICU/glib runtime DLLs it leaves behind. Usage: copydeps.sh <dir>
# Best-effort: ldd exits non-zero on some plugins, so no pipefail/set -e here.
cd "$1" || exit 1
find . \( -name '*.exe' -o -name '*.dll' \) -print0 \
    | xargs -0 -I{} ldd {} 2>/dev/null \
    | grep -o '/ucrt64/bin/[A-Za-z0-9_.+-]*\.dll' \
    | sort -u \
    | while read -r dep; do
        name="$(basename "$dep")"
        if [ ! -f "$name" ]; then
            cp "$dep" .
            echo "copied $name"
        fi
    done
