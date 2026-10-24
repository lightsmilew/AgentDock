$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$bitmap = New-Object System.Drawing.Bitmap(256, 256)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$shape = New-Object System.Drawing.Drawing2D.GraphicsPath
$shape.AddArc(0, 0, 112, 112, 180, 90)
$shape.AddArc(144, 0, 112, 112, 270, 90)
$shape.AddArc(144, 144, 112, 112, 0, 90)
$shape.AddArc(0, 144, 112, 112, 90, 90)
$shape.CloseFigure()
$dark = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml('#2F6FED'))
$mint = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml('#FFFFFF'))
$graphics.FillPath($dark, $shape)
$letter = New-Object System.Drawing.Drawing2D.GraphicsPath
$letter.AddPolygon([System.Drawing.PointF[]]@([System.Drawing.PointF]::new(53, 184), [System.Drawing.PointF]::new(104, 74), [System.Drawing.PointF]::new(144, 74), [System.Drawing.PointF]::new(195, 184), [System.Drawing.PointF]::new(151, 184), [System.Drawing.PointF]::new(141, 159), [System.Drawing.PointF]::new(105, 159), [System.Drawing.PointF]::new(96, 184)))
$graphics.FillPath($mint, $letter)
$graphics.FillPolygon($dark, [System.Drawing.PointF[]]@([System.Drawing.PointF]::new(116, 138), [System.Drawing.PointF]::new(123, 115), [System.Drawing.PointF]::new(131, 138)))
$graphics.FillEllipse($mint, 180, 48, 31, 31)
$memory = New-Object System.IO.MemoryStream
$bitmap.Save($memory, [System.Drawing.Imaging.ImageFormat]::Png)
$png = $memory.ToArray()
$outputPath = Join-Path (Split-Path -Parent $PSScriptRoot) 'assets\icon.ico'
$stream = [System.IO.File]::Create($outputPath)
$writer = New-Object System.IO.BinaryWriter($stream)
$writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]1)
$writer.Write([byte]0); $writer.Write([byte]0); $writer.Write([byte]0); $writer.Write([byte]0)
$writer.Write([uint16]1); $writer.Write([uint16]32); $writer.Write([uint32]$png.Length); $writer.Write([uint32]22)
$writer.Write($png)
$writer.Dispose(); $stream.Dispose(); $memory.Dispose(); $graphics.Dispose(); $bitmap.Dispose(); $shape.Dispose(); $letter.Dispose(); $dark.Dispose(); $mint.Dispose()
Write-Output $outputPath
