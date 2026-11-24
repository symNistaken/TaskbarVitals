param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$SumsPath = '',
    [string]$Repo = 'symNistaken/TaskbarVitals'
)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must match MAJOR.MINOR.PATCH.' }

$root = Split-Path $PSScriptRoot -Parent
if ([string]::IsNullOrWhiteSpace($SumsPath)) {
    $SumsPath = Join-Path $root 'dist\SHA256SUMS.txt'
}
if (-not (Test-Path -LiteralPath $SumsPath)) {
    throw "SHA256SUMS.txt not found at: $SumsPath"
}

$sums = @{}
foreach ($line in Get-Content -LiteralPath $SumsPath -Encoding UTF8) {
    if ($line -match '^\s*([0-9a-fA-F]{64})\s+\*?(.+?)\s*$') {
        $sums[$Matches[2]] = $Matches[1].ToLowerInvariant()
    }
}

$setupName = "TaskbarVitals-Setup-$Version.exe"
$zipName = "TaskbarVitals-$Version-portable.zip"
if (-not $sums.ContainsKey($setupName)) { throw "Missing hash for $setupName in $SumsPath" }
if (-not $sums.ContainsKey($zipName)) { throw "Missing hash for $zipName in $SumsPath" }

$setupHashUpper = $sums[$setupName].ToUpperInvariant()
$zipHashLower = $sums[$zipName]
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

# 1. Update Scoop manifest
$scoopPath = Join-Path $root 'installer\packaging\scoop\taskbar-vitals.json'
$scoop = Get-Content -LiteralPath $scoopPath -Raw -Encoding UTF8 | ConvertFrom-Json
$scoop.version = $Version
$scoop.homepage = "https://github.com/$Repo"
$scoop.architecture.'64bit'.url = "https://github.com/$Repo/releases/download/v$Version/$zipName"
$scoop.architecture.'64bit'.hash = $zipHashLower
$scoop.autoupdate.architecture.'64bit'.url = "https://github.com/$Repo/releases/download/v`$version/TaskbarVitals-`$version-portable.zip"
$scoopJson = ($scoop | ConvertTo-Json -Depth 8).Replace("`r`n", "`n") + "`n"
[System.IO.File]::WriteAllText($scoopPath, $scoopJson, $utf8NoBom)

# 2. Write / update winget 1.6.0 manifests for $Version
$wingetDir = Join-Path $root "installer\packaging\winget\manifests\s\symNistaken\TaskbarVitals\$Version"
if (-not (Test-Path -LiteralPath $wingetDir)) {
    New-Item -ItemType Directory -Path $wingetDir -Force | Out-Null
}

$versionYaml = @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.version.1.6.0.schema.json

PackageIdentifier: symNistaken.TaskbarVitals
PackageVersion: $Version
DefaultLocale: en-US
ManifestType: version
ManifestVersion: 1.6.0
"@.Replace("`r`n", "`n") + "`n"

$installerYaml = @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.installer.1.6.0.schema.json

PackageIdentifier: symNistaken.TaskbarVitals
PackageVersion: $Version
Platform:
- Windows.Desktop
MinimumOSVersion: 10.0.17763.0
InstallerType: inno
Scope: user
InstallModes:
- interactive
- silent
- silentWithProgress
UpgradeBehavior: install
AppsAndFeaturesEntries:
- DisplayName: Taskbar Vitals
  Publisher: symNistaken
  ProductCode: '{7E0B5C2A-3F4D-4C8E-9B61-2D5A8F1C4E93}_is1'
  InstallerType: inno
Installers:
- Architecture: x64
  InstallerUrl: https://github.com/$Repo/releases/download/v$Version/$setupName
  InstallerSha256: $setupHashUpper
ManifestType: installer
ManifestVersion: 1.6.0
"@.Replace("`r`n", "`n") + "`n"

$localeYaml = @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.defaultLocale.1.6.0.schema.json

PackageIdentifier: symNistaken.TaskbarVitals
PackageVersion: $Version
PackageLocale: en-US
Publisher: symNistaken
PublisherUrl: https://github.com/$Repo
PublisherSupportUrl: https://github.com/$Repo/issues
PackageName: Taskbar Vitals
PackageUrl: https://github.com/$Repo
License: GPL-3.0-or-later
LicenseUrl: https://github.com/$Repo/blob/main/LICENSE
ShortDescription: Wireless peripheral batteries and system vitals on the Windows taskbar.
Description: Taskbar Vitals displays live battery levels for wireless mice, keyboards, headsets, and controllers alongside optional CPU, GPU, RAM, laptop battery, and network telemetry directly on the Windows taskbar.
Tags:
- battery
- hardware-monitor
- peripherals
- taskbar
- telemetry
ManifestType: defaultLocale
ManifestVersion: 1.6.0
"@.Replace("`r`n", "`n") + "`n"

[System.IO.File]::WriteAllText((Join-Path $wingetDir 'symNistaken.TaskbarVitals.yaml'), $versionYaml, $utf8NoBom)
[System.IO.File]::WriteAllText((Join-Path $wingetDir 'symNistaken.TaskbarVitals.installer.yaml'), $installerYaml, $utf8NoBom)
[System.IO.File]::WriteAllText((Join-Path $wingetDir 'symNistaken.TaskbarVitals.locale.en-US.yaml'), $localeYaml, $utf8NoBom)

Write-Output "Updated packaging manifests for $Version (Setup: $setupHashUpper, Zip: $zipHashLower)"
