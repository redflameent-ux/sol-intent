; Inno Setup script - builds Sol-Intent-Setup.exe
[Setup]
AppName=Sol Intent
AppVersion=0.1.0
AppPublisher=Sol Intent
DefaultDirName={autopf}\Sol Intent
DefaultGroupName=Sol Intent
OutputBaseFilename=Sol-Intent-Setup
OutputDir=Output
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
DisableProgramGroupPage=yes
WizardStyle=modern

[Files]
; The VST3 plugin goes where every DAW looks for it
Source: "..\build\SolIntent_artefacts\Release\VST3\Sol Intent.vst3\*"; DestDir: "{commoncf64}\VST3\Sol Intent.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
; Stand-alone app for playing it without a DAW
Source: "..\build\SolIntent_artefacts\Release\Standalone\Sol Intent.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Sol Intent (stand-alone)"; Filename: "{app}\Sol Intent.exe"

[Run]
Filename: "{app}\Sol Intent.exe"; Description: "Try Sol Intent now (stand-alone)"; Flags: nowait postinstall skipifsilent unchecked
