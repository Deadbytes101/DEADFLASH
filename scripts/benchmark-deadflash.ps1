param(
    [Parameter(Mandatory = $true)][string]$Deadflash,
    [Parameter(Mandatory = $true)][string]$Image,
    [Parameter(Mandatory = $true)][string]$Target,
    [int]$Runs = 5,
    [ValidateSet('none', 'sample', 'full')][string]$Verify = 'full',
    [string]$Token = '',
    [string]$OutDir = 'deadflash-benchmark'
)

$ErrorActionPreference = 'Stop'
if ($Runs -lt 1 -or $Runs -gt 100) { throw 'Runs must be between 1 and 100.' }

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$imageHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Image).Hash.ToLowerInvariant()
$rows = @()

for ($run = 1; $run -le $Runs; $run++) {
    $report = Join-Path $OutDir ('run-{0:D2}.json' -f $run)
    $args = @('write', $Image, $Target, '--verify', $Verify, '--report', $report)
    if ($Token) {
        $args += @('--allow-device', '--confirm', $Token)
    }

    & $Deadflash @args
    if ($LASTEXITCODE -ne 0) { throw "DEADFLASH failed on run $run." }

    $record = Get-Content -Raw -LiteralPath $report | ConvertFrom-Json
    if ($record.result.source_sha256 -ne $imageHash) {
        throw "Source hash mismatch in $report."
    }
    $rows += [pscustomobject]@{
        run = $run
        state = $record.result.state
        write_mib_s = [double]$record.result.write_mib_s
        total_ms = [double]$record.result.total_ms
        write_ms = [double]$record.result.write_ms
        flush_ms = [double]$record.result.flush_ms
        verify_ms = [double]$record.result.verify_ms
        bytes_written = [uint64]$record.result.bytes_written
        bytes_verified = [uint64]$record.result.bytes_verified
        retries = [uint64]$record.result.write_retries
        mismatches = [uint64]$record.result.verification_mismatches
    }
}

$csvPath = Join-Path $OutDir 'runs.csv'
$rows | Export-Csv -NoTypeInformation -Encoding utf8 -LiteralPath $csvPath

function Get-Stats([double[]]$Values) {
    $sorted = $Values | Sort-Object
    $count = $sorted.Count
    $median = if (($count % 2) -eq 1) {
        [double]$sorted[[int]($count / 2)]
    } else {
        ([double]$sorted[$count / 2 - 1] + [double]$sorted[$count / 2]) / 2.0
    }
    $mean = ($Values | Measure-Object -Average).Average
    $variance = if ($count -gt 1) {
        (($Values | ForEach-Object { [math]::Pow($_ - $mean, 2) } | Measure-Object -Sum).Sum) / ($count - 1)
    } else { 0.0 }
    [ordered]@{
        minimum = [double]$sorted[0]
        median = $median
        maximum = [double]$sorted[$count - 1]
        mean = [double]$mean
        sample_stddev = [math]::Sqrt($variance)
    }
}

$summary = [ordered]@{
    schema = 'deadflash.benchmark.summary.v1'
    created_utc = [DateTime]::UtcNow.ToString('o')
    image_sha256 = $imageHash
    target = $Target
    verify_mode = $Verify
    runs = $Runs
    write_mib_s = Get-Stats @($rows.write_mib_s)
    total_ms = Get-Stats @($rows.total_ms)
    states = @($rows | Group-Object state | Sort-Object Name | ForEach-Object {
        [ordered]@{ state = $_.Name; count = $_.Count }
    })
}

$summaryPath = Join-Path $OutDir 'summary.json'
$summary | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 -LiteralPath $summaryPath
$summary | ConvertTo-Json -Depth 8
