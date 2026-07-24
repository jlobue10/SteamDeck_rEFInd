; Inno Setup script for SteamDeck_rEFInd (Windows).
;
; Packages the deploy\ staging folder (assembled by the build — see
; .github/workflows/windows-release.yml or the README "Windows" section) into
; a machine-wide installer. Privileged executable code lives under Program
; Files; mutable user configuration remains under
; %LOCALAPPDATA%\SteamDeck_rEFInd.

#define AppName "SteamDeck rEFInd GUI"
#define AppVersion "3.0.0"
#define AppExe "SteamDeck_rEFInd.exe"

[Setup]
AppId={{3D7E1C42-9A5B-4F60-BE18-2C6A9D4F1E70}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=jlobue10
AppPublisherURL=https://github.com/jlobue10/SteamDeck_rEFInd
DefaultDirName={autopf}\SteamDeck_rEFInd
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=admin
OutputBaseFilename=SteamDeck_rEFInd-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#AppExe}

[Languages]
; Only languages whose .isl ships with the Inno Setup compiler are listed;
; the app itself covers more (see I18N_AUDIT.md).
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "portuguese"; MessagesFile: "compiler:Languages\Portuguese.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "ukrainian"; MessagesFile: "compiler:Languages\Ukrainian.isl"
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"

[Files]
; deploy\ holds the final runtime layout: exe + Qt/MinGW DLLs + plugin dirs,
; plus windows\*.ps1, icons\, backgrounds\, and GUI\refind.conf (seed config).
Source: "..\..\deploy\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[InstallDelete]
; Pre-3.x installed the app itself into %LOCALAPPDATA%\SteamDeck_rEFInd
; (per-user). Its self-elevating exe and privileged helper scripts must not
; stay runnable from the user-writable data directory, so remove the legacy
; program files — keeping the user's GUI\, icons\ and backgrounds\ data —
; along with the old per-user shortcuts that point at the removed exe.
Type: filesandordirs; Name: "{localappdata}\SteamDeck_rEFInd\windows"
Type: files; Name: "{localappdata}\SteamDeck_rEFInd\{#AppExe}"
Type: files; Name: "{localappdata}\SteamDeck_rEFInd\*.dll"
Type: files; Name: "{localappdata}\SteamDeck_rEFInd\unins*"
Type: filesandordirs; Name: "{localappdata}\SteamDeck_rEFInd\platforms"
Type: filesandordirs; Name: "{localappdata}\SteamDeck_rEFInd\styles"
Type: filesandordirs; Name: "{localappdata}\SteamDeck_rEFInd\imageformats"
Type: filesandordirs; Name: "{localappdata}\SteamDeck_rEFInd\iconengines"
Type: filesandordirs; Name: "{localappdata}\SteamDeck_rEFInd\tls"
Type: filesandordirs; Name: "{localappdata}\SteamDeck_rEFInd\networkinformation"
Type: filesandordirs; Name: "{localappdata}\SteamDeck_rEFInd\generic"
Type: filesandordirs; Name: "{localappdata}\SteamDeck_rEFInd\translations"
Type: files; Name: "{userdesktop}\{#AppName}.lnk"
Type: filesandordirs; Name: "{userprograms}\{#AppName}"

[Registry]
; Drop the legacy per-user (HKCU) uninstall registration so Settings > Apps
; doesn't list a duplicate whose uninstaller binaries were removed above. The
; machine-wide registration under HKLM supersedes it.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\{{3D7E1C42-9A5B-4F60-BE18-2C6A9D4F1E70}_is1"; ValueType: none; Flags: deletekey dontcreatekey

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Tasks: desktopicon
; Shortcut inside GUI\ (the folder the app's Open Folder button shows) to the
; backgrounds folder the randomizer picks from.
Name: "{localappdata}\SteamDeck_rEFInd\GUI\backgrounds"; Filename: "{localappdata}\SteamDeck_rEFInd\backgrounds"

[Run]
; Preserve an enabled legacy task while moving its elevated action out of the
; user-writable data directory.
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\windows\rEFInd_bg_randomizer_task.ps1"" -Migrate"; Flags: runhidden waituntilterminated
; unchecked: don't launch the GUI by default when the installer finishes.
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent runascurrentuser unchecked

[UninstallRun]
; Undo the rEFInd boot entry and ESP files before the app files disappear.
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\windows\uninstall_rEFInd.ps1"""; Flags: shellexec waituntilterminated; Verb: runas; RunOnceId: "UninstallRefind"; Check: ShouldRemoveRefind

[UninstallDelete]
; The app generates data the uninstaller's manifest doesn't cover (the
; GUI-generated refind.conf and PNGs, settings ini, logs, boot-entry backup);
; scrub the per-user data dir separately from the Program Files installation.
Type: filesandordirs; Name: "{localappdata}\SteamDeck_rEFInd"

[Code]
var
  RemoveRefind: Boolean;

function InitializeUninstall(): Boolean;
begin
  RemoveRefind := MsgBox('Also remove the rEFInd boot manager itself?' + #13#10#13#10 +
    'Yes: delete the rEFInd firmware boot entry and the EFI\refind files, restoring direct Windows boot.' + #13#10 +
    'No: keep rEFInd bootable and remove only the GUI app.',
    mbConfirmation, MB_YESNO) = IDYES;
  Result := True;
end;

function ShouldRemoveRefind(): Boolean;
begin
  Result := RemoveRefind;
end;
