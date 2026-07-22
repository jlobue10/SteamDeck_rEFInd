# On-Deck QA checklist — `claude/steamdeck-refind-i18n-audit-ntdqnt`

Covers everything the branch changed that only real hardware can verify:
i18n + RTL rendering, async UI, preview, extra stanzas, ESP/NVRAM safety nets.
Run `deck_qa_test.sh` in Konsole (desktop mode); it automates the file-level
checks and pauses for the manual items below. Budget ~20 minutes plus one
reboot for the `--full` pass.

Fetch and run on the Deck (Konsole, desktop mode):

```
curl -fsSLO https://raw.githubusercontent.com/jlobue10/SteamDeck_rEFInd/claude/steamdeck-refind-i18n-audit-ntdqnt/qa/deck_qa_test.sh
bash deck_qa_test.sh          # stages 0–5: build → GUI → Create/Install Config
bash deck_qa_test.sh --full   # adds stage 6: rEFInd installer (touches NVRAM)
```

If the Deck has no `podman`, build the binary on any Linux PC with
`scripts/build_GUI_pinned.sh`, copy it over, and run
`QA_BINARY=/path/to/SteamDeck_rEFInd bash deck_qa_test.sh`.

## Automated by the script (just read its PASS/FAIL lines)

- Stage 0: SteamOS check, branch checkout, **baseline NVRAM snapshot** to
  `~/deck_qa_nvram_baseline.txt` (your independent recovery copy).
- Stage 1: pinned build (this also proves the Qt 6.9 snapshot still works).
- Stage 2: binary survives a headless run on the Deck's real Qt (linkage check);
  diagnostics log appears under `~/.local/SteamDeck_rEFInd/GUI/logs/`.
- Stage 4: `refind.conf` generated; diff of extras-off vs extras-on configs.
- Stage 5: ESP `refind.conf` matches the generated file; `refind.conf.prev`
  rollback copy exists.
- Stage 6 (`--full`): NVRAM snapshot in `~/.local/SteamDeck_rEFInd/nvram-backups/`;
  both `.efi` drivers on the ESP pass the `MZ` signature check; rEFInd heads
  BootOrder.

## Manual GUI items (stage 3 pauses for these)

| # | Check | Expected |
|---|-------|----------|
| G1 | Launch feel | Window appears instantly; boot combos briefly show "Scanning…", buttons disabled until the scan lands (~1–2 s), then SteamOS/Windows fill slots 1–2 |
| G2 | Tooltips | Hovering a boot-combo entry shows its `loader …` / `volume …` lines |
| G3 | Language switching | Switch to Español → 日本語 → العربية → System default. Every visible string changes each time; **Arabic mirrors the entire layout right-to-left**; CJK/Arabic render with real glyphs, no boxes |
| G4 | Qt's own buttons | With a non-English language active, open About — the OK button is translated (this validates the embedded qtbase catalogs) |
| G5 | Preview, extras off | Mock boot screen shows only the slotted OSes over the chosen background; default selection highlighted; second tab shows the config text |
| G6 | Preview, extras on | Check "Include all OSes" → any detected-but-unslotted OS appears **after** the slots as a placeholder tile (skip if you have ≤2 OSes — everything is slotted) |
| G7 | Create Config | With extras **unchecked** first (the script diffs after you re-create with it checked) |
| G8 | Return to terminal | Leave the GUI open; the script drives the rest |

## Boot verification after `--full` (one reboot)

| # | Check | Expected |
|---|-------|----------|
| B1 | rEFInd menu appears | Icons in slot order, chosen background, correct default highlighted; extra entries (if any) rightmost with stock Windows/Linux icons |
| B2 | Boot each entry | Every stanza — slotted and extra — actually boots its OS |
| B3 | Input | Touchscreen and/or a controller work in the menu (driver downloads were re-validated) |

## What to send back

1. `~/deck_qa_<timestamp>.log` (the script's full transcript)
2. `~/.local/SteamDeck_rEFInd/GUI/logs/SteamDeck_rEFInd.log`
3. A note per failed G/B item — one line each is plenty
4. If anything went sideways at boot: `~/deck_qa_nvram_baseline.txt` plus the
   newest file in `~/.local/SteamDeck_rEFInd/nvram-backups/`

## Recovery (if a `--full` run misbehaves)

- Boot entries: `scripts/restore_EFI_entries.sh` recreates SteamOS + rEFInd
  entries; your baseline snapshot shows the original state.
- Config: `sudo cp refind.conf.prev refind.conf` inside the ESP's
  `EFI/refind/` restores the previous config.
- Worst case the Deck's boot manager (hold Volume-Down at power-on) can always
  boot SteamOS directly.
