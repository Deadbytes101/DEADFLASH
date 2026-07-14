param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

$destination = [System.IO.Path]::GetFullPath($OutputPath)
$directory = [System.IO.Path]::GetDirectoryName($destination)
if ([string]::IsNullOrWhiteSpace($directory)) {
    throw "Could not resolve icon output directory: $destination"
}
[System.IO.Directory]::CreateDirectory($directory) | Out-Null

function Scale-IconValue {
    param([int]$Value, [int]$Size)
    return [int][Math]::Round(($Value * $Size) / 256.0)
}

function New-ScaledPointArray {
    param([int[]]$Coordinates, [int]$Size)
    if (($Coordinates.Count % 2) -ne 0) {
        throw 'Point coordinate list must contain x/y pairs.'
    }
    $count = [int]($Coordinates.Count / 2)
    $points = [System.Drawing.Point[]]::new($count)
    for ($index = 0; $index -lt $Coordinates.Count; $index += 2) {
        $points[[int]($index / 2)] = [System.Drawing.Point]::new(
            (Scale-IconValue $Coordinates[$index] $Size),
            (Scale-IconValue $Coordinates[$index + 1] $Size)
        )
    }
    return ,$points
}

function New-DeadflashBitmap {
    param([int]$Size)

    $bitmap = [System.Drawing.Bitmap]::new(
        $Size,
        $Size,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
    )
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::None
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $graphics.Clear([System.Drawing.Color]::Transparent)

    $black = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 8, 10, 14))
    $blue = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 0, 65, 181))
    $blueDark = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 0, 38, 112))
    $yellow = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 255, 212, 55))
    $silver = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 205, 210, 216))
    $silverDark = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 112, 120, 132))
    $white = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 245, 245, 235))

    try {
        $rectangles = @(
            @($black, 80, 12, 96, 70),
            @($silver, 92, 24, 72, 46),
            @($black, 102, 34, 16, 20),
            @($black, 138, 34, 16, 20),
            @($silverDark, 92, 62, 72, 8),
            @($black, 70, 70, 116, 34),
            @($blueDark, 82, 78, 92, 26),
            @($blueDark, 42, 114, 12, 82),
            @($black, 62, 122, 16, 16),
            @($black, 62, 146, 16, 16),
            @($white, 66, 126, 8, 8),
            @($yellow, 66, 150, 8, 8)
        )
        foreach ($rectangle in $rectangles) {
            $graphics.FillRectangle(
                [System.Drawing.Brush]$rectangle[0],
                (Scale-IconValue $rectangle[1] $Size),
                (Scale-IconValue $rectangle[2] $Size),
                [Math]::Max(1, (Scale-IconValue $rectangle[3] $Size)),
                [Math]::Max(1, (Scale-IconValue $rectangle[4] $Size))
            )
        }

        [System.Drawing.Point[]]$outer = New-ScaledPointArray @(50,90, 206,90, 226,110, 226,218, 204,240, 52,240, 30,218, 30,110) $Size
        [System.Drawing.Point[]]$inner = New-ScaledPointArray @(60,102, 196,102, 214,114, 214,210, 196,228, 60,228, 42,210, 42,114) $Size
        [System.Drawing.Point[]]$shadow = New-ScaledPointArray @(42,196, 214,196, 214,210, 196,228, 60,228, 42,210) $Size
        [System.Drawing.Point[]]$boltOutline = New-ScaledPointArray @(142,112, 166,112, 140,148, 172,148, 102,218, 124,166, 94,166) $Size
        [System.Drawing.Point[]]$bolt = New-ScaledPointArray @(144,120, 154,120, 130,156, 158,156, 118,200, 136,158, 108,158) $Size

        $graphics.FillPolygon($black, $outer)
        $graphics.FillPolygon($blue, $inner)
        $graphics.FillPolygon($blueDark, $shadow)
        $graphics.FillPolygon($black, $boltOutline)
        $graphics.FillPolygon($yellow, $bolt)
    } finally {
        $black.Dispose()
        $blue.Dispose()
        $blueDark.Dispose()
        $yellow.Dispose()
        $silver.Dispose()
        $silverDark.Dispose()
        $white.Dispose()
        $graphics.Dispose()
    }
    return $bitmap
}

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$images = @()
try {
    foreach ($size in $sizes) {
        $bitmap = New-DeadflashBitmap $size
        $stream = [System.IO.MemoryStream]::new()
        try {
            $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
            $images += [PSCustomObject]@{
                Size = $size
                Bytes = $stream.ToArray()
            }
        } finally {
            $stream.Dispose()
            $bitmap.Dispose()
        }
    }

    $temp = "$destination.tmp-$PID"
    $file = [System.IO.File]::Open(
        $temp,
        [System.IO.FileMode]::Create,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::None
    )
    $writer = [System.IO.BinaryWriter]::new($file)
    try {
        $writer.Write([UInt16]0)
        $writer.Write([UInt16]1)
        $writer.Write([UInt16]$images.Count)

        [UInt32]$offset = 6 + (16 * $images.Count)
        foreach ($image in $images) {
            $dimension = if ($image.Size -eq 256) { 0 } else { $image.Size }
            $writer.Write([Byte]$dimension)
            $writer.Write([Byte]$dimension)
            $writer.Write([Byte]0)
            $writer.Write([Byte]0)
            $writer.Write([UInt16]1)
            $writer.Write([UInt16]32)
            $writer.Write([UInt32]$image.Bytes.Length)
            $writer.Write([UInt32]$offset)
            $offset += [UInt32]$image.Bytes.Length
        }
        foreach ($image in $images) {
            $writer.Write([Byte[]]$image.Bytes)
        }
    } finally {
        $writer.Dispose()
        $file.Dispose()
    }

    Move-Item -LiteralPath $temp -Destination $destination -Force
} finally {
    Remove-Item -LiteralPath "$destination.tmp-$PID" -Force -ErrorAction SilentlyContinue
}

$bytes = [System.IO.File]::ReadAllBytes($destination)
if ($bytes.Length -lt 256 -or
    $bytes[0] -ne 0 -or $bytes[1] -ne 0 -or
    $bytes[2] -ne 1 -or $bytes[3] -ne 0) {
    throw 'Generated DEADFLASH icon failed ICO validation.'
}

Write-Host "[PASS] generated DEADFLASH USB flash-drive icon: $destination"
