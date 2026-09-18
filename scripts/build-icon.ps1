# Development-only ICO conversion; the supplied artwork is never overwritten.
param([string]$Source = "$PSScriptRoot/../assets/branding/exilekit-source.png",
      [string]$Output = "$PSScriptRoot/../apps/toolbox/exilekit.ico")
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$sourceImage = [Drawing.Image]::FromFile((Resolve-Path -LiteralPath $Source).Path)
$sizes = @(16,20,24,28,32,40,48,56,64,96,128,256)
$frames = [Collections.Generic.List[byte[]]]::new()
$transparent = $null
try {
    if ($sourceImage.Width -ne $sourceImage.Height) { throw 'The source icon must be square.' }
    $transparent = [Drawing.Bitmap]::new($sourceImage)
    # The supplied RGB artwork has a near-black matte, including the three holes.
    # Change only alpha: retain the gold's RGB, geometry, texture and original padding.
    # Feather the matte transition before resampling to avoid a hard black outline.
    # Already-transparent replacement artwork keeps its supplied alpha unchanged.
    if (-not [Drawing.Image]::IsAlphaPixelFormat($sourceImage.PixelFormat)) {
        for ($y = 0; $y -lt $transparent.Height; ++$y) {
            for ($x = 0; $x -lt $transparent.Width; ++$x) {
                $pixel = $transparent.GetPixel($x, $y)
                $value = [Math]::Max($pixel.R, [Math]::Max($pixel.G, $pixel.B))
                $alpha = [int](255 * [Math]::Clamp(($value - 16) / 16.0, 0.0, 1.0))
                $transparent.SetPixel($x, $y, [Drawing.Color]::FromArgb($alpha, $pixel.R, $pixel.G, $pixel.B))
            }
        }
    }
    foreach ($size in $sizes) {
        $bitmap = [Drawing.Bitmap]::new($size,$size,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $stream = [IO.MemoryStream]::new()
        $attributes = [Drawing.Imaging.ImageAttributes]::new()
        try {
            $graphics.Clear([Drawing.Color]::Transparent)
            $graphics.CompositingMode = [Drawing.Drawing2D.CompositingMode]::SourceCopy
            $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            # Prevent GDI+ from sampling transparent black outside the source bounds.
            $attributes.SetWrapMode([Drawing.Drawing2D.WrapMode]::TileFlipXY)
            $graphics.DrawImage($transparent,[Drawing.Rectangle]::new(0,0,$size,$size),
                0,0,$transparent.Width,$transparent.Height,[Drawing.GraphicsUnit]::Pixel,$attributes)
            $bitmap.Save($stream,[Drawing.Imaging.ImageFormat]::Png)
            $frames.Add($stream.ToArray())
        } finally { $attributes.Dispose(); $stream.Dispose(); $graphics.Dispose(); $bitmap.Dispose() }
    }
} finally { if ($transparent) { $transparent.Dispose() }; $sourceImage.Dispose() }
$file = [IO.File]::Create([IO.Path]::GetFullPath($Output))
$writer = [IO.BinaryWriter]::new($file)
try {
    $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
    $offset = 6 + 16*$sizes.Count
    for ($index=0; $index -lt $sizes.Count; ++$index) {
        $dimension = if ($sizes[$index] -eq 256) {0} else {$sizes[$index]}
        $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
        $writer.Write([byte]0); $writer.Write([byte]0)
        $writer.Write([uint16]1); $writer.Write([uint16]32)
        $writer.Write([uint32]$frames[$index].Length); $writer.Write([uint32]$offset)
        $offset += $frames[$index].Length
    }
    foreach ($frame in $frames) { $writer.Write($frame) }
} finally { $writer.Dispose() }
Get-Item -LiteralPath $Output | Select-Object FullName,Length
