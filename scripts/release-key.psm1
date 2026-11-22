# Shared helpers for new-release-key.ps1 and sign-release.ps1.
# The release key is ECDSA P-256 (Windows CNG). Its private half is stored encrypted with a password:
#   "WBK1" | salt (16) | IV (16) | HMAC-SHA256 (32) | AES-256-CBC ciphertext
# with both AES and HMAC keys derived from the password by PBKDF2-SHA256 (600,000 rounds).
$ErrorActionPreference = 'Stop'
$Script:Magic = [Text.Encoding]::ASCII.GetBytes('WBK1')
$Script:Rounds = 600000

function ConvertTo-PlainText([Security.SecureString]$Secure) {
    $ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($Secure)
    try { [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr) } finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr) }
}

function Get-DerivedKeys([Security.SecureString]$Password, [byte[]]$Salt) {
    $kdf = New-Object Security.Cryptography.Rfc2898DeriveBytes((ConvertTo-PlainText $Password), $Salt, $Script:Rounds,
        [Security.Cryptography.HashAlgorithmName]::SHA256)
    try { , $kdf.GetBytes(64) } finally { $kdf.Dispose() }
}

function Protect-ReleaseKey([byte[]]$PrivateBlob, [Security.SecureString]$Password) {
    $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
    $salt = New-Object byte[] 16; $iv = New-Object byte[] 16
    $rng.GetBytes($salt); $rng.GetBytes($iv)
    $keys = Get-DerivedKeys $Password $salt
    $aes = [Security.Cryptography.Aes]::Create()
    $aes.Key = $keys[0..31]; $aes.IV = $iv
    $cipher = $aes.CreateEncryptor().TransformFinalBlock($PrivateBlob, 0, $PrivateBlob.Length)
    $hmac = New-Object Security.Cryptography.HMACSHA256(, [byte[]]$keys[32..63])
    $mac = $hmac.ComputeHash([byte[]]($Script:Magic + $salt + $iv + $cipher))
    , [byte[]]($Script:Magic + $salt + $iv + $mac + $cipher)
}

function Unprotect-ReleaseKey([byte[]]$File, [Security.SecureString]$Password) {
    if ($File.Length -lt 4 + 16 + 16 + 32 + 16 -or [Text.Encoding]::ASCII.GetString($File, 0, 4) -ne 'WBK1') {
        throw 'This is not a Taskbar Vitals release key file.'
    }
    $salt = $File[4..19]; $iv = $File[20..35]; $mac = $File[36..67]; $cipher = $File[68..($File.Length - 1)]
    $keys = Get-DerivedKeys $Password $salt
    $hmac = New-Object Security.Cryptography.HMACSHA256(, [byte[]]$keys[32..63])
    $expected = $hmac.ComputeHash([byte[]]($Script:Magic + $salt + $iv + $cipher))
    $diff = 0
    for ($i = 0; $i -lt 32; $i++) { $diff = $diff -bor ($expected[$i] -bxor $mac[$i]) }
    if ($diff -ne 0) { throw 'Wrong password, or the key file is damaged.' }
    $aes = [Security.Cryptography.Aes]::Create()
    $aes.Key = $keys[0..31]; $aes.IV = $iv
    , $aes.CreateDecryptor().TransformFinalBlock([byte[]]$cipher, 0, $cipher.Length)
}

# Public half as the app embeds it: a BCRYPT_ECCPUBLIC_BLOB, base64.
function Get-PublicKeyText([Security.Cryptography.CngKey]$Key) {
    [Convert]::ToBase64String($Key.Export([Security.Cryptography.CngKeyBlobFormat]::EccPublicBlob))
}

function Test-ReleaseSignature([byte[]]$Data, [byte[]]$Signature, [string]$PublicKeyText) {
    $public = [Security.Cryptography.CngKey]::Import([Convert]::FromBase64String($PublicKeyText),
        [Security.Cryptography.CngKeyBlobFormat]::EccPublicBlob)
    $ecdsa = New-Object Security.Cryptography.ECDsaCng($public)
    $ecdsa.HashAlgorithm = [Security.Cryptography.CngAlgorithm]::Sha256
    $ecdsa.VerifyData($Data, $Signature)
}

Export-ModuleMember -Function ConvertTo-PlainText, Protect-ReleaseKey, Unprotect-ReleaseKey, Get-PublicKeyText, Test-ReleaseSignature
