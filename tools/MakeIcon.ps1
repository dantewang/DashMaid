# Draws the DashMaid logo (the logo of the prototype, 40 x 40 units) and
# writes src\Assets\DashMaid.ico with PNG images in the usual icon sizes.
# Run: pwsh -NoProfile -File tools\MakeIcon.ps1
Add-Type -AssemblyName System.Drawing

$sizes = 16, 20, 24, 32, 40, 48, 64, 256
$out = Join-Path $PSScriptRoot '..\src\Assets\DashMaid.ico'
New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null

function RoundRect([System.Drawing.Graphics]$g, [System.Drawing.Brush]$b, [double]$x, [double]$y, [double]$w, [double]$h, [double]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = [float]($r * 2)
    $p.AddArc([float]$x, [float]$y, $d, $d, 180, 90)
    $p.AddArc([float]($x + $w - $d), [float]$y, $d, $d, 270, 90)
    $p.AddArc([float]($x + $w - $d), [float]($y + $h - $d), $d, $d, 0, 90)
    $p.AddArc([float]$x, [float]($y + $h - $d), $d, $d, 90, 90)
    $p.CloseFigure()
    $g.FillPath($b, $p)
    $p.Dispose()
}

function Brush([string]$hex) { New-Object System.Drawing.SolidBrush ([System.Drawing.ColorTranslator]::FromHtml($hex)) }

$images = foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.PixelOffsetMode = 'HighQuality'
    $g.ScaleTransform($s / 40.0, $s / 40.0)
    RoundRect $g (Brush '#1b1b1b') 0 0 40 40 11
    $g.FillEllipse((Brush '#d7dde2'), [float]5.9, [float]16.4, [float]7.2, [float]7.2)
    RoundRect $g (Brush '#a3a3a3') 15 15 2.4 10 1.2
    RoundRect $g (Brush '#ffffff') 19.6 12.5 9 15 3
    RoundRect $g (Brush '#a3a3a3') 30.8 15 2.4 10 1.2
    RoundRect $g (Brush '#5a5a5a') 35 16.5 2 7 1
    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    , $ms.ToArray()
}

# ICO file: header, one directory entry per image, then the PNG data.
$fs = [System.IO.File]::Create($out)
$w = New-Object System.IO.BinaryWriter $fs
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]; $data = $images[$i]
    $w.Write([byte]($s % 256)); $w.Write([byte]($s % 256))  # 0 means 256.
    $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32)
    $w.Write([uint32]$data.Length); $w.Write([uint32]$offset)
    $offset += $data.Length
}
foreach ($data in $images) { $w.Write($data) }
$w.Dispose()
"Wrote $out"
