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
                installation_version = $null
                major_version = 0
                vswhere = $null
                cl = $clCommand.Source
                bundled_cmake = $null
                bundled_ctest = $null
            }
        }
        throw 'Visual Studio discovery failed: neither vswhere.exe nor cl.exe was found.'
    }

    $vswhere = $vswhereCandidates[0]
    Write-Host "[DISCOVER] $vswhere"

    $commonArguments = @(
        '-latest',
        '-products', '*',
        '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
    )

    $installationPathRaw = & $vswhere @commonArguments -property installationPath |
        Select-Object -First 1
    if ($LASTEXITCODE -ne 0) {
        throw "vswhere.exe failed while reading installationPath (exit $LASTEXITCODE)."
    }
    if ([string]::IsNullOrWhiteSpace($installationPathRaw)) {
        throw 'No Visual Studio installation with the x64 C++ toolchain was found.'
    }
    $installationPath = $installationPathRaw.Trim()

    $installationVersionRaw = & $vswhere @commonArguments -property installationVersion |
        Select-Object -First 1
    if ($LASTEXITCODE -ne 0) {
        throw "vswhere.exe failed while reading installationVersion (exit $LASTEXITCODE)."
    }

    $installationVersion = $null
    $majorVersion = 0
    if (-not [string]::IsNullOrWhiteSpace($installationVersionRaw)) {
        $installationVersion = $installationVersionRaw.Trim()
        $parsedVersion = $null
        if ([Version]::TryParse($installationVersion, [ref]$parsedVersion)) {
            $majorVersion = $parsedVersion.Major
        }
    }

    if ($majorVersion -le 0) {
        $leaf = Split-Path -Leaf $installationPath
        $parsedMajor = 0
        if ([int]::TryParse($leaf, [ref]$parsedMajor)) {
            $majorVersion = $parsedMajor
        }
    }

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

    $bundledCmake = $null
    $bundledCtest = $null
    $bundledCmakeRoot = Join-Path $installationPath `
        'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
    $bundledCmakeCandidate = Join-Path $bundledCmakeRoot 'cmake.exe'
    $bundledCtestCandidate = Join-Path $bundledCmakeRoot 'ctest.exe'
    if ((Test-Path $bundledCmakeCandidate -PathType Leaf) -and
        (Test-Path $bundledCtestCandidate -PathType Leaf)) {
        $bundledCmake = $bundledCmakeCandidate
        $bundledCtest = $bundledCtestCandidate
    }

    return [ordered]@{
        source = 'vswhere'
        installation_path = $installationPath
        installation_version = $installationVersion
        major_version = $majorVersion
        vswhere = $vswhere
        cl = $clPath
        bundled_cmake = $bundledCmake
        bundled_ctest = $bundledCtest
    }
}

function Select-CMakeGenerator {
    param(
        [Parameter(Mandatory)][string]$HelpText,
        [Parameter(Mandatory)][int]$VisualStudioMajor
    )

    $allMatches = [regex]::Matches(
        $HelpText,
        '(?m)^\s*\*?\s*(Visual Studio\s+(\d+)\s+[^=\r\n]+?)\s*='
    )
    $generators = @()
    foreach ($match in $allMatches) {
        $generators += [ordered]@{
            name = $match.Groups[1].Value.Trim()
            major = [int]$match.Groups[2].Value
        }
    }

    if ($generators.Count -eq 0) {
        throw 'CMake did not report any Visual Studio generators in `cmake --help`.'
    }

    if ($VisualStudioMajor -gt 0) {
        $matching = @($generators | Where-Object { $_.major -eq $VisualStudioMajor })
        if ($matching.Count -gt 0) {
            return $matching[0].name
        }

        $available = ($generators | ForEach-Object { $_.name }) -join ', '
        throw (
            "Installed Visual Studio major version is $VisualStudioMajor, but this CMake " +
            "does not provide a matching generator. Available: $available. " +
            'Install a newer CMake build that supports the installed Visual Studio.'
        )
    }

    $selected = $generators |
        Sort-Object -Property major -Descending |
        Select-Object -First 1
    return $selected.name
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
    while (-not $process.WaitForExit(1000)) {
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

    $exitCode = -1
    if (-not $timedOut) {
        # The second parameterless wait flushes redirected streams. Refresh is
        # required for reliable ExitCode retrieval on Windows PowerShell 5.1.
        $process.WaitForExit()
        $process.Refresh()
        $rawExitCode = $process.ExitCode
        if ($null -eq $rawExitCode) {
            throw "Command '$Name' exited but PowerShell did not expose an exit code."
        }
        $exitCode = [int]$rawExitCode
    }
    $timer.Stop()

    $stdout = if (Test-Path $stdoutPath) { Get-Content $stdoutPath -Raw } else { '' }
    $stderr = if (Test-Path $stderrPath) { Get-Content $stderrPath -Raw } else { '' }
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
    cmake_generator = $null
    cmake_architecture = 'x64'
    expected_tests = 7
    host = Get-HostMetadata
    visual_studio_environment = $null
    build_tools = $null
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
    Write-Host ("[FOUND] Visual Studio version: {0}" -f $script:Record.visual_studio_environment.installation_version)
    Write-Host ("[FOUND] cl.exe: {0}" -f $script:Record.visual_studio_environment.cl)

    Write-Host ""
    Write-Host "[2/7] Validate tools and select matching CMake generator"
    $toolCommands = [ordered]@{}

    if (-not [string]::IsNullOrWhiteSpace(
            $script:Record.visual_studio_environment.bundled_cmake
        )) {
        $toolCommands['cmake.exe'] =
            $script:Record.visual_studio_environment.bundled_cmake
        $toolCommands['ctest.exe'] =
            $script:Record.visual_studio_environment.bundled_ctest
        $cmakeSource = 'visual-studio-bundled'
    } else {
        $cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
        $ctestCommand = Get-Command ctest.exe -ErrorAction SilentlyContinue
        if ($null -eq $cmakeCommand) {
            throw 'Required executable is unavailable: cmake.exe'
        }
        if ($null -eq $ctestCommand) {
            throw 'Required executable is unavailable: ctest.exe'
        }
        $toolCommands['cmake.exe'] = $cmakeCommand.Source
        $toolCommands['ctest.exe'] = $ctestCommand.Source
        $cmakeSource = 'path'
    }

    $pythonCommand = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($null -eq $pythonCommand) {
        throw 'Required executable is unavailable: python.exe'
    }
    $toolCommands['python.exe'] = $pythonCommand.Source

    $script:Record.build_tools = [ordered]@{
        cmake_source = $cmakeSource
        cmake = $toolCommands['cmake.exe']
        ctest = $toolCommands['ctest.exe']
        python = $toolCommands['python.exe']
    }
    Write-Host ("[FOUND] cmake.exe ({0}): {1}" -f
        $cmakeSource, $toolCommands['cmake.exe'])
    Write-Host ("[FOUND] ctest.exe: {0}" -f $toolCommands['ctest.exe'])
    Write-Host ("[FOUND] python.exe: {0}" -f $toolCommands['python.exe'])

    Invoke-Recorded -Name 'cmake-version' `
        -FilePath $toolCommands['cmake.exe'] `
        -Arguments @('--version') `
        -WorkingDirectory $SourceDir `
        -TimeoutSeconds 60 | Out-Null
    $cmakeHelp = Invoke-Recorded -Name 'cmake-help' `
        -FilePath $toolCommands['cmake.exe'] `
        -Arguments @('--help') `
        -WorkingDirectory $SourceDir `
        -TimeoutSeconds 60
    $cmakeHelpText = $cmakeHelp.stdout + "`n" + $cmakeHelp.stderr
    $generator = Select-CMakeGenerator `
        -HelpText $cmakeHelpText `
        -VisualStudioMajor $script:Record.visual_studio_environment.major_version
    $script:Record.cmake_generator = $generator
    Write-Host ("[SELECTED] CMake generator: {0}" -f $generator)

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
    Write-Host "[3/7] Compile MSVC probe and record compiler version"
    $probeSource = Join-Path $script:TempLogDir 'cl-version-probe.c'
    Set-Content -Path $probeSource `
        -Value 'int deadflash_msvc_probe(void) { return 0; }' `
        -Encoding ascii
    Invoke-Recorded -Name 'cl-version' `
        -FilePath $script:Record.visual_studio_environment.cl `
        -Arguments @('/nologo', '/Bv', '/TC', '/Zs', $probeSource) `
        -WorkingDirectory $SourceDir `
        -AcceptedExitCodes @(0) `
        -TimeoutSeconds 60 | Out-Null

    Write-Host ""
    Write-Host ("[4/7] Configure {0} x64" -f $generator)
    Invoke-Recorded -Name 'cmake-configure' `
        -FilePath $toolCommands['cmake.exe'] `
        -Arguments @(
            '-S', $SourceDir,
            '-B', $BuildDir,
            '-G', $generator,
            '-A', 'x64',
            '-DDEADFLASH_WARNINGS_AS_ERRORS=ON',
            '-DDEADFLASH_BUILD_TESTS=ON'
        ) `
        -WorkingDirectory $SourceDir `
        -TimeoutSeconds 300 | Out-Null

    Write-Host ""
    Write-Host "[5/7] Build Release"
    Invoke-Recorded -Name 'cmake-build' `
        -FilePath $toolCommands['cmake.exe'] `
        -Arguments @('--build', $BuildDir, '--config', 'Release', '--parallel') `
        -WorkingDirectory $SourceDir `
        -TimeoutSeconds 1800 | Out-Null

    Write-Host ""
    Write-Host "[6/7] Run all seven tests"
    $ctest = Invoke-Recorded -Name 'ctest' `
        -FilePath $toolCommands['ctest.exe'] `
        -Arguments @('--test-dir', $BuildDir, '-C', 'Release', '--output-on-failure') `
        -WorkingDirectory $SourceDir `
        -TimeoutSeconds 600
    $testsPassed = $ctest.stdout -match '100% tests passed' -and
                   $ctest.stdout -match '0 tests failed out of 7'
    $script:Record.tests = [ordered]@{
        expected = 7
        passed = if ($testsPassed) { 7 } else { $null }
        raw_summary = (
            $ctest.stdout -split "`r?`n" |
            Where-Object { $_ -match 'tests passed|Total Test time' }
        )
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
    Invoke-Recorded -Name 'e2e-proof' `
        -FilePath $toolCommands['python.exe'] `
        -Arguments @(
            (Join-Path $SourceDir 'scripts\e2e-proof.py'),
            '--deadflash', $deadflash,
            '--proof', $proof,
            '--work-dir', $e2eWork,
            '--summary', $e2ePath
        ) `
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
    $script:Record | ConvertTo-Json -Depth 12 |
        Set-Content -Path $evidencePath -Encoding utf8
    Write-Host ("MSVC EVIDENCE: {0}" -f $evidencePath)
    Write-Host ("MSVC LOGS: {0}" -f $script:TempLogDir)
}
