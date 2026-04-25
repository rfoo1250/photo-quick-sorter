; Photo Quick Sorter — Inno Setup 6.x installer script
; Download Inno Setup: https://jrsoftware.org/isinfo.php
;
; ── PREREQUISITES BEFORE COMPILING ───────────────────────────────────────────
;
; 1. Build the app (Debug is the current default — see AppExe toggle below):
;      cmake --build build --config Debug
;
;    For a Release build (smaller, faster, no VS required on target machine):
;      - Rebuild wxWidgets 3.3.1 with Release config to get wxmsw33u_*.lib files
;      - Then: cmake --build build --config Release
;      - Switch the AppExe #define below to the Release path
;      - Uncomment the VC++ Redist lines in [Files] and [Run]
;
; 2. ffmpeg.exe and ffprobe.exe are taken from C:\Tools\FFmpeg-build\bin\
;    Update those paths in [Files] if you move or update the FFmpeg build.
;
; ── DEBUG vs RELEASE TOGGLE ──────────────────────────────────────────────────
; Uncomment the Release line (and comment out Debug) once wxWidgets is rebuilt:
;#define AppExe "..\build\Release\PhotoQuickSorter.exe"
#define AppExe "..\build\Debug\PhotoQuickSorter.exe"
; Note: the Debug exe requires Visual Studio 2022 (any edition) to be installed
;       on the target machine. Release removes this requirement.
; ─────────────────────────────────────────────────────────────────────────────

#define AppName    "Photo Quick Sorter"
#define AppVersion "0.7.1"
#define AppPublisher "rfoo1250"

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
; Unique ID — do not change after first release (used by Windows to identify updates/uninstalls)
AppId={{3F8A2D1E-7B4C-4E9F-A6D2-1C5B8E3F7A09}
DefaultDirName={autopf}\PhotoQuickSorter
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir=output
OutputBaseFilename=PhotoQuickSorterSetup-{#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
PrivilegesRequired=admin
WizardStyle=modern
MinVersion=10.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
; Main application executable
Source: "{#AppExe}";                 DestDir: "{app}"; Flags: ignoreversion

; UI assets (keycap PNGs used by the interface)
Source: "..\assets\*";               DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs

; Bundled FFmpeg binaries — update paths if you move the FFmpeg build
Source: "C:\Tools\FFmpeg-build\bin\ffmpeg.exe";  DestDir: "{app}\bin"; Flags: ignoreversion
Source: "C:\Tools\FFmpeg-build\bin\ffprobe.exe"; DestDir: "{app}\bin"; Flags: ignoreversion

; Visual C++ 2022 Redistributable — only needed when using the Release exe.
; Uncomment the line below after switching to the Release build:
;Source: "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\14.44.35112\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{group}\{#AppName}";           Filename: "{app}\PhotoQuickSorter.exe"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{commondesktop}\{#AppName}";   Filename: "{app}\PhotoQuickSorter.exe"; Tasks: desktopicon

[Run]
; Visual C++ Redistributable — uncomment when switching to the Release exe:
;Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Visual C++ Runtime..."; Flags: waituntilterminated

; Offer to launch the app after installation finishes
Filename: "{app}\PhotoQuickSorter.exe"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent
