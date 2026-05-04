param(
    [switch]$SkipBuild,
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $repoRoot 'build'
$assetRoot = Join-Path $buildRoot 'social_preview'
$exePath = Join-Path $buildRoot 'CaseDash.exe'
$screenshotPath = Join-Path $assetRoot 'dark_cyan_fake_screenshot.png'
$iconPath = Join-Path $assetRoot 'dark_cyan_app_icon_256.png'

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $assetRoot 'casedash-social-preview.png'
}

Add-Type -AssemblyName System.Drawing

$canvasWidth = 1280
$canvasHeight = 640
$themeBackground = [Drawing.Color]::FromArgb(255, 0, 0, 0)
$themePanel = [Drawing.Color]::FromArgb(255, 1, 22, 32)
$themeAccent = [Drawing.Color]::FromArgb(255, 0, 207, 238)
$themeGuide = [Drawing.Color]::FromArgb(255, 0, 80, 128)
$themeWhite = [Drawing.Color]::FromArgb(255, 255, 255, 255)
$themeMuted = [Drawing.Color]::FromArgb(255, 224, 232, 234)
$themeDeep = $themeBackground

function Wait-GeneratedFile {
    param(
        [string]$Path,
        [string]$Description
    )

    for ($attempt = 0; $attempt -lt 20; ++$attempt) {
        $item = Get-Item -LiteralPath $Path -ErrorAction SilentlyContinue
        if ($null -ne $item -and $item.Length -gt 0) {
            return
        }
        Start-Sleep -Milliseconds 100
    }

    throw "CaseDash $Description export did not create $Path."
}

function New-RoundedRectanglePath {
    param(
        [Drawing.RectangleF]$Rect,
        [float]$Radius
    )

    $diameter = $Radius * 2.0
    $path = [Drawing.Drawing2D.GraphicsPath]::new()
    $path.AddArc($Rect.X, $Rect.Y, $diameter, $diameter, 180, 90)
    $path.AddArc($Rect.Right - $diameter, $Rect.Y, $diameter, $diameter, 270, 90)
    $path.AddArc($Rect.Right - $diameter, $Rect.Bottom - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc($Rect.X, $Rect.Bottom - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    return $path
}

function Draw-RoundedRectangle {
    param(
        [Drawing.Graphics]$Graphics,
        [Drawing.RectangleF]$Rect,
        [float]$Radius,
        [Drawing.Brush]$Brush,
        [Drawing.Pen]$Pen
    )

    $path = New-RoundedRectanglePath -Rect $Rect -Radius $Radius
    try {
        if ($null -ne $Brush) {
            $Graphics.FillPath($Brush, $path)
        }
        if ($null -ne $Pen) {
            $Graphics.DrawPath($Pen, $path)
        }
    } finally {
        $path.Dispose()
    }
}

function Draw-GuideLine {
    param(
        [Drawing.Graphics]$Graphics,
        [Drawing.PointF[]]$Points,
        [Drawing.Color]$Color,
        [float]$Width
    )

    $pen = [Drawing.Pen]::new($Color, $Width)
    try {
        $pen.LineJoin = [Drawing.Drawing2D.LineJoin]::Round
        $pen.StartCap = [Drawing.Drawing2D.LineCap]::Round
        $pen.EndCap = [Drawing.Drawing2D.LineCap]::Round
        $Graphics.DrawLines($pen, $Points)
    } finally {
        $pen.Dispose()
    }
}

function Export-AppAsset {
    param(
        [string[]]$Arguments,
        [string]$Description
    )

    $process = Start-Process -FilePath $exePath -ArgumentList $Arguments -Wait -PassThru -NoNewWindow
    if ($process.ExitCode -ne 0) {
        throw "CaseDash $Description export failed with exit code $($process.ExitCode)."
    }
}

if (-not $SkipBuild) {
    & (Join-Path $repoRoot 'build.cmd')
    if ($LASTEXITCODE -ne 0) {
        throw "build.cmd failed with exit code $LASTEXITCODE."
    }
}

if (-not (Test-Path -LiteralPath $exePath)) {
    throw "CaseDash executable was not found at $exePath. Run build.cmd first or omit -SkipBuild."
}

New-Item -ItemType Directory -Force -Path $assetRoot | Out-Null

Export-AppAsset -Arguments @('/fake', '/default-config', '/theme:dark_cyan', '/scale:2', "/screenshot:$screenshotPath", '/exit') -Description 'social preview screenshot'
Wait-GeneratedFile -Path $screenshotPath -Description 'social preview screenshot'

Export-AppAsset -Arguments @('/default-config', '/theme:dark_cyan', "/app-icon:$iconPath", '/app-icon-size:256', '/exit') -Description 'social preview app icon'
Wait-GeneratedFile -Path $iconPath -Description 'social preview app icon'

$bitmap = [Drawing.Bitmap]::new($canvasWidth, $canvasHeight, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
try {
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CompositingQuality = [Drawing.Drawing2D.CompositingQuality]::HighQuality
        $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.TextRenderingHint = [Drawing.Text.TextRenderingHint]::ClearTypeGridFit

        $backgroundBrush = [Drawing.Drawing2D.LinearGradientBrush]::new(
            [Drawing.Rectangle]::new(0, 0, $canvasWidth, $canvasHeight),
            $themeDeep,
            $themeBackground,
            [Drawing.Drawing2D.LinearGradientMode]::ForwardDiagonal)
        try {
            $graphics.FillRectangle($backgroundBrush, 0, 0, $canvasWidth, $canvasHeight)
        } finally {
            $backgroundBrush.Dispose()
        }

        $haloBrush = [Drawing.Drawing2D.PathGradientBrush]::new([Drawing.PointF[]]@(
            [Drawing.PointF]::new(950, 70),
            [Drawing.PointF]::new(1270, 330),
            [Drawing.PointF]::new(760, 620),
            [Drawing.PointF]::new(420, 250)
        ))
        try {
            $haloBrush.CenterColor = [Drawing.Color]::FromArgb(82, 0, 90, 128)
            $haloBrush.SurroundColors = [Drawing.Color[]]@(
                [Drawing.Color]::FromArgb(0, 0, 90, 128),
                [Drawing.Color]::FromArgb(0, 0, 90, 128),
                [Drawing.Color]::FromArgb(0, 0, 90, 128),
                [Drawing.Color]::FromArgb(0, 0, 90, 128)
            )
            $graphics.FillRectangle($haloBrush, [Drawing.Rectangle]::new(0, 0, $canvasWidth, $canvasHeight))
        } finally {
            $haloBrush.Dispose()
        }

        Draw-GuideLine -Graphics $graphics -Color ([Drawing.Color]::FromArgb(130, $themeGuide)) -Width 1.0 -Points @(
            [Drawing.PointF]::new(210, 202),
            [Drawing.PointF]::new(275, 202),
            [Drawing.PointF]::new(335, 150),
            [Drawing.PointF]::new(500, 150),
            [Drawing.PointF]::new(565, 98),
            [Drawing.PointF]::new(668, 98)
        )
        Draw-GuideLine -Graphics $graphics -Color ([Drawing.Color]::FromArgb(120, $themeGuide)) -Width 1.0 -Points @(
            [Drawing.PointF]::new(138, 500),
            [Drawing.PointF]::new(205, 442),
            [Drawing.PointF]::new(516, 442),
            [Drawing.PointF]::new(595, 390)
        )

        $dotBrush = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(210, $themeAccent))
        try {
            $graphics.FillEllipse($dotBrush, 273, 200, 4, 4)
            $graphics.FillEllipse($dotBrush, 421, 440, 4, 4)
        } finally {
            $dotBrush.Dispose()
        }

        $iconImage = [Drawing.Image]::FromFile($iconPath)
        try {
            $graphics.DrawImage($iconImage, [Drawing.Rectangle]::new(74, 104, 146, 146))
        } finally {
            $iconImage.Dispose()
        }

        $screenshotImage = [Drawing.Image]::FromFile($screenshotPath)
        try {
            $targetWidth = 704
            $targetHeight = [Math]::Round($targetWidth * $screenshotImage.Height / $screenshotImage.Width)
            if ($targetHeight -gt 500) {
                $targetHeight = 500
                $targetWidth = [Math]::Round($targetHeight * $screenshotImage.Width / $screenshotImage.Height)
            }
            $targetLeft = 520
            $targetTop = [Math]::Floor(($canvasHeight - $targetHeight) / 2) + 16
            $shadowRect = [Drawing.RectangleF]::new($targetLeft - 22, $targetTop - 20, $targetWidth + 44, $targetHeight + 46)
            $shadowBrush = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(132, 0, 0, 0))
            try {
                Draw-RoundedRectangle -Graphics $graphics -Rect $shadowRect -Radius 32 -Brush $shadowBrush -Pen $null
            } finally {
                $shadowBrush.Dispose()
            }

            $screenFrame = [Drawing.RectangleF]::new($targetLeft - 16, $targetTop - 16, $targetWidth + 32, $targetHeight + 32)
            $screenFrameBrush = [Drawing.SolidBrush]::new($themeBackground)
            $screenFramePen = [Drawing.Pen]::new($themeAccent, 1.6)
            try {
                Draw-RoundedRectangle -Graphics $graphics -Rect $screenFrame -Radius 28 -Brush $screenFrameBrush -Pen $screenFramePen
            } finally {
                $screenFrameBrush.Dispose()
                $screenFramePen.Dispose()
            }

            $graphics.DrawImage($screenshotImage, [Drawing.Rectangle]::new($targetLeft, $targetTop, $targetWidth, $targetHeight))
        } finally {
            $screenshotImage.Dispose()
        }

        $titleFont = [Drawing.Font]::new('Segoe UI', 72, [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
        $mottoFont = [Drawing.Font]::new('Segoe UI', 26, [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
        $linkFont = [Drawing.Font]::new('Segoe UI', 26, [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
        $whiteBrush = [Drawing.SolidBrush]::new($themeWhite)
        $mutedBrush = [Drawing.SolidBrush]::new($themeMuted)
        $accentBrush = [Drawing.SolidBrush]::new($themeAccent)
        $format = [Drawing.StringFormat]::new()
        try {
            $format.Alignment = [Drawing.StringAlignment]::Near
            $format.LineAlignment = [Drawing.StringAlignment]::Near
            $format.Trimming = [Drawing.StringTrimming]::None
            $graphics.DrawString('CaseDash', $titleFont, $whiteBrush, [Drawing.RectangleF]::new(64, 304, 430, 90), $format)
            $graphics.DrawString('Compact Windows dashboard', $mottoFont, $mutedBrush, [Drawing.RectangleF]::new(68, 406, 448, 38), $format)
            $graphics.DrawString('for dedicated PC screens', $mottoFont, $mutedBrush, [Drawing.RectangleF]::new(68, 452, 448, 38), $format)
            $graphics.DrawString('github.com/casedash/CaseDash', $linkFont, $accentBrush, [Drawing.RectangleF]::new(68, 558, 450, 40), $format)
        } finally {
            $format.Dispose()
            $accentBrush.Dispose()
            $mutedBrush.Dispose()
            $whiteBrush.Dispose()
            $linkFont.Dispose()
            $mottoFont.Dispose()
            $titleFont.Dispose()
        }
    } finally {
        $graphics.Dispose()
    }

    $outputDirectory = Split-Path -Parent $OutputPath
    if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
        New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
    }
    $bitmap.Save($OutputPath, [Drawing.Imaging.ImageFormat]::Png)
} finally {
    $bitmap.Dispose()
}

Write-Host "Generated $OutputPath from dark_cyan fake telemetry and rendered app icon assets."
