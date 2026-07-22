; ---------------------------------------------------------------------------
; Inno Setup script for Miniscope DAQ
;
; Wraps the standalone bundle produced by the CMake `deploy` target
; (build/Release/ - launcher + configs at the top, real exe + DLLs in bin/)
; into a familiar Setup.exe: per-user install (NO admin rights), Start Menu
; shortcut, optional desktop shortcut, and an entry in "Add or Remove Programs"
; with a working uninstaller.
;
; Per-user is deliberate: the launcher writes ./userConfigs relative to the
; install dir, so the app must land somewhere the user can write to. {autopf}
; with PrivilegesRequired=lowest resolves to %LocalAppData%\Programs, which is
; user-writable and needs no elevation.
;
; Build locally (after `cmake --build build --config Release --target deploy`):
;   iscc packaging\windows\MiniscopeDAQ.iss
;
; CI overrides version / source / output name from the command line, e.g.:
;   iscc /DMyAppVersion=1.10 /DMySourceDir=C:\...\build\Release ^
;        /DMyOutputDir=C:\...\artifacts /DMyOutputBaseName=MiniscopeDAQ-v1.10-Setup ^
;        packaging\windows\MiniscopeDAQ.iss
; ---------------------------------------------------------------------------

#define MyAppName "Miniscope DAQ"
#define MyAppPublisher "Aharoni Lab"
#define MyAppExeName "MiniscopeDAQ.exe"
#define MyAppURL "https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software"

; --- Overridable from the iscc command line (sensible local defaults) -------
#ifndef MyAppVersion
  #define MyAppVersion "dev"
#endif
#ifndef MySourceDir
  #define MySourceDir "..\..\build\Release"
#endif
#ifndef MyOutputDir
  #define MyOutputDir "..\..\build\installer"
#endif
#ifndef MyOutputBaseName
  #define MyOutputBaseName "MiniscopeDAQ-" + MyAppVersion + "-Setup"
#endif

[Setup]
; A stable AppId ties every version to the same uninstall entry, so a new
; release upgrades the previous install in place instead of stacking copies.
; Never change this GUID once shipped.
AppId={{B7E6B2D4-3F8A-4C21-9E5D-2A1F6C9B4E70}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
; Controls the wizard caption AND the "Add or Remove Programs" display name.
; Without this, Inno shows "<AppName> version <AppVersion>"; set it explicitly
; for a clean "Miniscope DAQ 1.2" that matches the version shown in the app's
; Help (sourced from source/main.cpp VERSION_NUMBER - see the CI step).
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={autopf}\MiniscopeDAQ
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
; Per-user install: no UAC elevation, lands under %LocalAppData%\Programs.
PrivilegesRequired=lowest
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
OutputDir={#MyOutputDir}
OutputBaseFilename={#MyOutputBaseName}
SetupIconFile=..\..\source\miniscope_icon.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
; Tuck the uninstaller (unins000.exe + unins000.dat) into bin/ rather than the
; top level. Inno hardcodes those file names and the .dat is required by the
; uninstaller, so they can't be renamed or removed - but bin/ is where this
; project already hides its DLL/plugin clutter, leaving the top level clean
; (launcher + configs + bin/). Users uninstall via the Start Menu shortcut or
; Add/Remove Programs, both labelled "Uninstall", never "unins000".
UninstallFilesDir={app}\bin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Take the whole deployed bundle verbatim: launcher + configs at the top, real
; exe + DLLs + Qt plugins + QML under bin/. The bundle is already self-contained
; (no conda env required), so a recursive copy is all the installer does.
Source: "{#MySourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent
