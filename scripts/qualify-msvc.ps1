[CmdletBinding()]
param(
    [string]$SourceDir = "",
    [string]$BuildDir = "",
    [string]$EvidenceDir = "",
    [switch]$KeepBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptRoot = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($scriptRoot)) {
    $scriptPath = $MyInvocation.MyCommand.Path
    if ([string]::IsNullOrWhiteSpace($scriptPath)) {
        throw 'Could not determine the script directory.'
    }
    $scriptRoot = Split-Path -Parent $scriptPath
}

if ([string]::IsNullOrWhiteSpace($SourceDir)) {
    $SourceDir = Split-Path -Parent $scriptRoot
}

$runningGui = @(Get-Process deadflash-gui -ErrorAction SilentlyContinue)
if ($runningGui.Count -ne 0) {
    $pidText = ($runningGui | ForEach-Object { $_.Id }) -join ', '
    throw @"
deadflash-gui.exe is still running (PID: $pidText) and may lock the build artifact.
Close the GUI before qualification. The executable requires Administrator rights,
so a non-elevated PowerShell may receive Access is denied when stopping it.

Close the window normally, or run this command and accept UAC:

Start-Process powershell.exe -Verb RunAs -Wait -ArgumentList '-NoProfile','-Command','Stop-Process -Name deadflash-gui -Force'
"@
}

$python = Get-Command python.exe -ErrorAction SilentlyContinue
if ($null -eq $python) {
    throw 'Required executable is unavailable: python.exe'
}

$harness = Join-Path $scriptRoot 'qualify-msvc.py'
if (-not (Test-Path $harness -PathType Leaf)) {
    throw "MSVC qualification harness is missing: $harness"
}

$arguments = @($harness, '--source-dir', $SourceDir)
if (-not [string]::IsNullOrWhiteSpace($BuildDir)) {
    $arguments += @('--build-dir', $BuildDir)
}
if (-not [string]::IsNullOrWhiteSpace($EvidenceDir)) {
    $arguments += @('--evidence-dir', $EvidenceDir)
}
if ($KeepBuild) {
    $arguments += '--keep-build'
}

& $python.Source @arguments
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
    throw "MSVC qualification failed with exit code $exitCode."
}
