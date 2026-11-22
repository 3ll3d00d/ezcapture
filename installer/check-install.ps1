<#
.SYNOPSIS
Reports what the ezcapture installer has left on this PC. Read-only, does not need admin.
#>
$ErrorActionPreference = 'SilentlyContinue'

function Show($label, $value) { '{0,-28} {1}' -f $label, $value }

'--- ezcapture'
$uninst = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{EBA4B682-9E08-45B1-9BFE-A9CBBF617942}_is1'
Show 'Installed apps entry' $(if ($uninst) { "$($uninst.DisplayName) (DisplayVersion $($uninst.DisplayVersion))" } else { 'none' })
$reg = Get-ItemProperty 'HKLM:\SOFTWARE\ezcapture'
Show 'HKLM\Software\ezcapture' $(if ($reg) { "Version=$($reg.Version) Build=$($reg.Build) InstallDir=$($reg.InstallDir)" } else { 'none' })
$dir = if ($reg.InstallDir) { $reg.InstallDir } else { "$env:ProgramFiles\ezcapture" }
Show 'Install dir' $(if (Test-Path $dir) { $dir } else { "$dir (absent)" })
if (Test-Path $dir) {
    Get-ChildItem $dir -Recurse -File | ForEach-Object { Show '  file' $_.FullName.Substring($dir.Length + 1) }
    if (Test-Path "$dir\version.txt") { Get-Content "$dir\version.txt" | ForEach-Object { Show '  version.txt' $_ } }
}

'--- filter registrations (HKCR\CLSID\...\InprocServer32)'
$filters = [ordered]@{
    'Magewell Pro Capture'         = '4E3B0A92-2476-4016-81F0-201F19F6FBAE'
    'Magewell Pro Capture (Trace)' = '9E53337D-9E32-40B4-AD39-B8525CDECD45'
    'Blackmagic Capture'           = '64116B3A-1E04-4CA7-BCFE-F42A0CE7BF16'
    'Blackmagic Capture (Trace)'   = '1BCB1E63-505B-4E66-ABEB-95913C7B081D'
}
foreach ($name in $filters.Keys) {
    $k = "Registry::HKEY_CLASSES_ROOT\CLSID\{$($filters[$name])}\InprocServer32"
    $cat = "Registry::HKEY_CLASSES_ROOT\CLSID\{860BB310-5D01-11D0-BD3B-00A0C911CE86}\Instance\{$($filters[$name])}"
    $path = if (Test-Path $k) { (Get-ItemProperty $k).'(default)' } else { 'not registered' }
    $inCat = if (Test-Path $cat) { 'listed as video capture source' } else { 'not in video capture category' }
    Show $name "$path; $inCat"
}

'--- web API and Prometheus firewall rules'
foreach ($rule in @(
        @{ Name = 'ezcapture Magewell web API'; Port = '9474' },
        @{ Name = 'ezcapture DeckLink web API'; Port = '9475' },
        @{ Name = 'ezcapture Magewell Prometheus metrics'; Port = '9464' },
        @{ Name = 'ezcapture DeckLink Prometheus metrics'; Port = '9465' })) {
    $r = Get-NetFirewallRule -DisplayName $rule.Name
    if ($r) {
        $port = ($r | Get-NetFirewallPortFilter).LocalPort
        $remote = ($r | Get-NetFirewallAddressFilter).RemoteAddress
        Show $rule.Name "enabled=$($r.Enabled) action=$($r.Action) profile=$($r.Profile) port=$port remote=$remote (expect port $($rule.Port), Private, LocalSubnet)"
    } else {
        Show $rule.Name 'absent'
    }
}
# dismissing the Windows Firewall prompt for a player creates a block rule for its exe, which beats the port rule
$players = 'mpc-be', 'mpc-hc', 'Media Center'
$blocks = Get-NetFirewallRule -Direction Inbound -Action Block -Enabled True | Where-Object {
    $app = ($_ | Get-NetFirewallApplicationFilter).Program
    $app -and ($players | Where-Object { $app -like "*$_*" })
}
if ($blocks) {
    foreach ($b in $blocks) { Show 'Player block rule' "$($b.DisplayName): $(($b | Get-NetFirewallApplicationFilter).Program) (blocks the web API, delete it in Windows Defender Firewall)" }
} else {
    Show 'Player block rules' 'none'
}

'--- remote access settings (HKCU, the user running this script)'
foreach ($f in @(@{ Name = 'Magewell'; Key = 'MWCapture' }, @{ Name = 'Blackmagic'; Key = 'BMCapture' })) {
    $s = Get-ItemProperty "HKCU:\Software\3ll3d00d\$($f.Key)"
    if ($s) {
        $token = if ($s.webToken) { "set ($($s.webToken.Length) chars)" } else { 'none' }
        Show $f.Name "web=$($s.webEnabled) webAddress=$($s.webBindAddress) token=$token prometheus=$($s.prometheusEnabled) prometheusAddress=$($s.prometheusBindAddress)"
    } else {
        Show $f.Name 'no settings'
    }
}

'--- prerequisites'
$vc = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64'
Show 'VC++ x64 runtime' $(if ($vc.Installed -eq 1) { "$($vc.Major).$($vc.Minor).$($vc.Bld)" } else { 'not installed' })
$mwDriver = Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e96c-e325-11ce-bfc1-08002be10318}' |
    Get-ItemProperty | Where-Object { $_.ProviderName -like '*Magewell*' } | Select-Object -First 1
Show 'Magewell Pro Capture driver' $(if ($mwDriver) { "$($mwDriver.DriverVersion) ($($mwDriver.DriverDesc))" } elseif (Test-Path 'HKLM:\SYSTEM\CurrentControlSet\Services\ProCapture') { 'installed, no card bound' } else { 'not installed' })
$mw = Get-Item "$env:windir\System32\LibMWCapture.dll"
Show 'Magewell runtime' $(if ($mw) { $mw.VersionInfo.FileVersion } else { 'not installed' })
Show 'Blackmagic Desktop Video' $(if (Test-Path 'Registry::HKEY_CLASSES_ROOT\CLSID\{BA6C6F44-6DA5-4DCE-94AA-EE2D1372A676}') { 'installed' } else { 'not installed' })
Show 'LAV Audio (system-wide)' $(if (Test-Path 'Registry::HKEY_CLASSES_ROOT\CLSID\{E8E73B6B-4CB3-44A4-BE99-4F7BCB96E491}') { 'registered' } else { 'not registered' })
Add-Type -Namespace W -Name K -MemberDefinition '[DllImport("kernel32.dll")] public static extern bool IsProcessorFeaturePresent(uint f);'
Show 'CPU AVX2 (expect Build=)' $(if ([W.K]::IsProcessorFeaturePresent(40)) { 'yes (AVX2)' } else { 'no (NoArch)' })
