; Inno Setup script for SteamDeck_rEFInd (Windows).
;
; Packages the deploy\ staging folder (assembled by the build — see
; .github/workflows/windows-release.yml or the README "Windows" section) into
; a per-user installer. It installs to %LOCALAPPDATA%\SteamDeck_rEFInd, which is
; exactly the directory the app reads/writes at runtime (Platform::dataDir()),
; so no files are duplicated and no admin rights are needed to install. The
; app itself requests Administrator at launch via its embedded manifest.

#define AppName "SteamDeck rEFInd GUI"
#define AppVersion "2.7.0"
#define AppExe "SteamDeck_rEFInd.exe"

[Setup]
AppId={{3D7E1C42-9A5B-4F60-BE18-2C6A9D4F1E70}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=jlobue10
AppPublisherURL=https://github.com/jlobue10/SteamDeck_rEFInd
DefaultDirName={localappdata}\SteamDeck_rEFInd
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=lowest
OutputBaseFilename=SteamDeck_rEFInd-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#AppExe}

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"

[Files]
; deploy\ holds the final runtime layout: exe + Qt/MinGW DLLs + plugin dirs,
; plus windows\*.ps1, icons\, backgrounds\, and GUI\refind.conf (seed config).
Source: "..\..\deploy\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Tasks: desktopicon
; Shortcut inside GUI\ (the folder the app's Open Folder button shows) to the
; backgrounds folder the randomizer picks from.
Name: "{app}\GUI\backgrounds"; Filename: "{app}\backgrounds"

[Run]
; unchecked: don't launch the GUI by default when the installer finishes.
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent runascurrentuser unchecked

[UninstallRun]
; Undo the rEFInd boot entry and ESP files before the app files disappear.
; The per-user uninstaller is unelevated, so the script elevates via UAC.
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\windows\uninstall_rEFInd.ps1"""; Flags: shellexec waituntilterminated; Verb: runas; RunOnceId: "UninstallRefind"; Check: ShouldRemoveRefind

[UninstallDelete]
; The app generates data the uninstaller's manifest doesn't cover (the
; GUI-generated refind.conf and PNGs, settings ini, logs, boot-entry backup);
; scrub the whole per-user app dir so nothing lingers in %LOCALAPPDATA%.
Type: filesandordirs; Name: "{app}"

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
