# Creates the release signing key, once. Run it on your own PC, never in CI:
#   powershell -ExecutionPolicy Bypass -File scripts\new-release-key.ps1
# Writes the password-protected private key to -KeyFile (outside the repository) and puts the public
# key into src\update_public_key.h, which you commit. Builds made after that only install updates
# signed with this key (scripts\sign-release.ps1).
param(
    [string]$KeyFile = (Join-Path $env:USERPROFILE '.wirelessbattery\release-key.wbk'),
    [string]$PublicKeyHeader = (Join-Path (Split-Path $PSScriptRoot -Parent) 'src\update_public_key.h'),
    [Security.SecureString]$Password  # for tests; normally asked for
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'release-key.psm1') -Force

if (Test-Path -LiteralPath $KeyFile) { throw "$KeyFile already exists. Refusing to overwrite a release key." }
$repo = Split-Path $PSScriptRoot -Parent
if ([IO.Path]::GetFullPath($KeyFile).StartsWith([IO.Path]::GetFullPath($repo), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Keep the key file outside the repository, so it can never be committed.'
}
if (-not $Password) {
    $Password = Read-Host 'New password for the key (at least 12 characters)' -AsSecureString
    $again = Read-Host 'Same password again' -AsSecureString
    if ((ConvertTo-PlainText $Password) -cne (ConvertTo-PlainText $again)) { throw 'The passwords do not match.' }
}
if ((ConvertTo-PlainText $Password).Length -lt 12) { throw 'Use a password of at least 12 characters.' }

$params = New-Object Security.Cryptography.CngKeyCreationParameters
$params.ExportPolicy = [Security.Cryptography.CngExportPolicies]::AllowPlaintextExport
$params.KeyCreationOptions = [Security.Cryptography.CngKeyCreationOptions]::None
# [NullString]: a plain $null would become "" and save a named key in the Windows key store.
$key = [Security.Cryptography.CngKey]::Create([Security.Cryptography.CngAlgorithm]::ECDsaP256, [NullString]::Value, $params)
$private = $key.Export([Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
$publicText = Get-PublicKeyText $key
$key.Dispose()

New-Item -ItemType Directory -Force (Split-Path $KeyFile -Parent) | Out-Null
[IO.File]::WriteAllBytes($KeyFile, (Protect-ReleaseKey $private $Password))
[Array]::Clear($private, 0, $private.Length)

# Add the public key to the header: replace the empty placeholder, or append a second key.
$header = [IO.File]::ReadAllText($PublicKeyHeader)
if ($header -match '(?m)^\s*"",\s*$') { $header = $header -replace '(?m)^(\s*)"",\s*$', "`$1`"$publicText`"," }
else { $header = $header -replace '(?m)^(\s*)"([A-Za-z0-9+/=]+)",\s*$', "`$0`r`n`$1`"$publicText`"," }
if ($header -notmatch [regex]::Escape($publicText)) { throw "Could not add the key to $PublicKeyHeader; add `"$publicText`" by hand." }
[IO.File]::WriteAllText($PublicKeyHeader, $header, (New-Object Text.UTF8Encoding($false)))

Write-Host ''
Write-Host "Private key: $KeyFile (encrypted with your password)"
Write-Host "Public key:  $publicText"
Write-Host "             added to $PublicKeyHeader - commit that file."
Write-Host ''
Write-Host 'Back up the key file AND remember the password (for example in a password manager).' -ForegroundColor Yellow
Write-Host 'Without them, no further update can be installed automatically: users would have to' -ForegroundColor Yellow
Write-Host 'download one release by hand that carries a new key.' -ForegroundColor Yellow
