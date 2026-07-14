[CmdletBinding()]
param(
    [string]$BuildDir = "",
    [string]$EvidenceDir = "",
    [string]$OutputDir = "",
    [switch]$KeepBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$version = '1.0.0'
$tag = "v$version"
$repo = 'Deadbytes101/DEADFLASH'
$scriptRoot = $PSScriptRoot
$sourceRoot = Split-Path -Parent $scriptRoot

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $sourceRoot 'build-release-local'
}
if ([string]::IsNullOrWhiteSpace($EvidenceDir)) {
    $EvidenceDir = Join-Path $sourceRoot 'release-evidence-local'
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $sourceRoot 'release-local'
}

$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$EvidenceDir = [System.IO.Path]::GetFullPath($EvidenceDir)
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)

function Require-Command {
    param([Parameter(Mandatory = $true)][string]$Name)
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        throw "Required command is unavailable: $Name"
    }
    return $command
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][scriptblock]$Action
    )

    Write-Host ""
    Write-Host "[$Label]"
    & $Action
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

$null = Require-Command git.exe
$null = Require-Command gh.exe
$null = Require-Command python.exe

Push-Location $sourceRoot
try {
    Write-Host 'DEADFLASH 1.0.0 LOCAL RELEASE PUBLISHER'
    Write-Host '========================================='
    Write-Host "SOURCE   : $sourceRoot"
    Write-Host "BUILD    : $BuildDir"
    Write-Host "EVIDENCE : $EvidenceDir"
    Write-Host "OUTPUT   : $OutputDir"
    Write-Host "REPO     : $repo"
    Write-Host "TAG      : $tag"

    Invoke-Checked 'VERIFY GITHUB AUTHENTICATION' {
        gh auth status
    }

    $branch = (& git branch --show-current).Trim()
    if ($branch -ne 'main') {
        throw "Release must be published from main; current branch is '$branch'."
    }

    $dirty = @(& git status --porcelain)
    if ($dirty.Count -ne 0) {
        throw "Working tree is not clean. Commit or discard local changes before release.`n$($dirty -join "`n")"
    }

    Invoke-Checked 'FETCH ORIGIN AND TAGS' {
        git fetch --prune --tags origin
    }

    $head = (& git rev-parse HEAD).Trim()
    $originMain = (& git rev-parse origin/main).Trim()
    if ($head -ne $originMain) {
        throw "Local main does not match origin/main.`nLOCAL : $head`nREMOTE: $originMain"
    }

    $existingRemoteTag = (& git ls-remote --tags origin "refs/tags/$tag").Trim()
    if (-not [string]::IsNullOrWhiteSpace($existingRemoteTag)) {
        throw "Remote tag already exists: $tag"
    }

    gh release view $tag --repo $repo *> $null
    if ($LASTEXITCODE -eq 0) {
        throw "GitHub release already exists: $tag"
    }
    $global:LASTEXITCODE = 0

    $runningGui = @(Get-Process deadflash-gui -ErrorAction SilentlyContinue)
    if ($runningGui.Count -ne 0) {
        $pidText = ($runningGui | ForEach-Object { $_.Id }) -join ', '
        throw "deadflash-gui.exe is running (PID: $pidText). Close it before qualification."
    }

    Remove-Item -LiteralPath $EvidenceDir -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $OutputDir -Recurse -Force -ErrorAction SilentlyContinue

    $qualification = Join-Path $scriptRoot 'qualify-msvc.ps1'
    $qualificationArgs = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $qualification,
        '-SourceDir', $sourceRoot,
        '-BuildDir', $BuildDir,
        '-EvidenceDir', $EvidenceDir
    )
    if ($KeepBuild) {
        $qualificationArgs += '-KeepBuild'
    }

    Invoke-Checked 'RUN FULL MSVC QUALIFICATION' {
        powershell.exe @qualificationArgs
    }

    $releaseBuildDir = Join-Path $BuildDir 'Release'
    $executables = @(
        'deadflash-gui.exe',
        'deadflash.exe',
        'deadflash-proof.exe'
    )

    foreach ($name in $executables) {
        $path = Join-Path $releaseBuildDir $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Qualified executable is missing: $path"
        }
    }

    $evidenceFiles = @(Get-ChildItem -LiteralPath $EvidenceDir -Filter '*.json' -File | Sort-Object Name)
    if ($evidenceFiles.Count -lt 2) {
        throw "Expected qualification and E2E JSON evidence in $EvidenceDir"
    }
    foreach ($evidence in $evidenceFiles) {
        Get-Content -LiteralPath $evidence.FullName -Raw | ConvertFrom-Json | Out-Null
    }

    $packageName = "DEADFLASH-$version-windows-x64"
    $packageDir = Join-Path $OutputDir $packageName
    $packageEvidenceDir = Join-Path $packageDir 'evidence'
    New-Item -ItemType Directory -Path $packageEvidenceDir -Force | Out-Null

    foreach ($name in $executables) {
        Copy-Item -LiteralPath (Join-Path $releaseBuildDir $name) -Destination (Join-Path $packageDir $name)
        Copy-Item -LiteralPath (Join-Path $releaseBuildDir $name) -Destination (Join-Path $OutputDir $name)
    }

    foreach ($name in @('README.md', 'CHANGELOG.md', 'LICENSE', 'SECURITY.md')) {
        $source = Join-Path $sourceRoot $name
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            Copy-Item -LiteralPath $source -Destination (Join-Path $packageDir $name)
        }
    }

    foreach ($evidence in $evidenceFiles) {
        Copy-Item -LiteralPath $evidence.FullName -Destination (Join-Path $packageEvidenceDir $evidence.Name)
        Copy-Item -LiteralPath $evidence.FullName -Destination (Join-Path $OutputDir $evidence.Name)
    }

    $builtAt = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    $buildInfo = @(
        'DEADFLASH 1.0.0 WINDOWS X64',
        '============================',
        '',
        "COMMIT       $head",
        "TAG          $tag",
        "BUILT UTC    $builtAt",
        'COMPILER     MSVC VIA scripts/qualify-msvc.ps1',
        'SIGNATURE    UNSIGNED WINDOWS BINARIES',
        '',
        'VERIFY SHA256SUMS.txt BEFORE EXECUTION'
    )
    [System.IO.File]::WriteAllLines(
        (Join-Path $packageDir 'BUILD-INFO.txt'),
        $buildInfo,
        [System.Text.UTF8Encoding]::new($false)
    )

    $packageHashes = foreach ($file in Get-ChildItem -LiteralPath $packageDir -File -Recurse | Sort-Object FullName) {
        $relative = $file.FullName.Substring($packageDir.Length + 1).Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $relative"
    }
    [System.IO.File]::WriteAllLines(
        (Join-Path $packageDir 'SHA256SUMS.txt'),
        $packageHashes,
        [System.Text.UTF8Encoding]::new($false)
    )

    $zipPath = Join-Path $OutputDir "$packageName.zip"
    Compress-Archive -Path (Join-Path $packageDir '*') -DestinationPath $zipPath -CompressionLevel Optimal

    $publicAssets = @(
        (Join-Path $OutputDir 'deadflash-gui.exe'),
        (Join-Path $OutputDir 'deadflash.exe'),
        (Join-Path $OutputDir 'deadflash-proof.exe'),
        $zipPath
    )
    $publicHashes = foreach ($file in $publicAssets) {
        $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $([System.IO.Path]::GetFileName($file))"
    }
    $publicChecksumPath = Join-Path $OutputDir 'SHA256SUMS.txt'
    [System.IO.File]::WriteAllLines(
        $publicChecksumPath,
        $publicHashes,
        [System.Text.UTF8Encoding]::new($false)
    )

    Write-Host ''
    Write-Host 'QUALIFIED RELEASE ASSETS'
    Get-ChildItem -LiteralPath $OutputDir -File | Sort-Object Name |
        Select-Object Name, Length |
        Format-Table -AutoSize

    Invoke-Checked 'CREATE ANNOTATED TAG' {
        git tag -a $tag $head -m "DEADFLASH $version"
    }

    try {
        Invoke-Checked 'PUSH RELEASE TAG' {
            git push origin $tag
        }

        $releaseNotes = Join-Path $sourceRoot '.github\release-notes-v1.0.0.md'
        $releaseArgs = @(
            'release', 'create', $tag,
            (Join-Path $OutputDir 'deadflash-gui.exe'),
            (Join-Path $OutputDir 'deadflash.exe'),
            (Join-Path $OutputDir 'deadflash-proof.exe'),
            $zipPath,
            $publicChecksumPath
        )
        foreach ($evidence in $evidenceFiles) {
            $releaseArgs += (Join-Path $OutputDir $evidence.Name)
        }
        $releaseArgs += @(
            '--repo', $repo,
            '--verify-tag',
            '--title', "DEADFLASH $version",
            '--notes-file', $releaseNotes,
            '--latest'
        )

        Invoke-Checked 'CREATE GITHUB RELEASE AND UPLOAD BINARIES' {
            gh @releaseArgs
        }
    } catch {
        Write-Host ''
        Write-Host 'RELEASE PUBLICATION FAILED'
        Write-Host "The tag may already be present on GitHub: $tag"
        Write-Host 'The qualified local assets were retained at:'
        Write-Host "    $OutputDir"
        Write-Host 'Fix the reported GitHub error and retry asset publication manually.'
        throw
    }

    Write-Host ''
    Write-Host 'RELEASE PUBLISHED'
    Write-Host "TAG     : $tag"
    Write-Host "COMMIT  : $head"
    Write-Host "URL     : https://github.com/$repo/releases/tag/$tag"
    Write-Host "ASSETS  : $OutputDir"
    Write-Host ''
    gh release view $tag --repo $repo
} finally {
    Pop-Location
}
