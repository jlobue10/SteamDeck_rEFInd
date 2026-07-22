#!/bin/bash
# deck_qa_test.sh — staged on-Deck QA for the claude/steamdeck-refind-i18n-audit-ntdqnt
# branch of SteamDeck_rEFInd (lives in that branch's qa/ directory; see
# qa/DECK_QA_CHECKLIST.md for the manual items). Run in desktop mode from Konsole:
#
#     bash deck_qa_test.sh              # stages 0-5 (build .. Install Config)
#     bash deck_qa_test.sh --full       # also stage 6 (rEFInd installer, touches NVRAM)
#
# Everything is logged to ~/deck_qa_<timestamp>.log — paste that file (plus
# ~/.local/SteamDeck_rEFInd/GUI/logs/SteamDeck_rEFInd.log) back into the chat.
# The script never modifies the ESP or NVRAM itself; only the app/scripts it
# tests do, and only in the stages that say so, after asking first.

set -u

APP=SteamDeck_rEFInd
BRANCH=claude/steamdeck-refind-i18n-audit-ntdqnt
REPO_URL=https://github.com/jlobue10/SteamDeck_rEFInd.git
QA_DIR="${QA_DIR:-$HOME/${APP}_qa}"
DATA_DIR="$HOME/.local/$APP"
LOG="$HOME/deck_qa_$(date +%Y%m%d-%H%M%S).log"
FULL=0
[ "${1:-}" = "--full" ] && FULL=1

exec > >(tee -a "$LOG") 2>&1

PASS=(); FAIL=(); SKIP=()
ok()   { echo "  [PASS] $1"; PASS+=("$1"); }
bad()  { echo "  [FAIL] $1"; FAIL+=("$1"); }
skip() { echo "  [SKIP] $1"; SKIP+=("$1"); }
pause() { echo; read -r -p ">>> $1 [Enter to continue] " _; }
stage() { echo; echo "==================== Stage $1: $2 ===================="; }

echo "Deck QA for $APP branch $BRANCH — $(date)"
echo "Log: $LOG"

# ---------------------------------------------------------------- Stage 0
stage 0 "Preflight"
. /etc/os-release 2>/dev/null
echo "  OS: ${PRETTY_NAME:-unknown}   Model: $(cat /sys/class/dmi/id/product_name 2>/dev/null)"
[ "${ID:-}" = "steamos" ] && ok "running on SteamOS" || skip "not SteamOS (${ID:-unknown}) — continuing anyway"
command -v git >/dev/null && ok "git available" || { bad "git missing"; exit 1; }
# Baseline NVRAM snapshot of our own, before anything else runs.
efibootmgr -v > "$HOME/deck_qa_nvram_baseline.txt" 2>/dev/null \
    && ok "baseline NVRAM snapshot -> ~/deck_qa_nvram_baseline.txt" \
    || skip "efibootmgr not readable (unexpected on a Deck)"

if [ -d "$QA_DIR/.git" ]; then
    git -C "$QA_DIR" fetch origin "$BRANCH" && git -C "$QA_DIR" checkout -q "$BRANCH" \
        && git -C "$QA_DIR" reset -q --hard "origin/$BRANCH"
else
    git clone -b "$BRANCH" "$REPO_URL" "$QA_DIR"
fi
[ -d "$QA_DIR/.git" ] && ok "branch checked out at $QA_DIR ($(git -C "$QA_DIR" rev-parse --short HEAD))" \
    || { bad "could not clone/checkout branch"; exit 1; }

# ---------------------------------------------------------------- Stage 1
stage 1 "Pinned build (podman)"
BIN=""
if [ -n "${QA_BINARY:-}" ] && [ -x "$QA_BINARY" ]; then
    BIN="$QA_BINARY"; ok "using supplied binary $BIN"
elif command -v podman >/dev/null; then
    ( cd "$QA_DIR" && scripts/build_GUI_pinned.sh )
    if [ -x "$QA_DIR/build-pinned/$APP" ]; then
        BIN="$QA_DIR/build-pinned/$APP"; ok "pinned build produced $BIN"
    else
        bad "pinned build did not produce a binary (see output above)"
    fi
else
    skip "podman not available — build on another Linux box with scripts/build_GUI_pinned.sh, then re-run with QA_BINARY=/path/to/$APP"
fi
[ -z "$BIN" ] && { echo; echo "No binary to test — stopping before the GUI stages."; FULL=0; }

# ---------------------------------------------------------------- Stage 2
if [ -n "$BIN" ]; then
stage 2 "Binary sanity (linkage + headless run)"
# A binary linked against too-new Qt dies in the dynamic linker instantly;
# surviving a 5s offscreen run proves linkage and startup (incl. async scan).
timeout 5 env QT_QPA_PLATFORM=offscreen "$BIN" >/dev/null 2>&1
[ $? -eq 124 ] && ok "binary runs on this SteamOS Qt (survived 5s offscreen)" \
    || bad "binary exited early offscreen — check Qt symbol versions"
[ -f "$DATA_DIR/GUI/logs/$APP.log" ] && ok "diagnostics log exists ($DATA_DIR/GUI/logs/$APP.log)" \
    || skip "diagnostics log not created yet"

# ---------------------------------------------------------------- Stage 3
stage 3 "Interactive GUI checks (manual — see DECK_QA_CHECKLIST.md)"
cat <<'EOT'
  The GUI will open. Work through checklist items G1-G8:
    G1 window appears immediately; combos briefly show "Scanning…"
    G2 detected OSes fill the slots; hover a boot combo entry -> loader/volume tooltip
    G3 Language combo: switch to Espanol, then 日本語, then العربية
       (Arabic must mirror the whole layout right-to-left), then back
    G4 standard dialog buttons (e.g. About box) follow the language
    G5 Preview with "Include all OSes" UNCHECKED - slots only
    G6 Preview with it CHECKED - extra detected entries appear after the slots
    G7 Create Config with the box UNCHECKED, then close the preview
    G8 leave the GUI OPEN and return to this terminal
EOT
pause "Launching the GUI now"
"$BIN" & GUI_PID=$!
pause "Did G1-G7 pass? (record any failures; GUI still open)"

# ---------------------------------------------------------------- Stage 4
stage 4 "Create Config + extras diff (writes only ~/.local)"
CONF="$DATA_DIR/GUI/refind.conf"
[ -f "$CONF" ] && ok "refind.conf generated" || bad "refind.conf missing after Create Config"
cp -f "$CONF" /tmp/qa_conf_noextras.conf 2>/dev/null
pause "In the GUI: CHECK 'Include all OSes', click Create Config again"
if ! cmp -s /tmp/qa_conf_noextras.conf "$CONF"; then
    echo "  --- stanzas added by 'Include all OSes': ---"
    diff /tmp/qa_conf_noextras.conf "$CONF" | sed 's/^/    /'
    ok "extras changed the generated config (verify the diff above is sane)"
else
    skip "no extra stanzas (fine if all detected OSes already occupy slots)"
fi
grep -q "scanfor manual" "$CONF" && ok "config keeps scanfor manual" || bad "scanfor manual missing"

# ---------------------------------------------------------------- Stage 5
stage 5 "Install Config (writes the ESP; .prev backup check)"
pause "In the GUI: click Install Config, complete its dialogs, then come back"
ESP_REFIND=""
for d in /esp/efi/refind /esp/EFI/refind /efi/EFI/refind; do
    sudo test -f "$d/refind.conf" 2>/dev/null && { ESP_REFIND="$d"; break; }
done
if [ -n "$ESP_REFIND" ]; then
    sudo cmp -s "$CONF" "$ESP_REFIND/refind.conf" \
        && ok "ESP refind.conf matches the generated one ($ESP_REFIND)" \
        || bad "ESP refind.conf differs from the generated file"
    sudo test -f "$ESP_REFIND/refind.conf.prev" \
        && ok "rollback copy refind.conf.prev present" \
        || bad "refind.conf.prev missing (expected after overwrite of an existing config)"
else
    skip "no rEFInd dir found on /esp or /efi — is rEFInd installed?"
fi
kill "$GUI_PID" 2>/dev/null

# ---------------------------------------------------------------- Stage 6
if [ "$FULL" = 1 ]; then
stage 6 "rEFInd installer (TOUCHES NVRAM — --full only)"
echo "  This reruns the pacman installer: boot entries are recreated, drivers redownloaded."
pause "Proceed with SteamDeck_rEFInd_install.sh?"
( cd "$QA_DIR" && bash SteamDeck_rEFInd_install.sh )
ls -1t "$DATA_DIR/nvram-backups"/efibootmgr-*.txt 2>/dev/null | head -1 | grep -q . \
    && ok "NVRAM snapshot written to $DATA_DIR/nvram-backups/" \
    || bad "no NVRAM snapshot found"
for drv in UsbXbox360Dxe.efi TouchI2cDxe.efi; do
    P="${ESP_REFIND}/drivers_x64/$drv"
    if sudo test -f "$P"; then
        [ "$(sudo head -c2 "$P")" = "MZ" ] && ok "$drv on ESP is a valid PE binary" \
            || bad "$drv on ESP fails the MZ check"
    else
        skip "$drv not on ESP (download may have been skipped)"
    fi
done
efibootmgr | head -4 | sed 's/^/  /'
echo "  ^ verify rEFInd leads BootOrder, then REBOOT to confirm the menu (checklist B1-B3)."
else
stage 6 "rEFInd installer — skipped (rerun with --full to include it)"
fi
fi # BIN

# ---------------------------------------------------------------- Summary
echo
echo "==================== Summary ===================="
echo "  PASS: ${#PASS[@]}   FAIL: ${#FAIL[@]}   SKIP: ${#SKIP[@]}"
for f in "${FAIL[@]:-}"; do [ -n "$f" ] && echo "  FAILED: $f"; done
echo
echo "Send back: $LOG"
echo "      and: $DATA_DIR/GUI/logs/$APP.log"
echo "Manual results: note any G1-G8 / B1-B3 items that failed."
