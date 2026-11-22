param([Parameter(Mandatory = $true)][string]$ExpectedVersion)
$ErrorActionPreference = 'Stop'
if ($ExpectedVersion -notmatch '^\d+\.\d+\.\d+$') { throw 'ExpectedVersion must be MAJOR.MINOR.PATCH.' }
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root 'BatteryHub.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw 'Build BatteryHub.exe first (make or build.bat).' }
$info = [Diagnostics.FileVersionInfo]::GetVersionInfo($exe)
$numeric = '{0}.{1}.{2}' -f $info.FileMajorPart, $info.FileMinorPart, $info.FileBuildPart
if ($numeric -ne $ExpectedVersion -or $info.ProductVersion -ne $ExpectedVersion -or $info.FileVersion -ne $ExpectedVersion) {
    throw "Executable version mismatch: expected $ExpectedVersion, found $($info.FileVersion) / $($info.ProductVersion). Rebuild before packaging."
}
Write-Output "Executable version verified: $ExpectedVersion"
