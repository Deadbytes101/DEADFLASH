[CmdletBinding()]
param(
    [string]$SourceDir = (Split-Path -Parent $PSScriptRoot),
    [string]$BuildDir = "",
    [string]$EvidenceDir = "",
    [switch]$KeepBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-FullPath {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Base)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $Base $Path))
}

function Import-VcVars64 {
    $cl = Get-Command cl.exe -ErrorAction SilentlyContinue
    if ($null -ne $cl) {
        return [ordered]@{ source = 'existing-environment'; vcvars = $null }
    }

    $vswhereCandidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    ) | Where-Object { $_ -and (Test-Path $_ -PathType Leaf) }

    if ($vswhereCandidates.Count -eq 0) {
        throw 'cl.exe is not in PATH and vswhere.exe was not found.'
    }

    $vswhere = $vswhereCandidates[0]
    $installationPath = (& $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath | Select-Object -First 1).Trim()
    if ([string]::IsNullOrWhiteSpace($installationPath)) {
        throw 'No Visual Studio installation with the x64 C++ toolchain was found.'
    }

    $vcvars = Join-Path $installationPath 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars -PathType Leaf)) {
        throw "vcvars64.bat was not found at '$vcvars'."
    }

    $lines = & $env:ComSpec /d /s /c "`"$vcvars`" >nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw "vcvars64.bat failed with exit code $LASTEXITCODE."
    }
    foreach ($line in $lines) {
        $separator = $line.IndexOf('=')
        if ($separator -le 0) { continue }
        $name = $line.Substring(0, $separator)
        $value = $line.Substring($separator + 1)
        [Environment]::SetEnvironmentVariable($name, $value, 'Process')
    }

    if ($null -eq (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw 'vcvars64.bat completed but cl.exe is still unavailable.'
    }
    return [ordered]@{ source = 'vswhere-vcvars64'; vcvars = $vcvars }
}

function Invoke-Recorded {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$FilePath,
        [Parameter(Mandatory)][string[]]$Arguments,
        [Parameter(Mandatory)][string]$WorkingDirectory,
        [int[]]$AcceptedExitCodes = @(0)
    )

    $stdoutPath = Join-Path $script:TempLogDir ("{0}.stdout.txt" -f $Name)
    $stderrPath = Join-Path $script:TempLogDir ("{0}.stderr.txt" -f $Name)
    $nativeArguments = foreach ($argument in $Arguments) {
        if ($argument.Length -eq 0) {
            '""'
        } elseif ($argument -match '[\s"]') {
            '"' + $argument.Replace('"', '\"') + '"'
        } else {
            $argument
        }
    }
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $FilePath `
        -ArgumentList ($nativeArguments -join ' ') `
        -WorkingDirectory $WorkingDirectory -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $timer.Stop()

    $stdout = if (Test-Path $stdoutPath) { Get-Content $stdoutPath -Raw } else { '' }
    $stderr = if (Test-Path $stderrPath) { Get-Content $stderrPath -Raw } else { '' }
    $record = [ordered]@{
        name = $Name
        executable = $FilePath
        arguments = $Arguments
        working_directory = $WorkingDirectory
        exit_code = $process.ExitCode
        elapsed_ms = [math]::Round($timer.Elapsed.TotalMilliseconds, 3)
        stdout = $stdout
        stderr = $stderr
    }
    $script:Record.commands += $record

    if ($AcceptedExitCodes -notcontains $process.ExitCode) {
        throw "Command '$Name' failed with exit code $($process.ExitCode)."
    }
    return $record
}

$SourceDir = [System.IO.Path]::GetFullPath($SourceDir)
if (-not (Test-Path $SourceDir -PathType Container)) {
    throw "Source directory does not exist: $SourceDir"
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $SourceDir 'build-msvc-qualification'
} else {
    $BuildDir = Resolve-FullPath -Path $BuildDir -Base $SourceDir
}
if ([string]::IsNullOrWhiteSpace($EvidenceDir)) {
    $EvidenceDir = Join-Path $SourceDir 'bench\results'
} else {
    $EvidenceDir = Resolve-FullPath -Path $EvidenceDir -Base $SourceDir
}

$timestamp = [DateTimeOffset]::UtcNow.ToString('yyyyMMddTHHmmssZ')
New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null
$script:TempLogDir = Join-Path ([System.IO.Path]::GetTempPath()) ("deadflash-msvc-$timestamp")
New-Item -ItemType Directory -Force -Path $script:TempLogDir | Out-Null
$evidencePath = Join-Path $EvidenceDir ("msvc-qualification-$timestamp.json")
$e2ePath = Join-Path $EvidenceDir ("msvc-e2e-$timestamp.json")

$script:Record = [ordered]@{
    schema = 'deadflash.msvc-qualification.v1'
    created_utc = [DateTimeOffset]::UtcNow.ToString('o')
    result = 'running'
    source_dir = $SourceDir
    build_dir = $BuildDir
    evidence_path = $evidencePath
    host = [ordered]@{
        computer_name = $env:COMPUTERNAME
        os_description = [System.Runtime.InteropServices.RuntimeInformation]::OSDescription
        os_architecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
        process_architecture = [System.Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()
        powershell = $PSVersionTable.PSVersion.ToString()
    }
    visual_studio_environment = $null
    git_commit = $null
    commands = @()
    tests = $null
    e2e = $null
    error = $null
}

try {
    $script:Record.visual_studio_environment = Import-VcVars64

    foreach ($required in @('cmake.exe', 'ctest.exe', 'cl.exe', 'python.exe')) {
        if ($null -eq (Get-Command $required -ErrorAction SilentlyContinue)) {
            throw "Required executable is unavailable: $required"
        }
    }

    $git = Get-Command git.exe -ErrorAction SilentlyContinue
    if ($null -ne $git) {
        $commit = (& $git.Source -C $SourceDir rev-parse HEAD 2>$null)
        if ($LASTEXITCODE -eq 0 -and $commit) {
            $script:Record.git_commit = $commit.Trim()
        }
    }

    if ((Test-Path $BuildDir) -and -not $KeepBuild) {
        Remove-Item -Recurse -Force $BuildDir
    }
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

    Invoke-Recorded -Name 'cl-version' -FilePath $env:ComSpec `
        -Arguments @('/d', '/s', '/c', 'cl.exe /Bv 2>&1') `
        -WorkingDirectory $SourceDir -AcceptedExitCodes @(0, 2) | Out-Null

    Invoke-Recorded -Name 'cmake-configure' -FilePath 'cmake.exe' `
        -Arguments @('-S', $SourceDir, '-B', $BuildDir, '-A', 'x64',
                     '-DDEADFLASH_WARNINGS_AS_ERRORS=ON',
                     '-DDEADFLASH_BUILD_TESTS=ON') `
        -WorkingDirectory $SourceDir | Out-Null

    Invoke-Recorded -Name 'cmake-build' -FilePath 'cmake.exe' `
        -Arguments @('--build', $BuildDir, '--config', 'Release', '--parallel') `
        -WorkingDirectory $SourceDir | Out-Null

    $ctest = Invoke-Recorded -Name 'ctest' -FilePath 'ctest.exe' `
        -Arguments @('--test-dir', $BuildDir, '-C', 'Release', '--output-on-failure') `
        -WorkingDirectory $SourceDir
    $script:Record.tests = [ordered]@{
        expected = 7
        passed = if ($ctest.stdout -match '100% tests passed') { 7 } else { $null }
        raw_summary = ($ctest.stdout -split "`r?`n" | Where-Object { $_ -match 'tests passed|Total Test time' })
    }

    $deadflash = Join-Path $BuildDir 'Release\deadflash.exe'
    $proof = Join-Path $BuildDir 'Release\deadflash-proof.exe'
    foreach ($binary in @($deadflash, $proof)) {
        if (-not (Test-Path $binary -PathType Leaf)) {
            throw "Expected Release binary is missing: $binary"
        }
    }

    $e2eWork = Join-Path $BuildDir 'e2e-msvc'
    Invoke-Recorded -Name 'e2e-proof' -FilePath 'python.exe' `
        -Arguments @((Join-Path $SourceDir 'scripts\e2e-proof.py'),
                     '--deadflash', $deadflash,
                     '--proof', $proof,
                     '--work-dir', $e2eWork,
                     '--summary', $e2ePath) `
        -WorkingDirectory $SourceDir | Out-Null
    if (-not (Test-Path $e2ePath -PathType Leaf)) {
        throw "E2E evidence was not created: $e2ePath"
    }
    $script:Record.e2e = Get-Content $e2ePath -Raw | ConvertFrom-Json
    if ($script:Record.e2e.write_state -ne 'success_verified' -or
        $script:Record.e2e.proof_state -ne 'success_proven' -or
        $script:Record.e2e.corruption_state -ne 'target_mismatch' -or
        $script:Record.e2e.injected_bad_offset -ne $script:Record.e2e.reported_bad_offset) {
        throw 'MSVC E2E evidence does not satisfy the qualification contract.'
    }

    $script:Record.result = 'pass'
}
catch {
    $script:Record.result = 'fail'
    $script:Record.error = [ordered]@{
        type = $_.Exception.GetType().FullName
        message = $_.Exception.Message
        stack = $_.ScriptStackTrace
    }
    throw
}
finally {
    $script:Record.completed_utc = [DateTimeOffset]::UtcNow.ToString('o')
    $script:Record | ConvertTo-Json -Depth 12 | Set-Content -Path $evidencePath -Encoding utf8
    Remove-Item -Recurse -Force $script:TempLogDir -ErrorAction SilentlyContinue
    Write-Host "MSVC EVIDENCE: $evidencePath"
}
