; ============================================================================
;  NeuralStage — Inno Setup installer
;
;  Deliverables (staged to Installer\Binaries\ by build.ps1):
;    NeuralStage.exe   → Program Files\Atij 666 Studio\NeuralStage\
;                        Standalone host — hosts VST3, CLAP and LV2 plugins.
;    NeuralStage.vst3  → C:\Program Files\Common Files\VST3\NeuralStage.vst3\
;                        NeuralStage itself as a VST3 plugin for DAWs.
;    NeuralStage.clap  → C:\Program Files\Common Files\CLAP\
;                        NeuralStage itself as a CLAP plugin for DAWs.
;
;  The .ico helper file is installed alongside the exe so shortcuts and
;  the uninstaller can reference a stable path.
; ============================================================================

#define MyAppName       "NeuralStage"
#define MyAppVersion    "0.2.1"
#define MyAppPublisher  "Atij 666 Studio"
#define MyAppExeName    "NeuralStage.exe"
#define MyAppIconName   "NeuralStage.ico"
#define BinariesDir     "Binaries"
#define ProjectRoot     ".."

[Setup]
; NOTE: unique GUID — never reuse for another product.
AppId={{B3F0E5C2-7A4D-4F8A-9C2E-3D1F0B6E8A47}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL=https://github.com/atij666
DefaultDirName={autopf}\{#MyAppPublisher}\{#MyAppName}
DefaultGroupName={#MyAppPublisher}\{#MyAppName}
DisableProgramGroupPage=yes
OutputDir={#ProjectRoot}\Installer\Output
OutputBaseFilename=NeuralStage-{#MyAppVersion}-Setup
SetupIconFile={#ProjectRoot}\Assets\{#MyAppIconName}
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#MyAppIconName}
UninstallDisplayName={#MyAppName} {#MyAppVersion}
PrivilegesRequired=admin

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
; Standalone host executable
Source: "{#BinariesDir}\NeuralStage.exe"; DestDir: "{app}"; Flags: ignoreversion

; VST3 plugin bundle → C:\Program Files\Common Files\VST3\NeuralStage.vst3\
; DAWs locate VST3 plugins by bundle folder name, not filename, so it must
; land in the system VST3 folder (not alongside the standalone exe).
; {commoncf64} = C:\Program Files\Common Files on a 64-bit Windows install.
Source: "{#BinariesDir}\NeuralStage.vst3\*"; DestDir: "{commoncf64}\VST3\NeuralStage.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

; CLAP plugin → C:\Program Files\Common Files\CLAP\
Source: "{#BinariesDir}\NeuralStage.clap"; DestDir: "{commoncf64}\CLAP"; Flags: ignoreversion

; App icon — installed hidden so Windows shell shortcuts and the uninstaller
; can reference a stable path without the file appearing in Explorer's view
; of the install directory.
Source: "{#ProjectRoot}\Assets\{#MyAppIconName}"; DestDir: "{app}"; Flags: ignoreversion

; Manual is hosted online — no PDF bundled in the installer.

[Icons]
Name: "{group}\{#MyAppName}";           Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\{#MyAppIconName}"; WorkingDir: "{app}"
Name: "{group}\{#MyAppName} Manual (online)"; Filename: "https://github.com/atij666studio/NeuralStage/blob/main/Docs/NeuralStage-Manual.md"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{commondesktop}\{#MyAppName}";   Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\{#MyAppIconName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent

[Code]
function InitializeSetup(): Boolean;
begin
  Result := True;
  if not IsWin64 then
  begin
    MsgBox('{#MyAppName} requires a 64-bit version of Windows.',
           mbError, MB_OK);
    Result := False;
  end;
end;
