<#
.SYNOPSIS
Builds the ezcapture installer with Inno Setup 7.

.DESCRIPTION
Needs the mwcapture.ax and bmcapture.ax builds from the Release, ReleaseNoArch, ReleaseTrace and
ReleaseTraceNoArch configurations. By default they are read from x64\<config>; pass -StageDir to
read them from <StageDir>\msbuild-<config> instead (the layout CI artifacts download into).

Downloads the latest VC++ x64 redistributable into installer\deps if it is not already there.

.EXAMPLE
./installer/build-installer.ps1 -Version 1.0.0 -Iscc 'C:\Program Files\Inno Setup 7\ISCC.exe'
#>
param(
    [string]$Version = '0.0.0-dev',
    [string]$StageDir,
    [string]$Iscc
)
$ErrorActionPreference = 'Stop'

$here = $PSScriptRoot
$root = Split-Path $here -Parent

if (-not $Iscc) {
    $found = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    $candidates = @(
        $(if ($found) { $found.Source }),
        "$env:ProgramFiles\Inno Setup 7\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 7\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 7\ISCC.exe"
    ) | Where-Object { $_ -and (Test-Path $_) }
    if (-not $candidates) { throw 'ISCC.exe (Inno Setup 7) not found, pass -Iscc' }
    $Iscc = @($candidates)[0]
}

# x.y.z[-suffix] -> x.y.z.0 for the Windows file version resource
$numeric = if ($Version -match '^(\d+)\.(\d+)\.(\d+)') { "$($Matches[1]).$($Matches[2]).$($Matches[3]).0" } else { '0.0.0.0' }

$sources = @{}
foreach ($cfg in 'Release', 'ReleaseNoArch', 'ReleaseTrace', 'ReleaseTraceNoArch') {
    $dir = if ($StageDir) { Join-Path $StageDir "msbuild-$cfg" } else { Join-Path $root "x64\$cfg" }
    $dir = (Resolve-Path $dir).Path
    foreach ($ax in 'mwcapture.ax', 'bmcapture.ax') {
        if (-not (Test-Path (Join-Path $dir $ax))) { throw "missing $ax in $dir" }
    }
    $sources[$cfg] = $dir
}

$deps = Join-Path $here 'deps'
$redist = Join-Path $deps 'vc_redist.x64.exe'
if (-not (Test-Path $redist)) {
    New-Item -ItemType Directory -Force $deps | Out-Null
    Write-Host 'Downloading the VC++ x64 redistributable'
    Invoke-WebRequest -Uri 'https://aka.ms/vc14/vc_redist.x64.exe' -OutFile $redist
}
$sig = Get-AuthenticodeSignature $redist
if ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
    throw "vc_redist.x64.exe signature check failed: $($sig.Status) $($sig.SignerCertificate.Subject)"
}
Write-Host "VC++ redistributable $((Get-Item $redist).VersionInfo.FileVersion)"

& $Iscc `
    "--define=AppVersion=$Version" `
    "--define=AppVersionNumeric=$numeric" `
    "--define=SrcRelease=$($sources['Release'])" `
    "--define=SrcReleaseNoArch=$($sources['ReleaseNoArch'])" `
    "--define=SrcReleaseTrace=$($sources['ReleaseTrace'])" `
    "--define=SrcReleaseTraceNoArch=$($sources['ReleaseTraceNoArch'])" `
    (Join-Path $here 'ezcapture.iss')
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }

$out = Join-Path $here "output\ezcapture-$Version-setup.exe"
if (-not (Test-Path $out)) { throw "expected $out" }
Write-Host "Built $out"
if ($env:GITHUB_OUTPUT) { "installer_path=$out" | Out-File -Append -Encoding utf8 $env:GITHUB_OUTPUT }
