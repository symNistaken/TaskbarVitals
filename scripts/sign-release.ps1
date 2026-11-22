# Signs a release's SHA256SUMS.txt with your release key, after CI has published the release:
#   powershell -ExecutionPolicy Bypass -File scripts\sign-release.ps1 -Version 1.2.3
# It downloads SHA256SUMS.txt from the release, shows it, signs it after you confirm, and writes
# SHA256SUMS.txt.sig. Upload that file to the release (the script opens the page). Until then the app
# shows the update but does not offer to install it.
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$KeyFile = (Join-Path $env:USERPROFILE '.wirelessbattery\release-key.wbk'),
    [string]$SumsFile,       # sign a local SHA256SUMS.txt instead of downloading the release's
    [string]$OutFile,        # default: SHA256SUMS.txt.sig in the current folder
    [Security.SecureString]$Password,  # for tests; normally asked for
    [switch]$Yes             # do not ask before signing
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'release-key.psm1') -Force
$repo = 'krisgfx9/Wireless-Device-Battery-Monitor'
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must be MAJOR.MINOR.PATCH, for example 1.2.3.' }
if (-not (Test-Path -LiteralPath $KeyFile)) { throw "No key file at $KeyFile. Create one with scripts\new-release-key.ps1." }
if (-not $OutFile) { $OutFile = Join-Path (Get-Location) 'SHA256SUMS.txt.sig' }

if (-not $SumsFile) {
    $SumsFile = Join-Path ([IO.Path]::GetTempPath()) "WirelessBattery-SHA256SUMS-$Version.txt"
    $url = "https://github.com/$repo/releases/download/v$Version/SHA256SUMS.txt"
    Write-Host "Downloading $url"
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest $url -OutFile $SumsFile -UseBasicParsing
}
$data = [IO.File]::ReadAllBytes($SumsFile)
$text = [Text.Encoding]::ASCII.GetString($data)
$setup = "BatteryHub-Setup-$Version.exe"
if ($text -notmatch "(?m)^[0-9a-fA-F]{64} [ *]$([regex]::Escape($setup))\r?$") { throw "SHA256SUMS.txt does not list $setup." }

# If this PC has the same files (for example a local build), they must match the list.
$root = Split-Path $PSScriptRoot -Parent
foreach ($line in $text -split "`n") {
    if ($line -match '^([0-9a-fA-F]{64}) [ *](.+?)\r?$') {
        $local = Join-Path $root "dist\$($Matches[2])"
        if (Test-Path -LiteralPath $local) {
            $hash = (Get-FileHash -LiteralPath $local -Algorithm SHA256).Hash
            $state = if ($hash -eq $Matches[1].ToUpper()) { 'matches dist\' } else { 'DIFFERENT from dist\' }
            Write-Host "  $($Matches[2]): $state"
        }
    }
}

Write-Host ''
Write-Host $text
if (-not $Yes) {
    $answer = Read-Host "Sign this list for version $Version? Only do this for a release you published (y/n)"
    if ($answer -ne 'y') { Write-Host 'Not signed.'; exit 1 }
}
if (-not $Password) { $Password = Read-Host 'Key password' -AsSecureString }

$private = Unprotect-ReleaseKey ([IO.File]::ReadAllBytes($KeyFile)) $Password
$key = [Security.Cryptography.CngKey]::Import($private, [Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
[Array]::Clear($private, 0, $private.Length)
$ecdsa = New-Object Security.Cryptography.ECDsaCng($key)
$ecdsa.HashAlgorithm = [Security.Cryptography.CngAlgorithm]::Sha256
$signature = $ecdsa.SignData($data)  # IEEE P1363: 64 bytes r || s, what the app's BCrypt check expects
$publicText = Get-PublicKeyText $key
$key.Dispose()

if (-not (Test-ReleaseSignature $data $signature $publicText)) { throw 'The new signature did not verify. Nothing was written.' }
$header = [IO.File]::ReadAllText((Join-Path $root 'src\update_public_key.h'))
if ($header -notmatch [regex]::Escape($publicText)) {
    Write-Warning 'This key is not in src\update_public_key.h: builds from this repository will not accept the signature.'
}
[IO.File]::WriteAllText($OutFile, [Convert]::ToBase64String($signature) + "`n", (New-Object Text.UTF8Encoding($false)))
Write-Host ''
Write-Host "Signature written to $OutFile"
Write-Host "Upload it to the v$Version release as SHA256SUMS.txt.sig:"
Write-Host "  https://github.com/$repo/releases/edit/v$Version"
if (-not $Yes) { Start-Process "https://github.com/$repo/releases/edit/v$Version" }
