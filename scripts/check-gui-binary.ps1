param(
    [Parameter(Mandatory = $true)]
    [string]$Path
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$resolvedPath = (Resolve-Path -LiteralPath $Path).Path
$item = Get-Item -LiteralPath $resolvedPath
$bytes = [System.IO.File]::ReadAllBytes($resolvedPath)

if ($bytes.Length -lt 256) {
    throw "GUI binary is too small to be a PE image: $resolvedPath"
}
if ($bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
    throw "GUI binary has no MZ header: $resolvedPath"
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

Add-Type -AssemblyName System.Drawing
$icon = [System.Drawing.Icon]::ExtractAssociatedIcon($resolvedPath)
if ($null -eq $icon) {
    throw 'The GUI executable has no extractable application icon.'
}

$bitmap = $null
$bluePixels = 0
$yellowPixels = 0
try {
    $bitmap = $icon.ToBitmap()
    for ($y = 0; $y -lt $bitmap.Height; $y++) {
        for ($x = 0; $x -lt $bitmap.Width; $x++) {
            $color = $bitmap.GetPixel($x, $y)
            if ($color.A -lt 128) {
                continue
            }
            if ($color.B -ge 120 -and
                $color.B -gt ($color.R + 80) -and
                $color.B -gt ($color.G + 20)) {
                $bluePixels++
            }
            if ($color.R -ge 220 -and
                $color.G -ge 160 -and
                $color.B -le 100) {
                $yellowPixels++
            }
        }
    }
} finally {
    if ($null -ne $bitmap) {
        $bitmap.Dispose()
    }
    $icon.Dispose()
}

if ($bluePixels -lt 10 -or $yellowPixels -lt 5) {
    throw "Embedded icon does not match the DEADBYTE blue/yellow mark: blue=$bluePixels yellow=$yellowPixels"
}

Write-Host '[PASS] deadflash-gui.exe PE subsystem, 1.0.0 version, manifest, and DEADBYTE icon'
Write-Host "       File: $resolvedPath"
Write-Host "       Version: $($version.FileVersion)"
Write-Host "       Icon pixels: blue=$bluePixels yellow=$yellowPixels"
