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
Name: "trace"; Description: "Install the trace builds"; Components: mw bm; Flags: unchecked
Name: "firewall"; Description: "Allow the web API and Prometheus metrics through Windows Firewall (private networks only)"; Components: mw bm

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
; the web API and Prometheus firewall rules are always deleted first, so running setup again neither duplicates them
; nor keeps one that is no longer wanted, then added back for the filters installed. netsh failing to delete a rule
; that does not exist is expected, Inno Setup ignores the exit code.
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""ezcapture Magewell web API"""; StatusMsg: "Updating firewall rules..."; Flags: runhidden
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""ezcapture DeckLink web API"""; StatusMsg: "Updating firewall rules..."; Flags: runhidden
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""ezcapture Magewell Prometheus metrics"""; StatusMsg: "Updating firewall rules..."; Flags: runhidden
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""ezcapture DeckLink Prometheus metrics"""; StatusMsg: "Updating firewall rules..."; Flags: runhidden
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""ezcapture Magewell web API"" dir=in action=allow protocol=TCP localport=9474 profile=private remoteip=localsubnet"; StatusMsg: "Updating firewall rules..."; Flags: runhidden; Tasks: firewall; Components: mw
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""ezcapture DeckLink web API"" dir=in action=allow protocol=TCP localport=9475 profile=private remoteip=localsubnet"; StatusMsg: "Updating firewall rules..."; Flags: runhidden; Tasks: firewall; Components: bm
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""ezcapture Magewell Prometheus metrics"" dir=in action=allow protocol=TCP localport=9464 profile=private remoteip=localsubnet"; StatusMsg: "Updating firewall rules..."; Flags: runhidden; Tasks: firewall; Components: mw
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""ezcapture DeckLink Prometheus metrics"" dir=in action=allow protocol=TCP localport=9465 profile=private remoteip=localsubnet"; StatusMsg: "Updating firewall rules..."; Flags: runhidden; Tasks: firewall; Components: bm
Filename: "https://www.blackmagicdesign.com/support/family/capture-and-playback"; Description: "Open the Blackmagic Desktop Video download page"; Flags: postinstall shellexec skipifsilent nowait; Check: NeedDesktopVideo
Filename: "https://github.com/Nevcairiel/LAVFilters/releases"; Description: "Open the LAV Filters download page"; Flags: postinstall shellexec skipifsilent nowait unchecked; Check: NeedLav
Filename: "{#AppUrl}#configuration"; Description: "Open the player setup guide"; Flags: postinstall shellexec skipifsilent nowait unchecked

[UninstallRun]
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""ezcapture Magewell web API"""; Flags: runhidden; RunOnceId: "DeleteMagewellFirewallRule"
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""ezcapture DeckLink web API"""; Flags: runhidden; RunOnceId: "DeleteDeckLinkFirewallRule"
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""ezcapture Magewell Prometheus metrics"""; Flags: runhidden; RunOnceId: "DeleteMagewellMetricsFirewallRule"
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""ezcapture DeckLink Prometheus metrics"""; Flags: runhidden; RunOnceId: "DeleteDeckLinkMetricsFirewallRule"

[Messages]
SelectTasksDesc=Do you want the trace builds or the firewall rules?

[CustomMessages]
TraceExplain=The trace builds are a second copy of each filter that writes a detailed log to your Windows temp folder. They are registered under their own names, e.g. "Magewell Pro Capture (Trace)", so in your player you choose which one to use. Use them when diagnosing a problem or when asked for a log in a bug report.%n%nThe firewall rules let Home Assistant, Prometheus or a browser on another PC reach the filter's web API and metrics endpoint, both of which stay off until you enable them on the property page. They open TCP ports 9474 and 9464 (Magewell), 9475 and 9465 (Blackmagic) on private networks, to the local subnet only.%n%nYou can change either later by running this installer again.

[Code]
const
  PF_AVX2_INSTRUCTIONS_AVAILABLE = 40;
  VcRuntimeKey = 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';
  DeckLinkIteratorClsid = 'CLSID\{BA6C6F44-6DA5-4DCE-94AA-EE2D1372A676}';
  LavAudioClsid = 'CLSID\{E8E73B6B-4CB3-44A4-BE99-4F7BCB96E491}';
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#AppGuid}_is1';
  { where each filter keeps its settings, under HKCU (see capture_filter.cpp) }
  MwSettingsKey = 'Software\3ll3d00d\MWCapture';
  BmSettingsKey = 'Software\3ll3d00d\BMCapture';
  AllAddresses = '0.0.0.0';
  LoopbackAddress = '127.0.0.1';

  { rows of the prerequisites table }
  RowVc = 0;
  RowMw = 1;
  RowMwDriver = 2;
  RowDv = 3;
  RowLav = 4;
  RowLast = 4;

var
  PrereqPage: TWizardPage;
  HeaderName, HeaderInstalled, HeaderTodo: TNewStaticText;
  RowName, RowInstalled, RowTodo: array[0..4] of TNewStaticText;
  VcCheck, MwCheck: TNewCheckBox;
  ManualLabel: TNewStaticText;
  ManualMemo: TNewMemo;
  DeclinedVc, DeclinedMw, RefreshingPrereqs: Boolean;
  PreviousVersion: String;
  RemotePage: TWizardPage;
  WebCheck, MetricsCheck: TNewCheckBox;
  BindCombo: TNewComboBox;
  TokenLabel, TokenNote: TNewStaticText;
  TokenEdit: TNewEdit;
  RemoteLoaded: Boolean;

function IsProcessorFeaturePresent(ProcessorFeature: DWORD): Boolean;
  external 'IsProcessorFeaturePresent@kernel32.dll stdcall';

{ fills Value with random bytes from the OS, the same source the filter uses for its tokens }
function RtlGenRandom(var Value: Cardinal; Len: Cardinal): Boolean;
  external 'SystemFunction036@advapi32.dll stdcall';

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

{ the file version of the DLL registered for a COM class, or 'installed' if it cannot be read }
function ComServerVersion(const Clsid: String): String;
var
  Path: String;
begin
  if not (RegQueryStringValue(HKCR64, Clsid + '\InprocServer32', '', Path)
    and GetVersionNumbersString(ExpandConstant(Path), Result)) then
    Result := 'installed';
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

{ the prerequisites page is a table of dependency, installed version and what needs doing, with a tick box in the
  last column for the ones setup can install, followed by the details of anything the user has to do themselves }

function ColInstalled: Integer;
begin
  Result := PrereqPage.SurfaceWidth * 42 div 100;
end;

function ColTodo: Integer;
begin
  Result := PrereqPage.SurfaceWidth * 66 div 100;
end;

function NewCell(Left: Integer; Bold: Boolean): TNewStaticText;
begin
  Result := TNewStaticText.Create(PrereqPage);
  Result.Parent := PrereqPage.Surface;
  Result.Left := Left;
  if Bold then
    Result.Font.Style := [fsBold];
end;

function NewInstallCheck: TNewCheckBox;
begin
  Result := TNewCheckBox.Create(PrereqPage);
  Result.Parent := PrereqPage.Surface;
  Result.Left := ColTodo;
  Result.Width := PrereqPage.SurfaceWidth - ColTodo;
end;

procedure CreatePrereqPage;
var
  I: Integer;
begin
  PrereqPage := CreateCustomPage(wpSelectTasks, 'Prerequisites',
    'Other software the ezcapture filters depend on.');

  HeaderName := NewCell(0, True);
  HeaderName.Caption := 'Dependency';
  HeaderInstalled := NewCell(ColInstalled, True);
  HeaderInstalled.Caption := 'Installed';
  HeaderTodo := NewCell(ColTodo, True);
  HeaderTodo.Caption := 'To do';
  for I := 0 to RowLast do
  begin
    RowName[I] := NewCell(0, False);
    RowInstalled[I] := NewCell(ColInstalled, False);
    RowTodo[I] := NewCell(ColTodo, False);
  end;
  RowName[RowVc].Caption := 'Visual C++ runtime';
  RowName[RowMw].Caption := 'Magewell capture runtime';
  RowName[RowMwDriver].Caption := 'Magewell driver';
  RowName[RowDv].Caption := 'Blackmagic Desktop Video';
  RowName[RowLav].Caption := 'LAV Filters';
  VcCheck := NewInstallCheck;
  VcCheck.Caption := 'Install {#VcRedistVersion}';
  MwCheck := NewInstallCheck;
  MwCheck.Caption := 'Install {#MwRuntimeVersion}';

  ManualLabel := TNewStaticText.Create(PrereqPage);
  ManualLabel.Parent := PrereqPage.Surface;
  ManualLabel.Caption := 'Things you need to check or do yourself:';
  ManualLabel.Width := PrereqPage.SurfaceWidth;

  ManualMemo := TNewMemo.Create(PrereqPage);
  ManualMemo.Parent := PrereqPage.Surface;
  ManualMemo.Width := PrereqPage.SurfaceWidth;
  ManualMemo.ReadOnly := True;
  ManualMemo.ScrollBars := ssVertical;
  ManualMemo.WordWrap := True;
end;

{ lays out one row at Top, Check replaces the To do text when it is not nil, and moves Top to the next row }
procedure ShowRow(Row: Integer; Shown: Boolean; const Installed, Todo: String; Check: TNewCheckBox; var Top: Integer);
begin
  RowName[Row].Visible := Shown;
  RowInstalled[Row].Visible := Shown;
  RowTodo[Row].Visible := Shown and (Check = nil);
  if not Shown then
    Exit;
  RowName[Row].Top := Top + ScaleY(2);
  RowInstalled[Row].Top := Top + ScaleY(2);
  RowTodo[Row].Top := Top + ScaleY(2);
  RowInstalled[Row].Caption := Installed;
  RowTodo[Row].Caption := Todo;
  if Check <> nil then
  begin
    Check.Visible := True;
    Check.Top := Top;
  end;
  Top := Top + ScaleY(20);
end;

procedure RefreshPrereqPage;
var
  V: Int64;
  S: String;
  Top: Integer;
  Mw, Bm: Boolean;
begin
  RefreshingPrereqs := True;
  Mw := WizardIsComponentSelected('mw');
  Bm := WizardIsComponentSelected('bm');
  Top := HeaderName.Top + HeaderName.Height + ScaleY(6);
  VcCheck.Visible := False;
  MwCheck.Visible := False;

  if InstalledVcVersion(V) then
    S := VersionString(V)
  else
    S := 'not installed';
  VcCheck.Checked := not DeclinedVc;
  if NeedVc then
    ShowRow(RowVc, True, S, '', VcCheck, Top)
  else
    ShowRow(RowVc, True, S, 'nothing', nil, Top);

  if GetPackedVersion(MwRuntimeDll, V) then
    S := VersionString(V)
  else
    S := 'not installed';
  MwCheck.Checked := not DeclinedMw;
  if NeedMw then
    ShowRow(RowMw, Mw, S, '', MwCheck, Top)
  else
    ShowRow(RowMw, Mw, S, 'nothing', nil, Top);

  ShowRow(RowMwDriver, Mw, 'not checked', 'check yourself', nil, Top);

  if HasDesktopVideo then
    ShowRow(RowDv, Bm, ComServerVersion(DeckLinkIteratorClsid), 'nothing', nil, Top)
  else
    ShowRow(RowDv, Bm, 'not installed', 'install yourself', nil, Top);

  if HasLav then
    ShowRow(RowLav, True, ComServerVersion(LavAudioClsid), 'nothing', nil, Top)
  else
    ShowRow(RowLav, True, 'not registered', 'depends on player', nil, Top);

  ManualLabel.Top := Top + ScaleY(10);
  ManualMemo.Top := ManualLabel.Top + ManualLabel.Height + ScaleY(4);
  ManualMemo.Height := PrereqPage.SurfaceHeight - ManualMemo.Top;
  ManualMemo.Text := ManualSteps;
  if ManualMemo.Text = '' then
    ManualMemo.Text := 'Nothing.';
  RefreshingPrereqs := False;
end;

procedure PrereqCheckClick(Sender: TObject);
begin
  { setting Checked while refreshing fires this too }
  if RefreshingPrereqs then
    Exit;
  if VcCheck.Visible then
    DeclinedVc := not VcCheck.Checked;
  if MwCheck.Visible then
    DeclinedMw := not MwCheck.Checked;
  { show the consequences of unticking straight away }
  ManualMemo.Text := ManualSteps;
  if ManualMemo.Text = '' then
    ManualMemo.Text := 'Nothing.';
end;

{ --- remote access: turns on the web API and Prometheus metrics, and makes the web API token --- }

{ a token in the filter's format, 128 random bits as 32 lower case hex digits, empty if no random numbers are available }
function NewToken: String;
var
  I, J: Integer;
  V: Cardinal;
begin
  Result := '';
  for I := 1 to 4 do
  begin
    if not RtlGenRandom(V, 4) then
    begin
      Result := '';
      Exit;
    end;
    { Pascal Script has no IntToHex, so emit the 8 hex digits of V by hand, most significant first }
    for J := 7 downto 0 do
      Result := Result + Copy('0123456789abcdef', ((V shr (J * 4)) and 15) + 1, 1);
  end;
end;

{ the settings key of the filter whose current settings the page starts from }
function PrimarySettingsKey: String;
begin
  if WizardIsComponentSelected('mw') then
    Result := MwSettingsKey
  else
    Result := BmSettingsKey;
end;

function RegFlag(const Key, Name: String): Boolean;
var
  V: Cardinal;
begin
  Result := RegQueryDWordValue(HKCU, Key, Name, V) and (V = 1);
end;

function RegString(const Key, Name: String): String;
begin
  if not RegQueryStringValue(HKCU, Key, Name, Result) then
    Result := '';
end;

{ the IPv4 addresses of this PC's network adapters, excluding loopback and link local ones }
function LocalAddresses: TArrayOfString;
var
  OutFile: String;
  Code: Integer;
begin
  SetArrayLength(Result, 0);
  OutFile := ExpandConstant('{tmp}\addresses.txt');
  if Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    '-NoProfile -NonInteractive -Command "Get-NetIPAddress -AddressFamily IPv4 -AddressState Preferred | ' +
    'Where-Object { $_.IPAddress -ne ''' + LoopbackAddress + ''' -and $_.IPAddress -notlike ''169.254.*'' } | ' +
    'ForEach-Object { $_.IPAddress } | Set-Content -Encoding ascii -Path ''' + OutFile + '''"',
    '', SW_HIDE, ewWaitUntilTerminated, Code) and (Code = 0) then
    LoadStringsFromFile(OutFile, Result);
end;

{ the address part of a combo box entry, e.g. "0.0.0.0 (all network adapters)" gives 0.0.0.0 }
function BindAddress: String;
var
  P: Integer;
begin
  Result := Trim(BindCombo.Text);
  P := Pos(' ', Result);
  if P > 0 then
    Result := Copy(Result, 1, P - 1);
end;

procedure UpdateRemoteControls;
begin
  BindCombo.Enabled := WebCheck.Checked or MetricsCheck.Checked;
  TokenLabel.Enabled := WebCheck.Checked;
  TokenEdit.Enabled := WebCheck.Checked;
  TokenNote.Enabled := WebCheck.Checked;
end;

procedure RemoteCheckClick(Sender: TObject);
begin
  UpdateRemoteControls;
end;

procedure CreateRemotePage;
var
  Intro, BindLabel, BindHint: TNewStaticText;
  Top: Integer;
begin
  RemotePage := CreateCustomPage(wpSelectTasks, 'Remote access',
    'Turn on the web API and Prometheus metrics.');

  Intro := TNewStaticText.Create(RemotePage);
  Intro.Parent := RemotePage.Surface;
  Intro.AutoSize := False;
  Intro.WordWrap := True;
  Intro.Width := RemotePage.SurfaceWidth;
  Intro.Height := ScaleY(42);
  Intro.Caption := 'The web API serves a status page for a browser and the Home Assistant integration, the metrics ' +
    'endpoint is for Prometheus. These settings are saved for the Windows user running setup, and you can change ' +
    'them later on the filter''s property page.';

  Top := Intro.Top + Intro.Height + ScaleY(6);
  WebCheck := TNewCheckBox.Create(RemotePage);
  WebCheck.Parent := RemotePage.Surface;
  WebCheck.Top := Top;
  WebCheck.Width := RemotePage.SurfaceWidth;
  WebCheck.Caption := 'Turn on the web API (status page and Home Assistant)';
  WebCheck.OnClick := @RemoteCheckClick;

  MetricsCheck := TNewCheckBox.Create(RemotePage);
  MetricsCheck.Parent := RemotePage.Surface;
  MetricsCheck.Top := WebCheck.Top + ScaleY(22);
  MetricsCheck.Width := RemotePage.SurfaceWidth;
  MetricsCheck.Caption := 'Turn on the Prometheus metrics endpoint';
  MetricsCheck.OnClick := @RemoteCheckClick;

  BindLabel := TNewStaticText.Create(RemotePage);
  BindLabel.Parent := RemotePage.Surface;
  BindLabel.Top := MetricsCheck.Top + ScaleY(30);
  BindLabel.Caption := 'Listen on:';

  BindCombo := TNewComboBox.Create(RemotePage);
  BindCombo.Parent := RemotePage.Surface;
  BindCombo.Left := ScaleX(70);
  BindCombo.Top := BindLabel.Top - ScaleY(3);
  BindCombo.Width := RemotePage.SurfaceWidth - BindCombo.Left;
  BindCombo.Style := csDropDown;

  BindHint := TNewStaticText.Create(RemotePage);
  BindHint.Parent := RemotePage.Surface;
  BindHint.AutoSize := False;
  BindHint.WordWrap := True;
  BindHint.Top := BindCombo.Top + BindCombo.Height + ScaleY(4);
  BindHint.Width := RemotePage.SurfaceWidth;
  BindHint.Height := ScaleY(28);
  BindHint.Caption := 'Choose all network adapters or one of this PC''s addresses so Home Assistant or Prometheus ' +
    'on another PC can connect, or 127.0.0.1 to keep them to this PC.';

  TokenLabel := TNewStaticText.Create(RemotePage);
  TokenLabel.Parent := RemotePage.Surface;
  TokenLabel.Top := BindHint.Top + BindHint.Height + ScaleY(10);
  TokenLabel.Caption := 'Web API access token (enter it in Home Assistant):';

  TokenEdit := TNewEdit.Create(RemotePage);
  TokenEdit.Parent := RemotePage.Surface;
  TokenEdit.Top := TokenLabel.Top + TokenLabel.Height + ScaleY(4);
  TokenEdit.Width := RemotePage.SurfaceWidth;
  TokenEdit.ReadOnly := True;

  TokenNote := TNewStaticText.Create(RemotePage);
  TokenNote.Parent := RemotePage.Surface;
  TokenNote.AutoSize := False;
  TokenNote.WordWrap := True;
  TokenNote.Top := TokenEdit.Top + TokenEdit.Height + ScaleY(4);
  TokenNote.Width := RemotePage.SurfaceWidth;
  TokenNote.Height := ScaleY(28);
end;

{ starts the page from the current settings, the first time it is shown }
procedure LoadRemotePage;
var
  Key, Address: String;
  Addresses: TArrayOfString;
  I, Selected: Integer;
begin
  if RemoteLoaded then
    Exit;
  RemoteLoaded := True;
  Key := PrimarySettingsKey;
  WebCheck.Checked := RegFlag(Key, 'webEnabled');
  MetricsCheck.Checked := RegFlag(Key, 'prometheusEnabled');

  { the address in use, or all adapters as the point of turning these on is usually another PC }
  Address := RegString(Key, 'webBindAddress');
  if Address = '' then
    Address := RegString(Key, 'prometheusBindAddress');
  if Address = '' then
    Address := AllAddresses;

  BindCombo.Items.Add(AllAddresses + ' (all network adapters)');
  Addresses := LocalAddresses;
  for I := 0 to GetArrayLength(Addresses) - 1 do
    if Trim(Addresses[I]) <> '' then
      BindCombo.Items.Add(Trim(Addresses[I]));
  BindCombo.Items.Add(LoopbackAddress + ' (this PC only)');
  Selected := -1;
  for I := 0 to BindCombo.Items.Count - 1 do
  begin
    BindCombo.ItemIndex := I;
    if BindAddress = Address then
      Selected := I;
  end;
  BindCombo.ItemIndex := Selected;
  if Selected < 0 then
    BindCombo.Text := Address;

  TokenEdit.Text := RegString(Key, 'webToken');
  if TokenEdit.Text <> '' then
    TokenNote.Caption := 'This is the token already in use, setup keeps it. Make a new one on the property page if you need to.'
  else
  begin
    TokenEdit.Text := NewToken;
    if TokenEdit.Text <> '' then
      TokenNote.Caption := 'A new token, saved when setup finishes. Copy it from here or later from the property page.'
    else
      TokenNote.Caption := 'Setup could not make a token, the filter makes one when the web API is turned on.';
  end;
  UpdateRemoteControls;
end;

{ saves the page to one filter's settings }
procedure SaveRemoteSettings(const Key: String);
var
  Address: String;
begin
  Address := BindAddress;
  if WebCheck.Checked then
  begin
    RegWriteDWordValue(HKCU, Key, 'webEnabled', 1);
    RegWriteStringValue(HKCU, Key, 'webBindAddress', Address);
    { keep the token a filter already has, anything using it would stop working }
    if (RegString(Key, 'webToken') = '') and (TokenEdit.Text <> '') then
      RegWriteStringValue(HKCU, Key, 'webToken', TokenEdit.Text);
  end
  else
    RegWriteDWordValue(HKCU, Key, 'webEnabled', 0);
  if MetricsCheck.Checked then
  begin
    RegWriteDWordValue(HKCU, Key, 'prometheusEnabled', 1);
    RegWriteStringValue(HKCU, Key, 'prometheusBindAddress', Address);
  end
  else
    RegWriteDWordValue(HKCU, Key, 'prometheusEnabled', 0);
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

  { make room for the trace build and firewall explanation above the task list }
  Gap := WizardForm.TasksList.Top - WizardForm.SelectTasksLabel.Top - WizardForm.SelectTasksLabel.Height;
  WizardForm.SelectTasksLabel.Caption := CustomMessage('TraceExplain');
  WizardForm.SelectTasksLabel.WordWrap := True;
  WizardForm.SelectTasksLabel.Height := ScaleY(165);
  WizardForm.TasksList.Top := WizardForm.SelectTasksLabel.Top + WizardForm.SelectTasksLabel.Height + Gap;
  WizardForm.TasksList.Height := WizardForm.SelectTasksPage.Height - WizardForm.TasksList.Top;

  { both go straight after the tasks page, so the one created last comes first }
  CreatePrereqPage;
  VcCheck.OnClick := @PrereqCheckClick;
  MwCheck.OnClick := @PrereqCheckClick;
  CreateRemotePage;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = PrereqPage.ID then
    RefreshPrereqPage
  else if CurPageID = RemotePage.ID then
    LoadRemotePage;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpSelectComponents) and not WizardIsComponentSelected('mw') and not WizardIsComponentSelected('bm') then
  begin
    MsgBox('Select at least one filter to install.', mbError, MB_OK);
    Result := False;
  end;
  if (CurPageID = RemotePage.ID) and (WebCheck.Checked or MetricsCheck.Checked) and (BindAddress = '') then
  begin
    MsgBox('Choose the address to listen on.', mbError, MB_OK);
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
  if RemoteLoaded then
  begin
    Result := Result + 'Remote access:' + NewLine;
    if WebCheck.Checked then
      Result := Result + Space + 'Web API on, listening on ' + BindAddress + NewLine
    else
      Result := Result + Space + 'Web API off' + NewLine;
    if MetricsCheck.Checked then
      Result := Result + Space + 'Prometheus metrics on, listening on ' + BindAddress + NewLine
    else
      Result := Result + Space + 'Prometheus metrics off' + NewLine;
    Result := Result + NewLine;
  end;
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
    { only when the page was shown, a silent install leaves the settings alone }
    if RemoteLoaded then
    begin
      if WizardIsComponentSelected('mw') then
        SaveRemoteSettings(MwSettingsKey);
      if WizardIsComponentSelected('bm') then
        SaveRemoteSettings(BmSettingsKey);
    end;
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
