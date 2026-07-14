param(
    [Parameter(Mandatory = $true)]
    [string]$Base64Path,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$source = (Resolve-Path -LiteralPath $Base64Path).Path
$destination = [System.IO.Path]::GetFullPath($OutputPath)
$directory = [System.IO.Path]::GetDirectoryName($destination)
if ([string]::IsNullOrWhiteSpace($directory)) {
    throw "Could not resolve icon output directory: $destination"
}

[System.IO.Directory]::CreateDirectory($directory) | Out-Null
$payload = (Get-Content -LiteralPath $source -Raw) -replace '\s', ''
$bytes = [Convert]::FromBase64String($payload)

if ($bytes.Length -lt 256) {
    throw "Decoded icon is unexpectedly small: $($bytes.Length) bytes"
}
if ($bytes[0] -ne 0 -or $bytes[1] -ne 0 -or
    $bytes[2] -ne 1 -or $bytes[3] -ne 0) {
    throw 'Decoded payload is not a Windows ICO file.'
}

$temp = "$destination.tmp-$PID"
try {
    [System.IO.File]::WriteAllBytes($temp, $bytes)
    Move-Item -LiteralPath $temp -Destination $destination -Force
} finally {
    Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
}

Write-Host "[PASS] materialized DEADBYTE icon: $destination"
