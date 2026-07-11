param(
    [Parameter(Mandatory = $true)]
    [string]$Path
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$resolved = Resolve-Path -LiteralPath $Path
$item = Get-Item -LiteralPath $resolved
$bytes = [System.IO.File]::ReadAllBytes($resolved)

if ($bytes.Length -lt 256) {
    throw "GUI binary is too small to be a PE image: $resolved"
}
if ($bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
    throw "GUI binary has no MZ header: $resolved"
}

$peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
if ($peOffset -lt 0 -or ($peOffset + 96) -gt $bytes.Length) {
    throw "GUI binary has an invalid PE header offset: $peOffset"
}
if ($bytes[$peOffset] -ne 0x50 -or
    $bytes[$peOffset + 1] -ne 0x45 -or
    $bytes[$peOffset + 2] -ne 0 -or
    $bytes[$peOffset + 3] -ne 0) {
    throw 'GUI binary has no PE signature.'
}

$optionalHeader = $peOffset + 24
$subsystem = [BitConverter]::ToUInt16($bytes, $optionalHeader + 68)
if ($subsystem -ne 2) {
    throw "GUI binary subsystem is $subsystem; expected WINDOWS_GUI (2)."
}

$version = $item.VersionInfo
if ($version.FileVersion -notlike '1.0.0*') {
    throw "Unexpected FileVersion: $($version.FileVersion)"
}
if ($version.ProductVersion -notlike '1.0.0*') {
    throw "Unexpected ProductVersion: $($version.ProductVersion)"
}
if ($version.ProductName -ne 'DEADFLASH') {
    throw "Unexpected ProductName: $($version.ProductName)"
}
if ($version.FileDescription -ne 'DEADFLASH Image Writer & Verifier') {
    throw "Unexpected FileDescription: $($version.FileDescription)"
}

$ascii = [System.Text.Encoding]::ASCII.GetString($bytes)
if ($ascii.IndexOf('requireAdministrator', [System.StringComparison]::Ordinal) -lt 0) {
    throw 'Embedded requireAdministrator manifest entry was not found.'
}
if ($ascii.IndexOf('Microsoft.Windows.Common-Controls', [System.StringComparison]::Ordinal) -lt 0) {
    throw 'Embedded Common Controls v6 dependency was not found.'
}
if ($ascii.IndexOf('longPathAware', [System.StringComparison]::Ordinal) -lt 0) {
    throw 'Embedded longPathAware manifest entry was not found.'
}

Write-Host '[PASS] deadflash-gui.exe PE subsystem, 1.0.0 version, and manifest'
Write-Host "       File: $resolved"
Write-Host "       Version: $($version.FileVersion)"
