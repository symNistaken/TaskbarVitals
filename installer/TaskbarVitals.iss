; Inno Setup script for Taskbar Vitals (https://jrsoftware.org/isinfo.php).
; Per-user install by default: no administrator rights needed, installs to %LOCALAPPDATA%\Programs.
; Setup also offers "Install for all users" (Program Files, needs administrator rights).
; Build with installer\build-installer.ps1 (or: ISCC /DAppVersion=1.2.3 installer\TaskbarVitals.iss).

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#define Root ".."

[Setup]
; Same AppId as BatteryHub, the original project (and Wireless Battery): installing over either replaces it.
AppId={{7E0B5C2A-3F4D-4C8E-9B61-2D5A8F1C4E93}
AppName=Taskbar Vitals
AppVersion={#AppVersion}
AppVerName=Taskbar Vitals {#AppVersion}
AppPublisher=Taskbar Vitals contributors
DefaultDirName={autopf}\Taskbar Vitals
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#Root}\dist
OutputBaseFilename=TaskbarVitals-Setup-{#AppVersion}
SetupIconFile={#Root}\res\app.ico
UninstallDisplayIcon={app}\TaskbarVitals.exe
UninstallDisplayName=Taskbar Vitals
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=no

[Tasks]
Name: "autostart"; Description: "Start Taskbar Vitals when I sign in to Windows"

[Files]
Source: "{#Root}\TaskbarVitals.exe"; DestDir: "{app}"; Flags: ignoreversion
; Reads the CPU temperature (only used when the user turns that on; it copies itself to Program Files then).
Source: "{#Root}\TaskbarVitalsTemp.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\third_party\headsetcontrol\headsetcontrol.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#Root}\installer\THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion

[InstallDelete]
; Installing over an earlier name of this app: remove its exe and Start menu shortcut.
; BatteryHub is the original project (1.1.x); Wireless Battery was this project's first name.
Type: files; Name: "{app}\BatteryHub.exe"
Type: files; Name: "{autoprograms}\BatteryHub.lnk"
Type: files; Name: "{app}\WirelessBattery.exe"
Type: files; Name: "{autoprograms}\Wireless Battery.lnk"

[Icons]
Name: "{autoprograms}\Taskbar Vitals"; Filename: "{app}\TaskbarVitals.exe"

[Run]
Filename: "{app}\TaskbarVitals.exe"; Description: "Start Taskbar Vitals now"; Flags: nowait postinstall skipifsilent; Check: not IsUpdate

[UninstallRun]
; Takes the CPU temperature helper (its task and its copy in Program Files) off the PC, if it was set up.
Filename: "{app}\TaskbarVitalsTemp.exe"; Parameters: "--remove"; Verb: "runas"; Flags: shellexec runhidden waituntilterminated; RunOnceId: "RemoveTempHelper"; Check: TempHelperInstalled
Filename: "{app}\TaskbarVitals.exe"; Parameters: "--quit"; Flags: runhidden waituntilterminated; RunOnceId: "QuitApp"

[Code]
const
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';

function TempHelperInstalled(): Boolean;
begin
  Result := FileExists(ExpandConstant('{commonpf}\Taskbar Vitals\TaskbarVitalsTemp.exe'));
end;

function IsUpdate(): Boolean;
begin
  Result := ExpandConstant('{param:WBUPDATE|0}') = '1';
end;

{ Asks the copy in this folder to quit, whichever name its exe has. }
function QuitRunningApp(): Boolean;
var
  Code: Integer;
begin
  Result := True;
  if FileExists(ExpandConstant('{app}\TaskbarVitals.exe')) then
    Result := Exec(ExpandConstant('{app}\TaskbarVitals.exe'), '--quit', '', SW_HIDE, ewWaitUntilTerminated, Code) and (Code = 0)
  else if FileExists(ExpandConstant('{app}\WirelessBattery.exe')) then
    Result := Exec(ExpandConstant('{app}\WirelessBattery.exe'), '--quit', '', SW_HIDE, ewWaitUntilTerminated, Code) and (Code = 0)
  else if FileExists(ExpandConstant('{app}\BatteryHub.exe')) then
    Result := Exec(ExpandConstant('{app}\BatteryHub.exe'), '--quit', '', SW_HIDE, ewWaitUntilTerminated, Code) and (Code = 0);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if IsUpdate() and not QuitRunningApp() then
    Result := 'Taskbar Vitals could not close. Close it and try updating again.';
end;

function InstalledRunValue(): String;
begin
  Result := '"' + ExpandConstant('{app}\TaskbarVitals.exe') + '"';
end;

{ True for a startup entry that starts an earlier exe of this copy (BatteryHub.exe or WirelessBattery.exe).
  The value name 'WirelessBattery' stays the same for all of them, so only its path changes. }
function IsOldRunValue(Value: String): Boolean;
begin
  Result := (CompareText(Value, '"' + ExpandConstant('{app}\BatteryHub.exe') + '"') = 0) or
            (CompareText(Value, '"' + ExpandConstant('{app}\WirelessBattery.exe') + '"') = 0);
end;

{ Removes the startup entry only if it points at this installed copy, so a portable copy's
  "Start with Windows" setting is left alone. }
procedure RemoveOwnAutostart();
var
  Value: String;
begin
  if RegQueryStringValue(HKCU, RunKey, 'WirelessBattery', Value) and
     ((CompareText(Value, InstalledRunValue()) = 0) or IsOldRunValue(Value)) then
    RegDeleteValue(HKCU, RunKey, 'WirelessBattery');
end;

{ An entry that still starts an earlier exe of this copy now starts TaskbarVitals.exe (also on silent updates). }
procedure MigrateAutostart();
var
  Value: String;
begin
  if RegQueryStringValue(HKCU, RunKey, 'WirelessBattery', Value) and IsOldRunValue(Value) then
    RegWriteStringValue(HKCU, RunKey, 'WirelessBattery', InstalledRunValue());
end;

{ True if this account can create the chosen folder (or write to it if it already exists).
  Without administrator rights, folders such as D:\Programs or Program Files are often read-only. }
function CanWriteTo(Dir: String): Boolean;
var
  Parent, Probe: String;
begin
  Dir := RemoveBackslashUnlessRoot(Dir);
  if DirExists(Dir) then
  begin
    Probe := AddBackslash(Dir) + '~wirelessbattery-write-test.tmp';
    Result := SaveStringToFile(Probe, '', False);
    DeleteFile(Probe);
    exit;
  end;
  { Find the nearest folder that exists and check that a new folder can be created in it. }
  Parent := Dir;
  while not DirExists(Parent) do
  begin
    if ExtractFileDir(Parent) = Parent then
    begin
      Result := False;  { the drive itself does not exist }
      exit;
    end;
    Parent := ExtractFileDir(Parent);
  end;
  Probe := AddBackslash(Parent) + '~wirelessbattery-write-test';
  Result := CreateDir(Probe);
  if Result then
    RemoveDir(Probe);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  { Not in silent installs: there the wizard would wait on a hidden page forever. }
  if (CurPageID = wpSelectDir) and not WizardSilent() and not CanWriteTo(WizardDirValue()) then
  begin
    if IsAdminInstallMode() then
      MsgBox('Setup cannot create or write to this folder:' + #13#10 + WizardDirValue() + #13#10#13#10 +
        'Please choose a different folder.', mbError, MB_OK)
    else
      MsgBox('Your account cannot create or write to this folder without administrator rights:' + #13#10 +
        WizardDirValue() + #13#10#13#10 +
        'Choose a folder you own (the default one always works), or close Setup, run it again and ' +
        'pick "Install for all users".', mbError, MB_OK);
    Result := False;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssInstall) and not IsUpdate() then
    QuitRunningApp();  { upgrade: close the old version so its files can be replaced }
  if CurStep = ssPostInstall then
    MigrateAutostart();
  if (CurStep = ssPostInstall) and not IsUpdate() then
  begin
    { Same value the app's own "Start with Windows" menu item writes, so the two stay in sync. }
    if WizardIsTaskSelected('autostart') then
      RegWriteStringValue(HKCU, RunKey, 'WirelessBattery', InstalledRunValue())
    else
      RemoveOwnAutostart();
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    RemoveOwnAutostart();
    RegDeleteKeyIncludingSubkeys(HKCU, 'Software\WirelessBattery');
  end;
end;
