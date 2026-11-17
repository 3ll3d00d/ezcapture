; ezcapture installer, built with Inno Setup 7 (https://jrsoftware.org/isinfo.php)
;
; Normally built by build-installer.ps1, which supplies the defines below. To build by hand:
;   ISCC.exe --define=AppVersion=1.2.3 --define=SrcRelease=..\x64\Release ... ezcapture.iss
;
; This file must stay pure ASCII: Inno Setup 7 rejects bytes that are invalid in the script's code page.

#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#ifndef AppVersionNumeric
  #define AppVersionNumeric "0.0.0.0"
#endif
; directories holding mwcapture.ax and bmcapture.ax for each build configuration
#ifndef SrcRelease
  #define SrcRelease "..\x64\Release"
#endif
#ifndef SrcReleaseNoArch
  #define SrcReleaseNoArch "..\x64\ReleaseNoArch"
#endif
#ifndef SrcReleaseTrace
  #define SrcReleaseTrace "..\x64\ReleaseTrace"
#endif
#ifndef SrcReleaseTraceNoArch
  #define SrcReleaseTraceNoArch "..\x64\ReleaseTraceNoArch"
#endif
#ifndef VcRedist
  #define VcRedist "deps\vc_redist.x64.exe"
#endif

; the VC++ runtime version we bundle, the installed runtime must be at least this new
#define VcRedistVersion GetVersionNumbersString(AddBackslash(SourcePath) + VcRedist)
#if VcRedistVersion == ""
  #error Unable to read the version of the bundled VC++ redistributable
#endif

; the Magewell runtime version matching the SDK we link against
#define MwVerHandle FileOpen(AddBackslash(SourcePath) + "..\mwsdk\VERSION")
#define MwRuntimeVersion Trim(FileRead(MwVerHandle))
#expr FileClose(MwVerHandle)

#define AppGuid "{EBA4B682-9E08-45B1-9BFE-A9CBBF617942}"
#define AppUrl "https://github.com/3ll3d00d/ezcapture"

[Setup]
AppId={#StringChange(AppGuid, '{', '{{')}
AppName=ezcapture
AppVersion={#AppVersion}
AppPublisher=3ll3d00d
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}/releases
AppComments=DirectShow capture filters for Magewell and Blackmagic capture cards
VersionInfoVersion={#AppVersionNumeric}
VersionInfoProductTextVersion={#AppVersion}
VersionInfoTextVersion={#AppVersion}
SetupArchitecture=x64
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
DefaultDirName={autopf}\ezcapture
DisableProgramGroupPage=yes
DisableWelcomePage=no
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputDir=output
OutputBaseFilename=ezcapture-{#AppVersion}-setup
UninstallDisplayName=ezcapture {#AppVersion}
SetupLogging=yes
CloseApplications=yes
RestartApplications=no

[Types]
Name: "full"; Description: "Magewell and Blackmagic"
Name: "mw"; Description: "Magewell only"
Name: "bm"; Description: "Blackmagic only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "mw"; Description: "Magewell filter (Pro Capture PCIe and USB Capture)"; Types: full mw
Name: "bm"; Description: "Blackmagic filter (DeckLink)"; Types: full bm

[Tasks]
Name: "trace"; Description: "Also install the trace builds"; Components: mw bm; Flags: unchecked

[Files]
; AVX2 or no-arch build, chosen to suit this CPU
Source: "{#SrcRelease}\mwcapture.ax"; DestDir: "{app}"; Components: mw; Check: UseAvx2; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#SrcReleaseNoArch}\mwcapture.ax"; DestDir: "{app}"; Components: mw; Check: not UseAvx2; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#SrcRelease}\bmcapture.ax"; DestDir: "{app}"; Components: bm; Check: UseAvx2; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#SrcReleaseNoArch}\bmcapture.ax"; DestDir: "{app}"; Components: bm; Check: not UseAvx2; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#SrcReleaseTrace}\mwcapture.ax"; DestDir: "{app}"; DestName: "mwcapture-trace.ax"; Components: mw; Tasks: trace; Check: UseAvx2; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#SrcReleaseTraceNoArch}\mwcapture.ax"; DestDir: "{app}"; DestName: "mwcapture-trace.ax"; Components: mw; Tasks: trace; Check: not UseAvx2; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#SrcReleaseTrace}\bmcapture.ax"; DestDir: "{app}"; DestName: "bmcapture-trace.ax"; Components: bm; Tasks: trace; Check: UseAvx2; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#SrcReleaseTraceNoArch}\bmcapture.ax"; DestDir: "{app}"; DestName: "bmcapture-trace.ax"; Components: bm; Tasks: trace; Check: not UseAvx2; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "..\COPYING"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\mwsdk\RedistLicense.rtf"; DestDir: "{app}\licenses"; DestName: "MWCaptureRT-RedistLicense.rtf"; Components: mw; Flags: ignoreversion
; prerequisites, extracted and run on demand from PrepareToInstall
Source: "{#VcRedist}"; Flags: dontcopy
Source: "..\mwsdk\MWCaptureRT.exe"; Flags: dontcopy

[Registry]
Root: HKLM64; Subkey: "Software\ezcapture"; Flags: uninsdeletekey
Root: HKLM64; Subkey: "Software\ezcapture"; ValueType: string; ValueName: "Version"; ValueData: "{#AppVersion}"
Root: HKLM64; Subkey: "Software\ezcapture"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"
Root: HKLM64; Subkey: "Software\ezcapture"; ValueType: string; ValueName: "Build"; ValueData: "AVX2"; Check: UseAvx2
Root: HKLM64; Subkey: "Software\ezcapture"; ValueType: string; ValueName: "Build"; ValueData: "NoArch"; Check: not UseAvx2

[UninstallDelete]
Type: files; Name: "{app}\version.txt"

[Run]
Filename: "https://www.blackmagicdesign.com/support/family/capture-and-playback"; Description: "Open the Blackmagic Desktop Video download page"; Flags: postinstall shellexec skipifsilent nowait; Check: NeedDesktopVideo
Filename: "https://github.com/Nevcairiel/LAVFilters/releases"; Description: "Open the LAV Filters download page"; Flags: postinstall shellexec skipifsilent nowait unchecked; Check: NeedLav
Filename: "{#AppUrl}#configuration"; Description: "Open the player setup guide"; Flags: postinstall shellexec skipifsilent nowait unchecked

[Messages]
SelectTasksDesc=Do you want the trace builds as well?

[CustomMessages]
TraceExplain=The trace builds are a second copy of each filter that writes a detailed log (signal and format changes, frame timings, errors) to your Windows temp folder.%n%nThey are registered alongside the normal filters under their own names, e.g. "Magewell Pro Capture (Trace)", so in your player you choose which one to use. Use them when diagnosing a problem or when asked for a log in a bug report; use the normal filters day to day as logging costs CPU and disk space.%n%nYou can add or remove them later by running this installer again.

[Code]
const
  PF_AVX2_INSTRUCTIONS_AVAILABLE = 40;
  VcRuntimeKey = 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';
  DeckLinkIteratorClsid = 'CLSID\{BA6C6F44-6DA5-4DCE-94AA-EE2D1372A676}';
  LavAudioClsid = 'CLSID\{E8E73B6B-4CB3-44A4-BE99-4F7BCB96E491}';
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#AppGuid}_is1';

var
  PrereqPage: TWizardPage;
  PrereqList: TNewCheckListBox;
  PrereqListLabel: TNewStaticText;
  ManualLabel: TNewStaticText;
  ManualMemo: TNewMemo;
  VcIndex, MwIndex: Integer;
  DeclinedVc, DeclinedMw: Boolean;
  PreviousVersion: String;

function IsProcessorFeaturePresent(ProcessorFeature: DWORD): Boolean;
  external 'IsProcessorFeaturePresent@kernel32.dll stdcall';

function UseAvx2: Boolean;
begin
  Result := IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE);
end;

function VersionString(V: Int64): String;
var
  A, B, C, D: Word;
begin
  UnpackVersionComponents(V, A, B, C, D);
  Result := Format('%d.%d.%d.%d', [A, B, C, D]);
end;

{ --- prerequisite detection --- }

function InstalledVcVersion(var V: Int64): Boolean;
var
  Installed, Major, Minor, Bld: Cardinal;
begin
  Result := RegQueryDWordValue(HKLM64, VcRuntimeKey, 'Installed', Installed) and (Installed = 1)
    and RegQueryDWordValue(HKLM64, VcRuntimeKey, 'Major', Major)
    and RegQueryDWordValue(HKLM64, VcRuntimeKey, 'Minor', Minor)
    and RegQueryDWordValue(HKLM64, VcRuntimeKey, 'Bld', Bld);
  if Result then
    V := PackVersionComponents(Word(Major), Word(Minor), Word(Bld), 0);
end;

function BundledVcVersion: Int64;
var
  A, B, C, D: Word;
begin
  StrToVersion('{#VcRedistVersion}', Result);
  UnpackVersionComponents(Result, A, B, C, D);
  Result := PackVersionComponents(A, B, C, 0);
end;

function NeedVc: Boolean;
var
  V: Int64;
begin
  Result := (not InstalledVcVersion(V)) or (ComparePackedVersion(V, BundledVcVersion) < 0);
end;

function MwRuntimeDll: String;
begin
  Result := ExpandConstant('{sys}\LibMWCapture.dll');
end;

function NeedMw: Boolean;
var
  Installed, Bundled: Int64;
begin
  StrToVersion('{#MwRuntimeVersion}', Bundled);
  Result := (not GetPackedVersion(MwRuntimeDll, Installed)) or (ComparePackedVersion(Installed, Bundled) < 0);
end;

function HasDesktopVideo: Boolean;
begin
  Result := RegKeyExists(HKCR64, DeckLinkIteratorClsid);
end;

function HasLav: Boolean;
begin
  Result := RegKeyExists(HKCR64, LavAudioClsid);
end;

function NeedDesktopVideo: Boolean;
begin
  Result := WizardIsComponentSelected('bm') and not HasDesktopVideo;
end;

function NeedLav: Boolean;
begin
  Result := not HasLav;
end;

function WantVc: Boolean;
begin
  Result := NeedVc and not DeclinedVc;
end;

function WantMw: Boolean;
begin
  Result := WizardIsComponentSelected('mw') and NeedMw and not DeclinedMw;
end;

function Bullet(const S: String): String;
begin
  Result := '* ' + S + #13#10#13#10;
end;

{ things the user has to sort out themselves, empty if there is nothing to do }
function ManualSteps: String;
var
  V: Int64;
begin
  Result := '';
  if WizardIsComponentSelected('bm') and not HasDesktopVideo then
    Result := Result + Bullet('Blackmagic Desktop Video is not installed. Blackmagic do not allow it to be bundled, so ' +
      'download and install it from https://www.blackmagicdesign.com/support/family/capture-and-playback ' +
      '(setup can open that page when it finishes). The Blackmagic filter will not find any cards until it is installed.');
  if WizardIsComponentSelected('mw') then
  begin
    if NeedMw and DeclinedMw then
    begin
      if GetPackedVersion(MwRuntimeDll, V) then
        Result := Result + Bullet('The Magewell runtime is version ' + VersionString(V) + ', the filter is built against ' +
          '{#MwRuntimeVersion}. Run MWCaptureRT {#MwRuntimeVersion} or later before using the filter.')
      else
        Result := Result + Bullet('The Magewell runtime (LibMWCapture.dll) is not installed. The Magewell filter cannot be ' +
          'registered without it; run this installer again and let it install the runtime.');
    end;
    Result := Result + Bullet('Magewell cards also need the Magewell driver: the Pro Capture driver for PCIe cards, or ' +
      'the USB Capture Utility for USB devices, from https://www.magewell.com/downloads. USB Capture users must set the ' +
      'output formats in the Utility, see the README.');
  end;
  if NeedVc and DeclinedVc then
    Result := Result + Bullet('The Microsoft Visual C++ runtime {#VcRedistVersion} or later is needed. Download it from ' +
      'https://aka.ms/vc14/vc_redist.x64.exe; the filters cannot be registered without it.');
  if not HasLav then
    Result := Result + Bullet('LAV Filters are not registered on this PC. JRiver Media Center and MPC-HC/MPC-BE ship ' +
      'their own copy, so nothing is needed for those players; other players need LAV Filters (LAV Audio) installed from ' +
      'https://github.com/Nevcairiel/LAVFilters/releases');
end;

{ --- wizard --- }

procedure CreatePrereqPage;
var
  Top: Integer;
begin
  PrereqPage := CreateCustomPage(wpSelectTasks, 'Prerequisites',
    'Other software the ezcapture filters depend on.');

  PrereqListLabel := TNewStaticText.Create(PrereqPage);
  PrereqListLabel.Parent := PrereqPage.Surface;
  PrereqListLabel.Caption := 'These are missing or out of date, setup can install them for you:';
  PrereqListLabel.Top := 0;
  PrereqListLabel.Width := PrereqPage.SurfaceWidth;

  PrereqList := TNewCheckListBox.Create(PrereqPage);
  PrereqList.Parent := PrereqPage.Surface;
  PrereqList.Top := PrereqListLabel.Top + PrereqListLabel.Height + ScaleY(4);
  PrereqList.Width := PrereqPage.SurfaceWidth;
  PrereqList.Height := ScaleY(52);

  Top := PrereqList.Top + PrereqList.Height + ScaleY(10);
  ManualLabel := TNewStaticText.Create(PrereqPage);
  ManualLabel.Parent := PrereqPage.Surface;
  ManualLabel.Caption := 'Things you need to check or do yourself:';
  ManualLabel.Top := Top;
  ManualLabel.Width := PrereqPage.SurfaceWidth;

  ManualMemo := TNewMemo.Create(PrereqPage);
  ManualMemo.Parent := PrereqPage.Surface;
  ManualMemo.Top := ManualLabel.Top + ManualLabel.Height + ScaleY(4);
  ManualMemo.Width := PrereqPage.SurfaceWidth;
  ManualMemo.Height := PrereqPage.SurfaceHeight - ManualMemo.Top;
  ManualMemo.ReadOnly := True;
  ManualMemo.ScrollBars := ssVertical;
  ManualMemo.WordWrap := True;
end;

procedure RefreshPrereqPage;
var
  V: Int64;
  S: String;
begin
  PrereqList.Items.Clear;
  VcIndex := -1;
  MwIndex := -1;
  if NeedVc then
  begin
    if InstalledVcVersion(V) then
      S := 'installed: ' + VersionString(V)
    else
      S := 'not installed';
    VcIndex := PrereqList.AddCheckBox('Microsoft Visual C++ runtime {#VcRedistVersion}', S, 0, not DeclinedVc,
      True, False, False, nil);
  end;
  if WizardIsComponentSelected('mw') and NeedMw then
  begin
    if GetPackedVersion(MwRuntimeDll, V) then
      S := 'installed: ' + VersionString(V)
    else
      S := 'not installed';
    MwIndex := PrereqList.AddCheckBox('Magewell capture runtime {#MwRuntimeVersion}', S, 0, not DeclinedMw,
      True, False, False, nil);
  end;
  if PrereqList.Items.Count = 0 then
    PrereqListLabel.Caption := 'Everything setup can install for you is already present.'
  else
    PrereqListLabel.Caption := 'These are missing or out of date, setup can install them for you:';
  PrereqList.Visible := PrereqList.Items.Count > 0;
  { show the consequences of unticking straight away }
  ManualMemo.Text := ManualSteps;
  if ManualMemo.Text = '' then
    ManualMemo.Text := 'Nothing.';
end;

procedure PrereqListClick(Sender: TObject);
begin
  if VcIndex >= 0 then
    DeclinedVc := not PrereqList.Checked[VcIndex];
  if MwIndex >= 0 then
    DeclinedMw := not PrereqList.Checked[MwIndex];
  ManualMemo.Text := ManualSteps;
end;

procedure InitializeWizard;
var
  Gap: Integer;
begin
  if RegQueryStringValue(HKLM64, UninstallKey, 'DisplayVersion', PreviousVersion) then
  begin
    if PreviousVersion = '{#AppVersion}' then
      WizardForm.WelcomeLabel2.Caption := WizardForm.WelcomeLabel2.Caption + #13#10#13#10 +
        'ezcapture ' + PreviousVersion + ' is already installed, you can use this to change which filters are installed.'
    else
      WizardForm.WelcomeLabel2.Caption := WizardForm.WelcomeLabel2.Caption + #13#10#13#10 +
        'ezcapture ' + PreviousVersion + ' is currently installed and will be replaced by {#AppVersion}.';
  end;

  { make room for the trace build explanation above the task list }
  Gap := WizardForm.TasksList.Top - WizardForm.SelectTasksLabel.Top - WizardForm.SelectTasksLabel.Height;
  WizardForm.SelectTasksLabel.Caption := CustomMessage('TraceExplain');
  WizardForm.SelectTasksLabel.WordWrap := True;
  WizardForm.SelectTasksLabel.Height := ScaleY(140);
  WizardForm.TasksList.Top := WizardForm.SelectTasksLabel.Top + WizardForm.SelectTasksLabel.Height + Gap;
  WizardForm.TasksList.Height := WizardForm.SelectTasksPage.Height - WizardForm.TasksList.Top;

  CreatePrereqPage;
  PrereqList.OnClickCheck := @PrereqListClick;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = PrereqPage.ID then
    RefreshPrereqPage;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if PageID = PrereqPage.ID then
    Result := (not NeedVc) and not (WizardIsComponentSelected('mw') and NeedMw) and (ManualSteps = '');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpSelectComponents) and not WizardIsComponentSelected('mw') and not WizardIsComponentSelected('bm') then
  begin
    MsgBox('Select at least one filter to install.', mbError, MB_OK);
    Result := False;
  end;
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo, MemoComponentsInfo,
  MemoGroupInfo, MemoTasksInfo: String): String;
var
  Build, Prereqs: String;
begin
  Result := 'Version:' + NewLine + Space + '{#AppVersion}';
  if PreviousVersion <> '' then
    Result := Result + ' (replacing ' + PreviousVersion + ')';
  Result := Result + NewLine + NewLine;
  if UseAvx2 then
    Build := 'AVX2 optimised'
  else
    Build := 'no-arch (this CPU does not support AVX2)';
  Result := Result + 'Build:' + NewLine + Space + Build + NewLine + NewLine;
  if MemoDirInfo <> '' then
    Result := Result + MemoDirInfo + NewLine + NewLine;
  if MemoComponentsInfo <> '' then
    Result := Result + MemoComponentsInfo + NewLine + NewLine;
  if MemoTasksInfo <> '' then
    Result := Result + MemoTasksInfo + NewLine + NewLine;
  Prereqs := '';
  if WantVc then
    Prereqs := Prereqs + Space + 'Microsoft Visual C++ runtime {#VcRedistVersion}' + NewLine;
  if WantMw then
    Prereqs := Prereqs + Space + 'Magewell capture runtime {#MwRuntimeVersion}' + NewLine;
  if Prereqs <> '' then
    Result := Result + 'Prerequisites to install:' + NewLine + Prereqs;
end;

{ --- install --- }

function RunPrereq(ProgressPage: TOutputProgressWizardPage; const Name, FileName, Params: String;
  var NeedsRestart: Boolean): String;
var
  Code: Integer;
begin
  Result := '';
  ProgressPage.SetText('Installing ' + Name + '...', '');
  Log('Installing ' + Name);
  ExtractTemporaryFile(FileName);
  if not Exec(ExpandConstant('{tmp}\') + FileName, Params, '', SW_SHOWNORMAL, ewWaitUntilTerminated, Code) then
    Result := 'Unable to run the ' + Name + ' installer: ' + SysErrorMessage(Code)
  else
  begin
    Log(Format('%s installer exited with %d', [Name, Code]));
    { 3010 = success, reboot required; 1638 = a newer version is already installed }
    if Code = 3010 then
      NeedsRestart := True
    else if (Code <> 0) and (Code <> 1638) then
      Result := Format('The %s installer failed (exit code %d). Install it yourself, ' +
        'or go back and untick it, then try again.', [Name, Code]);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ProgressPage: TOutputProgressWizardPage;
begin
  Result := '';
  if not (WantVc or WantMw) then
    Exit;
  ProgressPage := CreateOutputProgressPage('Installing prerequisites', 'Please wait while setup installs the software ezcapture depends on.');
  ProgressPage.Show;
  try
    if WantVc then
      Result := RunPrereq(ProgressPage, 'Microsoft Visual C++ runtime', 'vc_redist.x64.exe', '/install /quiet /norestart', NeedsRestart);
    if (Result = '') and WantMw then
      Result := RunPrereq(ProgressPage, 'Magewell capture runtime', 'MWCaptureRT.exe', '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-', NeedsRestart);
  finally
    ProgressPage.Hide;
  end;
end;

function FilterSelected(const FileName: String): Boolean;
begin
  if Pos('mwcapture', FileName) = 1 then
    Result := WizardIsComponentSelected('mw')
  else
    Result := WizardIsComponentSelected('bm');
  if Pos('-trace', FileName) > 0 then
    Result := Result and WizardIsTaskSelected('trace');
end;

function RegSvr(const Params, Path: String): Integer;
var
  Code: Integer;
begin
  if Exec(ExpandConstant('{sys}\regsvr32.exe'), Params + ' "' + Path + '"', '', SW_HIDE, ewWaitUntilTerminated, Code) then
    Result := Code
  else
    Result := -1;
  Log(Format('regsvr32 %s "%s" exited with %d', [Params, Path, Result]));
end;

function Filters: TArrayOfString;
begin
  SetArrayLength(Result, 4);
  Result[0] := 'mwcapture.ax';
  Result[1] := 'mwcapture-trace.ax';
  Result[2] := 'bmcapture.ax';
  Result[3] := 'bmcapture-trace.ax';
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Names: TArrayOfString;
  I: Integer;
  Path, Failed, Build: String;
begin
  Names := Filters;
  if CurStep = ssInstall then
  begin
    { unregister whatever an earlier install left, and remove filters that are no longer wanted }
    for I := 0 to GetArrayLength(Names) - 1 do
    begin
      Path := ExpandConstant('{app}\') + Names[I];
      if FileExists(Path) then
      begin
        RegSvr('/u /s', Path);
        if not FilterSelected(Names[I]) then
          if not DeleteFile(Path) then
            Log('Unable to delete ' + Path);
      end;
    end;
  end
  else if CurStep = ssPostInstall then
  begin
    Failed := '';
    for I := 0 to GetArrayLength(Names) - 1 do
    begin
      Path := ExpandConstant('{app}\') + Names[I];
      if FilterSelected(Names[I]) and FileExists(Path) then
        if RegSvr('/s', Path) <> 0 then
          Failed := Failed + '  ' + Names[I] + #13#10;
    end;
    if UseAvx2 then
      Build := 'AVX2'
    else
      Build := 'NoArch';
    SaveStringToFile(ExpandConstant('{app}\version.txt'), 'ezcapture {#AppVersion}' + #13#10 + 'Build: ' + Build + #13#10, False);
    if Failed <> '' then
      SuppressibleMsgBox('These filters were installed but could not be registered:' + #13#10#13#10 + Failed + #13#10 +
        'This usually means a prerequisite is missing (the Visual C++ runtime, or the Magewell runtime for mwcapture). ' +
        'Install it, then run this installer again.', mbError, MB_OK, IDOK);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Names: TArrayOfString;
  I: Integer;
  Path: String;
begin
  if CurUninstallStep = usUninstall then
  begin
    Names := Filters;
    for I := 0 to GetArrayLength(Names) - 1 do
    begin
      Path := ExpandConstant('{app}\') + Names[I];
      if FileExists(Path) then
        RegSvr('/u /s', Path);
    end;
  end;
end;
