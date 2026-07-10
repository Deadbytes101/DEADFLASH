[CmdletBinding()]
param(
    [string]$SourceDir = "",
    [string]$BuildDir = "",
    [string]$EvidenceDir = "",
    [switch]$KeepBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-FullPath {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Base
    )

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $Base $Path))
}

function Get-HostMetadata {
    # Keep evidence collection compatible with Windows PowerShell 5.1 and
    # older .NET Framework builds. Metadata must never abort qualification.
    $osDescription = 'unknown'
    $osArchitecture = 'unknown'
    $processArchitecture = 'unknown'
    $is64BitOperatingSystem = $null
    $is64BitProcess = $null

    try {
        $osDescription = [Environment]::OSVersion.VersionString
    } catch {
        $osDescription = 'unavailable'
    }

    try {
        $is64BitOperatingSystem = [Environment]::Is64BitOperatingSystem
    } catch {
        $is64BitOperatingSystem = $null
    }

    try {
        $is64BitProcess = [Environment]::Is64BitProcess
    } catch {
        $is64BitProcess = $null
    }

    if (-not [string]::IsNullOrWhiteSpace($env:PROCESSOR_ARCHITEW6432)) {
        $osArchitecture = $env:PROCESSOR_ARCHITEW6432
    } elseif (-not [string]::IsNullOrWhiteSpace($env:PROCESSOR_ARCHITECTURE)) {
        $osArchitecture = $env:PROCESSOR_ARCHITECTURE
    } elseif ($is64BitOperatingSystem -eq $true) {
        $osArchitecture = '64-bit'
    } elseif ($is64BitOperatingSystem -eq $false) {
        $osArchitecture = '32-bit'
    }

    if (-not [string]::IsNullOrWhiteSpace($env:PROCESSOR_ARCHITECTURE)) {
        $processArchitecture = $env:PROCESSOR_ARCHITECTURE
    } elseif ($is64BitProcess -eq $true) {
        $processArchitecture = '64-bit'
    } elseif ($is64BitProcess -eq $false) {
        $processArchitecture = '32-bit'
    }

    return [ordered]@{
        computer_name = $env:COMPUTERNAME
        os_description = $osDescription
        os_architecture = $osArchitecture
        process_architecture = $processArchitecture
        is_64_bit_os = $is64BitOperatingSystem
        is_64_bit_process = $is64BitProcess
        powershell = $PSVersionTable.PSVersion.ToString()
        clr = [Environment]::Version.ToString()
    }
}

function Find-VisualStudio {
    $clCommand = Get-Command cl.exe -ErrorAction SilentlyContinue
    $vswhereCandidates = @()

    foreach ($programFilesRoot in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
        if ([string]::IsNullOrWhiteSpace($programFilesRoot)) { continue }
        $candidate = Join-Path $programFilesRoot 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path $candidate -PathType Leaf) {
            $vswhereCandidates += $candidate
        }
    }

    if ($vswhereCandidates.Count -eq 0) {
        if ($null -ne $clCommand) {
            return [ordered]@{
                source = 'existing-environment'
                installation_path = $null
                vswhere = $null
                cl = $clCommand.Source
            }
        }
        throw 'Visual Studio discovery failed: neither vswhere.exe nor cl.exe was found.'
    }

    $vswhere = $vswhereCandidates[0]
    Write-Host "[DISCOVER] $vswhere"
    $installationPathRaw = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath | Select-Object -First 1
    if ($LASTEXITCODE -ne 0) {
        throw "vswhere.exe failed with exit code $LASTEXITCODE."
    }
    if ([string]::IsNullOrWhiteSpace($installationPathRaw)) {
        throw 'No Visual Studio installation with the x64 C++ toolchain was found.'
    }
    $installationPath = $installationPathRaw.Trim()

    $clPath = $null
    $toolsRoot = Join-Path $installationPath 'VC\Tools\MSVC'
    if (Test-Path $toolsRoot -PathType Container) {
        $toolsets = @(Get-ChildItem -Path $toolsRoot -Directory |
            Sort-Object -Property Name -Descending)
        foreach ($toolset in $toolsets) {
            $candidate = Join-Path $toolset.FullName 'bin\Hostx64\x64\cl.exe'
            if (Test-Path $candidate -PathType Leaf) {
                $clPath = $candidate
                break
            }
        }
    }
    if ([string]::IsNullOrWhiteSpace($clPath) -and $null -ne $clCommand) {
        $clPath = $clCommand.Source
    }
    if ([string]::IsNullOrWhiteSpace($clPath)) {
        throw "Visual Studio was found at '$installationPath', but x64 cl.exe was not found."
    }

    return [ordered]@{
        source = 'vswhere'
        installation_path = $installationPath
        vswhere = $vswhere
        cl = $clPath
    }
}

function Invoke-Recorded {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$FilePath,
        [Parameter(Mandatory)][string[]]$Arguments,
        [Parameter(Mandatory)][string]$WorkingDirectory,
        [int[]]$AcceptedExitCodes = @(0),
        [int]$TimeoutSeconds = 900
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

    Write-Host ""
    Write-Host ("[RUN] {0}" -f $Name)
    Write-Host ("      {0} {1}" -f $FilePath, ($nativeArguments -join ' '))
    Write-Host ("      stdout: {0}" -f $stdoutPath)
    Write-Host ("      stderr: {0}" -f $stderrPath)

    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $FilePath `
        -ArgumentList ($nativeArguments -join ' ') `
        -WorkingDirectory $WorkingDirectory -NoNewWindow -PassThru `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath

    $nextHeartbeat = 5
    $timedOut = $false
    while (-not $process.HasExited) {
        Start-Sleep -Seconds 1
        $process.Refresh()
        if ($timer.Elapsed.TotalSeconds -ge $nextHeartbeat) {
            Write-Host ("[RUNNING] {0} elapsed={1:N0}s" -f $Name, $timer.Elapsed.TotalSeconds)
            $nextHeartbeat += 5
        }
        if ($timer.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
            $timedOut = $true
            Write-Host ("[TIMEOUT] {0} exceeded {1}s; terminating process tree." -f $Name, $TimeoutSeconds)
            & taskkill.exe /PID $process.Id /T /F 2>$null | Out-Null
            break
        }
    }
    if (-not $timedOut) {
        $process.WaitForExit()
    }
    $timer.Stop()

    $stdout = if (Test-Path $stdoutPath) { Get-Content $stdoutPath -Raw } else { '' }
    $stderr = if (Test-Path $stderrPath) { Get-Content $stderrPath -Raw } else { '' }
    $exitCode = if ($timedOut) { -1 } else { $process.ExitCode }
    $record = [ordered]@{
        name = $Name
        executable = $FilePath
        arguments = $Arguments
        working_directory = $WorkingDirectory
        exit_code = $exitCode
        timed_out = $timedOut
        timeout_seconds = $TimeoutSeconds
        elapsed_ms = [math]::Round($timer.Elapsed.TotalMilliseconds, 3)
        stdout = $stdout
        stderr = $stderr
    }
    $script:Record.commands += $record

    if ($timedOut) {
        throw "Command '$Name' timed out after $TimeoutSeconds seconds."
    }
    if ($AcceptedExitCodes -notcontains $exitCode) {
        if (-not [string]::IsNullOrWhiteSpace($stdout)) {
            Write-Host "--- $Name stdout ---"
            Write-Host $stdout
        }
        if (-not [string]::IsNullOrWhiteSpace($stderr)) {
            Write-Host "--- $Name stderr ---"
            Write-Host $stderr
        }
        throw "Command '$Name' failed with exit code $exitCode."
    }

    Write-Host ("[PASS] {0} exit={1} elapsed={2:N1}s" -f $Name, $exitCode, $timer.Elapsed.TotalSeconds)
    return $record
}

$scriptRoot = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($scriptRoot)) {
    $scriptPath = $MyInvocation.MyCommand.Path
    if ([string]::IsNullOrWhiteSpace($scriptPath)) {
        throw 'Could not determine the script directory. Pass -SourceDir explicitly.'
    }
    $scriptRoot = Split-Path -Parent $scriptPath
}
if ([string]::IsNullOrWhiteSpace($SourceDir)) {
    $SourceDir = Split-Path -Parent $scriptRoot
} else {
    $SourceDir = Resolve-FullPath -Path $SourceDir -Base (Get-Location).Path
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

Write-Host "DEADFLASH MSVC QUALIFICATION"
Write-Host "============================"
Write-Host ("SOURCE   : {0}" -f $SourceDir)
Write-Host ("BUILD    : {0}" -f $BuildDir)
Write-Host ("EVIDENCE : {0}" -f $evidencePath)
Write-Host ("LOGS     : {0}" -f $script:TempLogDir)

$script:Record = [ordered]@{
    schema = 'deadflash.msvc-qualification.v1'
    created_utc = [DateTimeOffset]::UtcNow.ToString('o')
    result = 'running'
    source_dir = $SourceDir
    build_dir = $BuildDir
    evidence_path = $evidencePath
    log_directory = $script:TempLogDir
    cmake_generator = 'Visual Studio 17 2022'
    cmake_architecture = 'x64'
    expected_tests = 7
    host = Get-HostMetadata
    visual_studio_environment = $null
    git_commit = $null
    commands = @()
    tests = $null
    e2e = $null
    error = $null
}

try {
    Write-Host ""
    Write-Host "[1/7] Discover Visual Studio and MSVC"
    $script:Record.visual_studio_environment = Find-VisualStudio
    Write-Host ("[FOUND] Visual Studio source: {0}" -f $script:Record.visual_studio_environment.source)
    Write-Host ("[FOUND] cl.exe: {0}" -f $script:Record.visual_studio_environment.cl)

    Write-Host ""
    Write-Host "[2/7] Validate required tools"
    foreach ($required in @('cmake.exe', 'ctest.exe', 'python.exe')) {
        $command = Get-Command $required -ErrorAction SilentlyContinue
        if ($null -eq $command) {
            throw "Required executable is unavailable: $required"
        }
        Write-Host ("[FOUND] {0}: {1}" -f $required, $command.Source)
    }

    $git = Get-Command git.exe -ErrorAction SilentlyContinue
    if ($null -ne $git) {
        $commit = (& $git.Source -C $SourceDir rev-parse HEAD 2>$null)
        if ($LASTEXITCODE -eq 0 -and $commit) {
            $script:Record.git_commit = $commit.Trim()
            Write-Host ("[COMMIT] {0}" -f $script:Record.git_commit)
        }
    }

    if ((Test-Path $BuildDir) -and -not $KeepBuild) {
        Write-Host ("[CLEAN] Removing {0}" -f $BuildDir)
        Remove-Item -Recurse -Force $BuildDir
    }
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

    Write-Host ""
    Write-Host "[3/7] Record compiler version"
    Invoke-Recorded -Name 'cl-version' `
        -FilePath $script:Record.visual_studio_environment.cl `
        -Arguments @('/Bv') `
        -WorkingDirectory $SourceDir `
        -AcceptedExitCodes @(0, 2) `
        -TimeoutSeconds 60 | Out-Null

    Write-Host ""
    Write-Host "[4/7] Configure Visual Studio 2022 x64"
    Invoke-Recorded -Name 'cmake-configure' -FilePath 'cmake.exe' `
        -Arguments @('-S', $SourceDir, '-B', $BuildDir,
                     '-G', 'Visual Studio 17 2022', '-A', 'x64',
                     '-DDEADFLASH_WARNINGS_AS_ERRORS=ON',
                     '-DDEADFLASH_BUILD_TESTS=ON') `
        -WorkingDirectory $SourceDir `
        -TimeoutSeconds 300 | Out-Null

    Write-Host ""
    Write-Host "[5/7] Build Release"
    Invoke-Recorded -Name 'cmake-build' -FilePath 'cmake.exe' `
        -Arguments @('--build', $BuildDir, '--config', 'Release', '--parallel') `
        -WorkingDirectory $SourceDir `
        -TimeoutSeconds 1800 | Out-Null

    Write-Host ""
    Write-Host "[6/7] Run all seven tests"
    $ctest = Invoke-Recorded -Name 'ctest' -FilePath 'ctest.exe' `
        -Arguments @('--test-dir', $BuildDir, '-C', 'Release', '--output-on-failure') `
        -WorkingDirectory $SourceDir `
        -TimeoutSeconds 600
    $testsPassed = $ctest.stdout -match '100% tests passed' -and
                   $ctest.stdout -match '0 tests failed out of 7'
    $script:Record.tests = [ordered]@{
        expected = 7
        passed = if ($testsPassed) { 7 } else { $null }
        raw_summary = ($ctest.stdout -split "`r?`n" | Where-Object { $_ -match 'tests passed|Total Test time' })
    }
    if (-not $testsPassed) {
        throw 'CTest exited successfully but did not report all seven tests passing.'
    }

    $deadflash = Join-Path $BuildDir 'Release\deadflash.exe'
    $proof = Join-Path $BuildDir 'Release\deadflash-proof.exe'
    foreach ($binary in @($deadflash, $proof)) {
        if (-not (Test-Path $binary -PathType Leaf)) {
            throw "Expected Release binary is missing: $binary"
        }
    }

    Write-Host ""
    Write-Host "[7/7] Run proof/corruption end-to-end qualification"
    $e2eWork = Join-Path $BuildDir 'e2e-msvc'
    Invoke-Recorded -Name 'e2e-proof' -FilePath 'python.exe' `
        -Arguments @((Join-Path $SourceDir 'scripts\e2e-proof.py'),
                     '--deadflash', $deadflash,
                     '--proof', $proof,
                     '--work-dir', $e2eWork,
                     '--summary', $e2ePath) `
        -WorkingDirectory $SourceDir `
        -TimeoutSeconds 900 | Out-Null
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
    Write-Host ""
    Write-Host "MSVC QUALIFICATION: PASS"
}
catch {
    $script:Record.result = 'fail'
    $script:Record.error = [ordered]@{
        type = $_.Exception.GetType().FullName
        message = $_.Exception.Message
        stack = $_.ScriptStackTrace
    }
    Write-Host ""
    Write-Host ("MSVC QUALIFICATION: FAIL - {0}" -f $_.Exception.Message)
    throw
}
finally {
    $script:Record.completed_utc = [DateTimeOffset]::UtcNow.ToString('o')
    $script:Record | ConvertTo-Json -Depth 12 | Set-Content -Path $evidencePath -Encoding utf8
    Write-Host ("MSVC EVIDENCE: {0}" -f $evidencePath)
    Write-Host ("MSVC LOGS: {0}" -f $script:TempLogDir)
}
