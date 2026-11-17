<#
.SYNOPSIS
Installs a pinned Inno Setup 7 into a fixed directory, used by CI to build the installer.
#>
param(
    [string]$Version = '7.1.1',
    [string]$Dir = 'C:\inno7'
)
$ErrorActionPreference = 'Stop'

if (Test-Path (Join-Path $Dir 'ISCC.exe')) { Write-Host "Inno Setup already present in $Dir"; return }

$tag = 'is-' + ($Version -replace '\.', '_')
$exe = Join-Path $env:RUNNER_TEMP "innosetup-$Version-x64.exe"
if (-not $env:RUNNER_TEMP) { $exe = Join-Path ([IO.Path]::GetTempPath()) "innosetup-$Version-x64.exe" }
Invoke-WebRequest -Uri "https://github.com/jrsoftware/issrc/releases/download/$tag/innosetup-$Version-x64.exe" -OutFile $exe

$sig = Get-AuthenticodeSignature $exe
if ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'O=Pyrsys B\.V\.') {
    throw "Inno Setup signature check failed: $($sig.Status) $($sig.SignerCertificate.Subject)"
}

$p = Start-Process $exe -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', "/DIR=$Dir" -Wait -PassThru
if ($p.ExitCode -ne 0) { throw "Inno Setup install failed with exit code $($p.ExitCode)" }
if (-not (Test-Path (Join-Path $Dir 'ISCC.exe'))) { throw "ISCC.exe not found in $Dir" }
Write-Host "Installed Inno Setup $Version to $Dir"
