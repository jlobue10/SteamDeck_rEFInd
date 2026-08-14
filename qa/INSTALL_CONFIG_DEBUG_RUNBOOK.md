# Install Config "no effect at boot" — hardware debug runbook

> **RESOLVED 2026-08-14 — see §10.** No resolver or publish bug: the Deck
> and Windows GUIs were overwriting each other's installed config during
> alternating test cycles. Kept as a worked example of using the
> diagnostics; §1–§9 are the investigation as it ran.

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

---

Everything below exists so the investigation can be **finished on the
device** without the session that produced this branch.

## 6. State of the investigation (handoff)

Symptom (maintainer, Steam Deck OLED, released v3.4.0 on both OSes):
Install Config shows the success dialog on the Deck **and** on Windows,
but no change — including boot-entry/order changes — ever appears at
boot. Boot-entry changes cannot be masked by a theme include, so the
booting rEFInd is reading a different `refind.conf` than the one being
written.

Ruled out off-hardware (don't re-investigate these):

- **The staged ESP publish** (`GUI/src/espops/configinstall.cpp`) — unit
  tested (all six suites green) and exercised end-to-end in a container
  (root helper + `SUDO_USER` + fake ESP → correct install, exit 0).
- **The invocation chain** — sudoers template matches the exact argument
  vectors the GUI runs; version handshake passes (released GUI and helper
  both embed 3.4.0); dialog/exit-code plumbing audited on both platforms.
- **The release artifacts** — v3.4.0 pkg contains the helper root-owned at
  `etc/SteamDeck_rEFInd/`, both binaries need nothing newer than `Qt_6.9`
  (no pre-`main()` linker abort), Windows deploy ships the helper exe.
- **Parity skew** — `espops/` + `helper/main.cpp` are byte-identical with
  rEFInd_GUI HEAD; `espconstants.h` diffs are all intentional.

Established context:

- NVRAM-first ESP targeting is not new in 3.4.0: the bash version shipped
  in **v3.1.2** (2026-07-26) and the native resolver is a step-for-step
  port of it (compared tier by tier). If Install Config last visibly
  worked before 3.1.2, the misbehavior may span 3.1.2–3.3.0 unnoticed.
- The Deck was **never** part of the hardware verification for this code
  (rEFInd_GUI NATIVE_HELPER_DESIGN §7.3: Linux verified on a CachyOS
  desktop only; the Windows pass also ran on the dev machine).

Open hypotheses, in order (the §4 table maps diagnostic output to them):

1. rEFInd actually boots from the fallback path `EFI/BOOT/bootx64.efi`
   (own `refind.conf` beside it) while installs land in `EFI/refind/`.
2. Two rEFInd installs on different ESP-typed partitions; NVRAM boots one,
   the resolver verified-and-wrote the other.
3. Resolver tier fell through (dialog's `(chosen as …)` says "an ESP
   containing rEFInd" or "the running system's ESP" instead of "the ESP in
   the firmware's rEFInd boot entry") and picked a shadow copy.

Code map for the fix, wherever the data points:

- Linux resolver: `GUI/src/espops/espresolve_linux.cpp` —
  `EspResolver::resolve()` (the three tiers), `refindEspGuidFromNvram()`,
  `mountPointOf()`/`ensureMounted()`.
- Windows resolver: `GUI/src/espops/espresolve_win.cpp` — same shape.
- Shared NVRAM matching: `GUI/src/espops/loadoption.cpp` —
  `parseLoadOption()`, `loaderLooksLikeRefind()`.
- Tests: `GUI/src/tests/` (`cmake -DBUILD_GUI_TESTS=ON`, then `ctest`);
  add the captured NVRAM entry bytes as a `tst_loadoption`/`tst_espresolve`
  fixture.
- **Parity rule**: these files are byte-locked with the sibling rEFInd_GUI
  repo (the implementation lead). Land the fix there, byte-copy `espops/` +
  `helper/main.cpp` here, re-apply nothing (`espconstants.h` stays this
  repo's own).

Deck OLED specifics to expect in the diagnostic: DMI product name
`Galileo`; internal disk `nvme0n1`; **three** ESP-typed partitions on a
stock Deck — `nvme0n1p1` (label `esp`, where rEFInd lives) plus `efi-A`/
`efi-B` (SteamOS's A/B `steamcl` partitions, also ESP-typed) — more with
an SD card inserted. `/esp` and `/efi` are systemd automounts (the
diagnostic triggers them itself).

## 7. Getting this branch onto the Deck

```
cd ~ && git clone --branch claude/steamdeck-refind-install-debug-3utkvj --single-branch \
    https://github.com/jlobue10/SteamDeck_rEFInd SteamDeck_rEFInd-debug
```

Clone to `~/SteamDeck_rEFInd-debug`, NOT `~/SteamDeck_rEFInd` — the
release installer owns that path and `rm -rf`s it on every run.

## 8. Applying a candidate fix on the Deck

SteamOS has no compiler; build on any machine with podman using the
pinned toolchain (this matters — an unpinned build links a newer Qt and
aborts on-Deck before `main()`):

```
scripts/build_GUI_pinned.sh          # writes build-pinned/SteamDeck_rEFInd + _helper,
                                     # asserts Qt <= 6.9 on BOTH binaries
```

- **Resolver fixes need only the helper** (ESP resolution runs behind sudo
  on Linux). Hand-place it over the release copy:

  ```
  sudo install -o root -g root -m 0755 build-pinned/SteamDeck_rEFInd_helper \
      /etc/SteamDeck_rEFInd/SteamDeck_rEFInd_helper
  ```

  The version handshake stays green as long as the branch's `project
  VERSION` stays 3.4.0 (the installed release GUI expects exactly that
  from `helper --version`). The sudoers rule from the release install
  keeps matching — the path and argument vectors are unchanged.
- GUI-side fixes: also replace the copy the desktop entry runs,
  `~/.local/SteamDeck_rEFInd/GUI/SteamDeck_rEFInd` (the `/usr/bin` copy is
  optional — a SteamOS update wipes it anyway).
- Windows-side fixes: the in-process resolver lives in the GUI exe —
  rebuild in MSYS2 UCRT64 and replace
  `%ProgramFiles%\SteamDeck_rEFInd\SteamDeck_rEFInd.exe` (elevated), or
  build the full installer via `Windows/GUI/assemble-deploy.sh` + Inno.
- Re-run the §2 exercise after placing a fix; §1's diagnostics confirm
  where the write landed.

## 9. Immediate unblock (no fix required)

If the boot menu must be corrected today regardless of diagnosis, the
pre-3.1.2 manual path still works for a rEFInd that lives on the Deck's
own ESP:

```
ls /esp/. > /dev/null    # trigger the automount
sudo cp ~/.local/SteamDeck_rEFInd/GUI/refind.conf /esp/efi/refind/refind.conf
sudo sh -c 'cp ~deck/.local/SteamDeck_rEFInd/GUI/background.png \
    ~deck/.local/SteamDeck_rEFInd/GUI/os_icon*.png /esp/efi/refind/' 2>/dev/null
```

Caveat: if the §1 diagnostic shows the booting rEFInd is somewhere else
(fallback `EFI/BOOT`, SD card, …), copy there instead — blind `/esp`
writes are exactly the historical failure the resolver was built to end.

## 10. Outcome (2026-08-14, OLED Deck, v3.4.0 both OSes)

**No live bug in the install chain.** Every hypothesis in §6 was ruled out
on hardware with the §1 diagnostic:

- Single ESP-typed partition (`nvme0n1p1`), the same one `Boot0007
  rEFInd` points at; every logged install chose tier 1 and wrote there.
- `EFI/BOOT/bootx64.efi` is Valve's steamcl fallback (2025-dated), not a
  rEFInd — no fallback-path install.
- rEFInd demonstrably ran and read the just-installed config:
  `vars/PreviousBoot` was written seconds before the session's boot, and
  the live `refind.conf` matched the staged source byte-for-byte.

**The actual mechanism**: `refind.conf.prev` — which `configinstall.cpp`
writes as a copy of the live config taken at publish time — from a
10:19:06 install was the staged config **with CRLF line endings**: the
Windows GUI's rendition (its pre-3.4.1 staging wrote `\r\n`). The reboot
history (`last -x reboot shutdown`) showed the only window it could have
been published in was a ~3-minute Windows session between the Deck GUI's
10:13:20 install and the 10:17:00 SteamOS boot — the maintainer confirmed
running Install Config from Windows in the gaps. So each side's install
truthfully reported success while replacing the other side's config;
whenever the two sides' staged configs differ (i.e. whenever a change is
being tested on one side), the change never survives to the next look at
the boot menu.

**Fixes shipped from this** (rEFInd_GUI PR #90 + this repo's companion,
released as 3.4.1):

- `installConfigSet` publishes a `refind.conf.origin` sidecar
  (product/platform/version/sha256/timestamp) and notes in the install
  output when the config it replaces was installed by another GUI or
  changed by hand — the silent clobber is now a visible, attributed event.
- Create Config stages LF on both platforms (no more `QIODevice::Text`),
  so identical selections produce identical bytes across builds.
- Both diagnostics in this repo now report "MATCHES except line endings
  (same config installed by the other OS's GUI)" as its own verdict —
  during this investigation the plain `cmp` reported the Windows twin as
  `DIFFERS`, which §4's table would have misread — and print the origin
  sidecar when present.

The captured NVRAM dump and the CRLF-twin `.prev` remain in the report
files (`~/refind-diag-deck-before.txt`) as fixtures if the resolver ever
needs them.
