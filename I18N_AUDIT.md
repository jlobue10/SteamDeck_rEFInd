# Multi-language (i18n) audit — SteamDeck_rEFInd

Audit of user-facing text across the Qt GUI and the bash/PowerShell scripts,
what this branch fixes, and what is deliberately left for later. The sibling
rEFInd_GUI repo received the same audit and the same fixes (this repo's GUI
sources mirror it).

## Summary

| Area | State before | State after this branch |
|---|---|---|
| Qt GUI translation pipeline | Dead: no translator in `main.cpp` at all; `SteamDeck_rEFInd_en_US.ts` an empty stub; no `.qm` built | Working: translator installed, `.qm` compiled at build time and embedded under `:/i18n` |
| GUI `tr()` coverage | Good, with a few gaps | Complete (About box, "None" combo entry, `Platform` error strings) |
| Shipped languages | English only | English + German (`de`), Spanish (`es`), French (`fr`), Japanese (`ja`), Korean (`ko`), Simplified Chinese (`zh_CN`), Ukrainian (`uk`), Arabic (`ar`), Persian/Farsi (`fa`) |
| Desktop entry | English `Comment=` only | Localized `Comment[<lang>]=` |
| Bash scripts (Deck-side) | English only | Unchanged — audited, recommendations below |
| PowerShell scripts (`Windows/`, `Windows/GUI/`) | English only | Unchanged — audited, recommendations below |
| Inno Setup installer | English only | Unchanged — recommendation below |

## GUI findings and fixes

1. **There was no translation pipeline at all.** Unlike rEFInd_GUI (whose
   `main.cpp` at least tried to load from `:/i18n/`), this repo's `main.cpp`
   never created a `QTranslator`, and the sole `.ts` file was an empty stub.
   `CMakeLists.txt` called `qt_create_translation()` whose output no target
   depended on. Fixed: `main.cpp` now installs a `qtbase` translator plus the
   app translator (locale-fallback `QTranslator::load(QLocale(), ...)`
   overload, es_MX → es → en), and `qt_add_translations()` compiles the
   `.ts` files during the build and embeds the `.qm` output at `:/i18n`.
   The Qt 5 fallback path compiles `.qm` into `translations/` next to the
   binary (`main.cpp` tries that location second).

2. **`tr()` gaps closed:**
   - The About box text was a plain `QStringLiteral`.
   - The "None" boot-slot combo entry was a file-scope `static const QString`
     — initialized before `main()` installs the translator, so it could never
     be translated. It is now a function (`noneOption()`), evaluated at use
     time. Note: settings persist combo selections *by text*, so switching UI
     language makes a previously saved "None" fail its `findText` lookup; the
     slot then falls back to defaults, which is the intended graceful path.
   - `platform.cpp`'s two user-visible launch-failure strings
     (`powershell.exe/sudo could not be started.`) now go through
     `QCoreApplication::translate("Platform", ...)`.

3. **Translator noise removed.** `.ui` strings that are overwritten at
   runtime (the path placeholders replaced by `Platform::dataDir()`-based
   hints, the Pacman/Sourceforge combo items replaced by
   `Platform::installSourceOptions()`) and the numeral `5` are now marked
   `notr="true"` so they never reach translators.

4. **Deliberately NOT translated** — these are identifiers, not prose:
   - `BootEntry` `displayName`/`menuName` values ("Windows", "SteamOS",
     "Windows (SD)", "Ventoy", "Batocera"…). They are OS proper nouns, they
     are compared as matching keys (`applyAutoSelection()`, settings
     persistence by text, dedup in `comboOptions()`), and the `osdetect_*`
     files that produce them are kept byte-identical with the sibling
     rEFInd_GUI repo. Translating them would break matching and the
     cross-repo parity rule.
   - `menuentry` names written into `refind.conf` — rendered by rEFInd at
     boot; keep them ASCII proper nouns.
   - Settings keys, `refind.conf` directives, file names.

5. **Build-environment note.** The pinned Arch snapshot build
   (`scripts/build_GUI_pinned.sh`, `.github/workflows/arch-release.yml`)
   already installs `qt6-tools`, and the MSYS2 workflow installs
   `mingw-w64-ucrt-x86_64-qt6-tools`, so `lupdate`/`lrelease` are available
   everywhere `qt_add_translations()` needs them. Verified: both GUIs build
   with the new CMake and the compiled `.qm` files resolve correctly
   (including the de_DE → de fallback).

## Shipped translations

`GUI/src/SteamDeck_rEFInd_{ar,de,es,fa,fr,ja,ko,uk,zh_CN}.ts` cover all 89 messages.
`SteamDeck_rEFInd_en_US.ts` is the source-language reference and
intentionally has empty translations (source text is used as-is). The
language is picked from the system locale automatically — on a Deck this
follows the language set in Steam/desktop-mode settings; there is no in-app
language switcher (a possible future improvement — a combo writing a
`Language` key to the INI and installing the translator before `MainWindow`
construction).

Arabic and Persian are right-to-left languages: their catalogs translate Qt's `QT_LAYOUT_DIRECTION` key to `RTL` (anchored in `main.cpp` so `lupdate` keeps the key), which makes Qt mirror the entire widget layout automatically. The rEFInd boot screen itself stays left-to-right, so strings that reference on-screen icon order ("leftmost icon") still mean the physical left.

### Adding a language (contributor guide)

1. Add `SteamDeck_rEFInd_<lang>.ts` to `TS_FILES` in `GUI/src/CMakeLists.txt`.
2. Generate/refresh it from the sources:
   `lupdate main.cpp mainwindow.cpp mainwindow.ui platform.cpp osdetect_*.cpp -ts SteamDeck_rEFInd_<lang>.ts`
   (or build the `update_translations` CMake target, Qt 6).
3. Translate with Qt Linguist (`linguist SteamDeck_rEFInd_<lang>.ts`).
4. Build — `qt_add_translations` compiles and embeds it; nothing else to do.
   Leave untranslated entries `unfinished`: they fall back to English.

When GUI strings change, re-run `lupdate` for **all** files in `TS_FILES` so
the `.ts` files stay in sync with the sources.

## Scripts audit (not changed in this branch)

All bash and PowerShell user-facing text is English-only: zenity dialogs
(`scripts/pacman_install.sh` ~8, `scripts/sourceforge_install.sh` ~10,
`scripts/scan_esp.sh` ~5, the config-install pair), the standalone installers'
echo output and prompts (`SteamDeck_rEFInd_install.sh`,
`refind_install_no_pacman.sh`, `install-GUI.sh`), and the PowerShell
`Write-Step` banners and result summaries (`Windows/GUI/install_rEFInd.ps1`
~48 `Write-*` calls, `uninstall_rEFInd.ps1` ~19, plus the top-level
`Windows/` dual-boot-fix scripts).

Localizing them was deferred on purpose — the constraints are real:

1. **The tamper hash check.** `scripts/install_config_from_GUI.sh`,
   `scripts/lib_esp_target.sh`, and `scripts/install_config_from_GUI_root.sh`
   are embedded in the GUI binary at build time and SHA-256-verified before
   every run. Any edit to them must ship together with a rebuilt, re-released
   GUI, or every existing user's Install Config button reports the script as
   "modified". Script localization therefore has to ride a coordinated
   release, not a standalone branch.
2. **Cross-repo parity.** The `Windows/GUI/*.ps1` scripts are kept identical
   to rEFInd_GUI's `windows/` copies modulo renames, and several bash/ps1
   pairs must stay in behavioral parity. Localization must land in both repos
   in lockstep to keep the diffs auditable.
3. **Output is part of the contract.** `Platform::installConfig()` captures
   script output for the result dialog and relies on *exit codes* for
   success/failure — that part is localization-safe — but the zenity-driven
   scripts keep a strict stdout protocol (diagnostics on stderr only).
   Localized text must never move output between streams. The `bootnext`
   systemd units log to the journal; those messages should stay English.

Recommended approach when it is tackled:

- **Bash**: GNU gettext (`gettext.sh`, `TEXTDOMAIN=steamdeck_refind`,
  `.po/.mo` under a new `po/` directory), with a no-op fallback
  (`command -v gettext >/dev/null || gettext() { printf '%s' "$1"; }`) so the
  scripts keep working on a stock SteamOS image. Route zenity `--text`
  arguments through it. Keep diagnostics/log lines English (they end up in
  bug reports); localize only summaries and prompts.
- **PowerShell**: the standard `Import-LocalizedData` mechanism (`.psd1`
  string tables per culture next to each script), falling back to `en-US`.
  Keep the numbered `Write-Step` arithmetic intact.
- **Inno Setup** (`Windows/GUI/SteamDeck_rEFInd.iss`): add a `[Languages]`
  section — the compiler ships official translations; this is a cheap,
  isolated win for the installer UI.
- Priority order: GUI-launched zenity dialogs (highest visibility) →
  installer banners/prompts → uninstaller. Plain log output can stay English.

## Other surfaces

- `SteamDeck_rEFInd.desktop`: localized `Comment[<lang>]` added (done).
  `install-GUI.sh` rewrites `Exec=`/`Icon=` paths only, so the localized
  lines survive installation untouched.
- `README.md`: English-only; per-language READMEs are only worth it with a
  commitment to keep them in sync — not recommended now.
- `refind.conf` / rEFInd boot menu: rEFInd itself has no i18n; the icon-based
  menu needs no text. Nothing to do here.
