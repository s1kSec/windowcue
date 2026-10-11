[CmdletBinding()]
param(
    [string]$SourceIcon
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
if ([string]::IsNullOrWhiteSpace($SourceIcon)) {
    $SourceIcon = Join-Path (Split-Path -Parent $PSScriptRoot) 'WindowCue\Assets\WindowCue.ico'
}

# Decode the largest ICO entry explicitly. System.Drawing.Icon's default size
# selection otherwise tends to return a small icon, even from a multi-size ICO.
$iconBytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $SourceIcon).Path)
if ($iconBytes.Length -lt 22 -or [BitConverter]::ToUInt16($iconBytes, 0) -ne 0 -or
    [BitConverter]::ToUInt16($iconBytes, 2) -ne 1) {
    throw "Not a valid ICO file: $SourceIcon"
}
$entryCount = [BitConverter]::ToUInt16($iconBytes, 4)
if ($entryCount -eq 0 -or $iconBytes.Length -lt (6 + 16 * $entryCount)) {
    throw 'The ICO directory is empty or truncated.'
}
$entries = for ($index = 0; $index -lt $entryCount; $index++) {
    $offset = 6 + 16 * $index
    $width = [int]$iconBytes[$offset]
    $height = [int]$iconBytes[$offset + 1]
    if ($width -eq 0) { $width = 256 }
    if ($height -eq 0) { $height = 256 }
    [pscustomobject]@{
        DirectoryOffset = $offset
        Width = $width
        Height = $height
        Area = $width * $height
        BitDepth = [BitConverter]::ToUInt16($iconBytes, $offset + 6)
        Length = [BitConverter]::ToUInt32($iconBytes, $offset + 8)
        DataOffset = [BitConverter]::ToUInt32($iconBytes, $offset + 12)
    }
}
$largest = $entries | Sort-Object -Property Area, BitDepth -Descending | Select-Object -First 1
if ($largest.Length -eq 0 -or ([long]$largest.DataOffset + $largest.Length) -gt $iconBytes.Length) {
    throw 'The largest ICO image is empty or truncated.'
}
$payload = New-Object byte[] ([int]$largest.Length)
[Array]::Copy($iconBytes, [int]$largest.DataOffset, $payload, 0, $payload.Length)

$stream = $null
$decoded = $null
$source = $null
$nativeIcon = $null
try {
    if ($payload.Length -ge 8 -and $payload[0] -eq 137 -and $payload[1] -eq 80 -and
        $payload[2] -eq 78 -and $payload[3] -eq 71) {
        $stream = New-Object IO.MemoryStream(,$payload)
        $decoded = [Drawing.Image]::FromStream($stream)
        $source = New-Object Drawing.Bitmap($decoded)
    } else {
        # Wrap a DIB entry in a single-image ICO so GDI+ cannot pick another size.
        $singleIcon = New-Object byte[] (22 + $payload.Length)
        $singleIcon[2] = 1
        $singleIcon[4] = 1
        [Array]::Copy($iconBytes, [int]$largest.DirectoryOffset, $singleIcon, 6, 16)
        [Array]::Copy([BitConverter]::GetBytes([uint32]22), 0, $singleIcon, 18, 4)
        [Array]::Copy($payload, 0, $singleIcon, 22, $payload.Length)
        $stream = New-Object IO.MemoryStream(,$singleIcon)
        $nativeIcon = New-Object Drawing.Icon($stream, [Drawing.Size]::new($largest.Width, $largest.Height))
        $source = $nativeIcon.ToBitmap()
    }

    # Ignore transparent canvas padding when applying each asset's safe margin.
    $left = $source.Width
    $top = $source.Height
    $right = -1
    $bottom = -1
    for ($y = 0; $y -lt $source.Height; $y++) {
        for ($x = 0; $x -lt $source.Width; $x++) {
            if ($source.GetPixel($x, $y).A -gt 0) {
                $left = [Math]::Min($left, $x)
                $top = [Math]::Min($top, $y)
                $right = [Math]::Max($right, $x)
                $bottom = [Math]::Max($bottom, $y)
            }
        }
    }
    if ($right -lt $left -or $bottom -lt $top) { throw 'The source icon is fully transparent.' }
    $sourceBounds = [Drawing.Rectangle]::new($left, $top, $right - $left + 1, $bottom - $top + 1)

    $assetsDirectory = Join-Path $PSScriptRoot 'Assets'
    [void][IO.Directory]::CreateDirectory($assetsDirectory)
    $assets = @(
        @{ Name = 'StoreLogo.png'; Size = 50; ContentScale = 0.80 },
        @{ Name = 'Square44x44Logo.png'; Size = 44; ContentScale = 0.82 },
        @{ Name = 'Square150x150Logo.png'; Size = 150; ContentScale = 0.68 }
    )
    foreach ($size in @(16, 24, 32, 48, 256)) {
        $assets += @{ Name = "Square44x44Logo.targetsize-${size}_altform-unplated.png"; Size = $size; ContentScale = 0.94 }
    }

    foreach ($asset in $assets) {
        $bitmap = $null
        $graphics = $null
        $attributes = $null
        try {
            $bitmap = New-Object Drawing.Bitmap($asset.Size, $asset.Size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            $graphics.Clear([Drawing.Color]::Transparent)
            $graphics.CompositingMode = [Drawing.Drawing2D.CompositingMode]::SourceCopy
            $graphics.CompositingQuality = [Drawing.Drawing2D.CompositingQuality]::HighQuality
            $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::HighQuality
            $available = [Math]::Max(1, [Math]::Floor($asset.Size * $asset.ContentScale))
            $scale = [Math]::Min($available / $sourceBounds.Width, $available / $sourceBounds.Height)
            $drawWidth = [Math]::Max(1, [int][Math]::Round($sourceBounds.Width * $scale))
            $drawHeight = [Math]::Max(1, [int][Math]::Round($sourceBounds.Height * $scale))
            $destination = [Drawing.Rectangle]::new(
                [int][Math]::Floor(($asset.Size - $drawWidth) / 2),
                [int][Math]::Floor(($asset.Size - $drawHeight) / 2), $drawWidth, $drawHeight)
            $attributes = New-Object Drawing.Imaging.ImageAttributes
            $attributes.SetWrapMode([Drawing.Drawing2D.WrapMode]::TileFlipXY)
            $graphics.DrawImage($source, $destination, $sourceBounds.X, $sourceBounds.Y,
                $sourceBounds.Width, $sourceBounds.Height, [Drawing.GraphicsUnit]::Pixel, $attributes)
            $destinationPath = Join-Path $assetsDirectory $asset.Name
            $bitmap.Save($destinationPath, [Drawing.Imaging.ImageFormat]::Png)
            Write-Output ("{0} ({1}x{1}, transparent PNG)" -f $destinationPath, $asset.Size)
        } finally {
            if ($attributes) { $attributes.Dispose() }
            if ($graphics) { $graphics.Dispose() }
            if ($bitmap) { $bitmap.Dispose() }
        }
    }
    Write-Output ("Source: {0}, largest ICO image {1}x{2}, visible bounds {3}x{4}." -f
        $SourceIcon, $largest.Width, $largest.Height, $sourceBounds.Width, $sourceBounds.Height)
} finally {
    if ($source) { $source.Dispose() }
    if ($nativeIcon) { $nativeIcon.Dispose() }
    if ($decoded) { $decoded.Dispose() }
    if ($stream) { $stream.Dispose() }
}
