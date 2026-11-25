; Chiikawa Desktop Study Pet - Windows Per-User Installer (Inno Setup 6)
; Builds ChiikawaStudyPet-Setup-x64.exe from the verified windeployqt output directory.

#ifndef MyAppVersion
  #define MyAppVersion "1.1.0"
#endif

#ifndef SourceDir
  #define SourceDir "..\dist\ChiikawaStudyPet-Windows-x64"
#endif

#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

#define MyAppName "Chiikawa"
#define MyAppPublisher "John Nguyen"
#define MyAppURL "https://github.com/nguyenj1863/Chiikawa-Desktop-Study-Pet"
#define MyAppExeName "ChiikawaStudyPet.exe"
#define MyAppUserModelId "JohnNguyen.ChiikawaStudyPet"
#define MyStartupRunValueName "ChiikawaStudyPet"

[Setup]
; Stable application identifier for clean in-place upgrades and Windows Installed Apps registration.
AppId={{8B6E4A2E-9C3D-4F1A-B7E2-5D8A1C3F6E90}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases

; Per-user installation under %LOCALAPPDATA%\Programs\Chiikawa (no administrator privileges required).
PrivilegesRequired=lowest
DefaultDirName={autopf}\{#MyAppName}
UsePreviousAppDir=yes
UsePreviousTasks=yes

; Keep the installation flow simple and clean.
DisableWelcomePage=no
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes

; 64-bit Windows deployment.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; Output artifact configuration.
OutputDir={#OutputDir}
OutputBaseFilename=ChiikawaStudyPet-Setup-x64
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

; Application icon and Windows Installed Apps metadata.
SetupIconFile=..\assets\chiikawa.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}

; PE version resource metadata for the installer executable.
VersionInfoVersion={#MyAppVersion}.0
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription=Chiikawa Desktop Study Pet Installer
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}
VersionInfoCopyright=Copyright (C) 2026 John Nguyen

; Safely handle running instances during upgrades (prompts user interactively via Restart Manager).
CloseApplications=yes
CloseApplicationsFilter=*.exe,*.dll
RestartApplications=no

; Optional code signing when SIGN_ENABLED is defined and /Ssigntool=... is passed to ISCC.exe.
#ifdef SIGN_ENABLED
SignTool=signtool
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Package the entire verified windeployqt deployment directory (Qt DLLs, plugins, CRT DLLs, notices).
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
; Start Menu shortcut matches WindowsIdentity::startMenuShortcutPath() and AppUserModelID to avoid duplicates.
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"; AppUserModelID: "{#MyAppUserModelId}"; Comment: "Chiikawa Desktop Study Pet"
; Optional Desktop shortcut.
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"; AppUserModelID: "{#MyAppUserModelId}"; Comment: "Chiikawa Desktop Study Pet"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    // Remove per-user Windows startup registration if enabled by the app.
    // User preferences under HKCU\Software\ChiikawaStudyPet are preserved by default.
    if RegValueExists(HKEY_CURRENT_USER, 'Software\Microsoft\Windows\CurrentVersion\Run', '{#MyStartupRunValueName}') then
    begin
      RegDeleteValue(HKEY_CURRENT_USER, 'Software\Microsoft\Windows\CurrentVersion\Run', '{#MyStartupRunValueName}');
    end;
  end;
end;
