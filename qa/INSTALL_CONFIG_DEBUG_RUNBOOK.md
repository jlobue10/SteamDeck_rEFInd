# Install Config "no effect at boot" — hardware debug runbook

For the v3.4.0 report that Install Config shows a success dialog on both
the Deck and Windows, but changes (including boot-entry/order changes,
which no theme include can mask) never appear on the boot screen.

What is already established off-hardware:

- The whole install chain (GUI → helper/`espops` → staged ESP publish) is
  audited, unit-tested, and was exercised end-to-end in a container — the
  logic itself installs correctly when the target resolution is right.
- The released v3.4.0 artifacts are sound: helper present, versions
  matched (handshake passes), Qt symbols ≤ 6.9.
- The NVRAM-first ESP targeting is semantically identical to the bash
  version that shipped in v3.1.2, so the misbehavior may be older than
  3.4.0 and simply unnoticed.
- Conclusion: the config is being **written somewhere** — the open question
  is only *where the write lands* vs *where the booting rEFInd reads*.
  That question can only be answered on the machine.

Everything below is read-only except the one deliberate Install Config
exercise in step 2. Total time: ~15 minutes plus two reboots.

## 1. Capture diagnostics (both OSes, before touching anything)

### Deck (desktop mode, Konsole)

```
curl -L -o ~/diag.sh https://raw.githubusercontent.com/jlobue10/SteamDeck_rEFInd/claude/steamdeck-refind-install-debug-3utkvj/scripts/diagnose_install_config.sh
sudo bash ~/diag.sh 2>&1 | tee ~/refind-diag-deck-before.txt
```

(Save under `~`, not `/tmp` — `/tmp` does not survive the reboot in step 3.)

### Windows (elevated PowerShell)

```
curl.exe -L -o "$env:USERPROFILE\diag.ps1" https://raw.githubusercontent.com/jlobue10/SteamDeck_rEFInd/claude/steamdeck-refind-install-debug-3utkvj/Windows/GUI/diagnose_install_config.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File "$env:USERPROFILE\diag.ps1" 2>&1 | Tee-Object "$env:USERPROFILE\refind-diag-win-before.txt"
```

Skim each output for the three load-bearing lines before moving on:

- the `resolver NVRAM match` section — did tier `loader` or `label` match,
  and which partition GUID did it name?
- under each ESP section: `MATCHES` / `DIFFERS` against the staged source,
  and whether any partition is flagged
  `*** this is the ESP the firmware's rEFInd entry points at ***`;
- any `EFI/BOOT/bootx64.efi looks like a rEFInd binary` flag.

## 2. Controlled Install Config exercise (Deck first, then Windows)

1. Open the GUI. Make one **reversible, unmissable** change — swap Boot
   Options 1 and 2, or set a slot to None.
2. Create Config, then Install Config.
3. Write down the success dialog's last lines — `Installed N file(s) to …`
   and `(chosen as …)`. (They are also appended to
   `~/.local/SteamDeck_rEFInd/GUI/logs/` /
   `%LOCALAPPDATA%\SteamDeck_rEFInd\GUI\logs\`.)
4. Re-run the step-1 diagnostic into a second file
   (`…-after.txt`): the ESP that was just written must now say `MATCHES`.

## 3. Reboot and observe

Reboot into the boot menu. Then answer one question: **did the change from
step 2 appear?** Revert the change in the GUI afterwards either way
(Create Config + Install Config again).

## 4. Interpret

| Diagnostic result | Boot screen | Diagnosis | Fix direction |
|---|---|---|---|
| NVRAM-flagged ESP `MATCHES` after install | changed | No live bug — the original repro was stale (e.g. an attempt predating the 3.4.0 upgrade, or a theme masking a cosmetic-only change). | Close; re-test the original scenario. |
| NVRAM-flagged ESP `MATCHES` after install | unchanged | The write is correct but the booting rEFInd reads elsewhere. Check the `EFI/BOOT` fallback flag and any *second* ESP whose copy says `DIFFERS` — one of those is what actually boots. | Remove/refresh the fallback copy, or fix NVRAM to boot the real install; consider teaching the resolver to detect a fallback-path rEFInd. |
| Written ESP `DIFFERS` from the one the firmware flag names (resolver chose a different partition than tier 1 named, or `(chosen as …)` says tier 2/3) | unchanged | Resolver targeting bug — it picked a shadow location. | Fix `espops/espresolve_*` ordering/verification with this NVRAM dump as the test case. |
| `no rEFInd entry matched by loader path or exact label` | (any) | Tier 1 cannot see the rEFInd entry on this firmware — the `firmware boot entries` dump shows what the entry actually looks like. | Extend the matcher for the Deck's entry shape; the dump is the fixture. |
| Helper `MISSING`, version mismatch, or sudoers probe `FAILED` (Deck) | — | Install/packaging problem, not resolver — but note this *refuses* with a dialog rather than succeeding, so it cannot explain a success-but-no-effect report. | Re-run install-GUI.sh; if still broken, the diag output shows which piece is absent. |

## 5. Report back

Attach all four capture files (`…-before.txt` / `…-after.txt` from both
OSes) plus the noted dialog lines. The NVRAM dump in them doubles as the
regression fixture for whatever fix comes out of this.
