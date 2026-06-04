param(
    [string] $Image = "dist\leonos.img",
    [string] $OutDir = "dist",
    [int] $TimeoutSeconds = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")

& (Join-Path $PSScriptRoot "build.ps1") | Write-Host

$Root = Split-Path -Parent $PSScriptRoot
$OutputRoot = if ([System.IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $Root $OutDir }
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null

$Qemu = Get-LeonOsQemu
$ImagePath = Get-LeonOsImagePath $Image
$EscapedImagePath = $ImagePath -replace '"', '\"'
$PpmPath = Join-Path $OutputRoot "leonos-visual-test.ppm"
$PngPath = Join-Path $OutputRoot "leonos-visual-test-scaled.png"

function Read-Ppm {
    param([string] $Path)

    $Bytes = [System.IO.File]::ReadAllBytes($Path)
    $Index = 0

    function Next-Token {
        param([byte[]] $Data, [ref] $Cursor)

        while ($Cursor.Value -lt $Data.Length) {
            $Char = [char] $Data[$Cursor.Value]
            if ([char]::IsWhiteSpace($Char)) {
                $Cursor.Value += 1
                continue
            }
            if ($Char -eq "#") {
                while ($Cursor.Value -lt $Data.Length -and $Data[$Cursor.Value] -ne 10) {
                    $Cursor.Value += 1
                }
                continue
            }
            break
        }

        $Start = $Cursor.Value
        while ($Cursor.Value -lt $Data.Length -and -not [char]::IsWhiteSpace([char] $Data[$Cursor.Value])) {
            $Cursor.Value += 1
        }

        return [System.Text.Encoding]::ASCII.GetString($Data, $Start, $Cursor.Value - $Start)
    }

    $Magic = Next-Token $Bytes ([ref] $Index)
    if ($Magic -ne "P6") {
        throw "Unsupported PPM magic '$Magic'."
    }

    $Width = [int] (Next-Token $Bytes ([ref] $Index))
    $Height = [int] (Next-Token $Bytes ([ref] $Index))
    $Max = [int] (Next-Token $Bytes ([ref] $Index))
    if ($Max -ne 255) {
        throw "Unsupported PPM max value '$Max'."
    }

    while ($Index -lt $Bytes.Length -and [char]::IsWhiteSpace([char] $Bytes[$Index])) {
        $Index += 1
    }

    return [pscustomobject] @{
        Bytes = $Bytes
        Width = $Width
        Height = $Height
        DataOffset = $Index
    }
}

function Get-Pixel {
    param(
        [pscustomobject] $Ppm,
        [int] $X,
        [int] $Y
    )

    $Offset = $Ppm.DataOffset + (($Y * $Ppm.Width + $X) * 3)
    return [pscustomobject] @{
        R = [int] $Ppm.Bytes[$Offset]
        G = [int] $Ppm.Bytes[$Offset + 1]
        B = [int] $Ppm.Bytes[$Offset + 2]
    }
}

function Assert-PixelNear {
    param(
        [pscustomobject] $Ppm,
        [int] $X,
        [int] $Y,
        [int] $R,
        [int] $G,
        [int] $B,
        [int] $Tolerance,
        [string] $Name
    )

    $Pixel = Get-Pixel $Ppm $X $Y
    if ([Math]::Abs($Pixel.R - $R) -gt $Tolerance -or
        [Math]::Abs($Pixel.G - $G) -gt $Tolerance -or
        [Math]::Abs($Pixel.B - $B) -gt $Tolerance) {
        throw "$Name pixel at ($X,$Y) was RGB($($Pixel.R),$($Pixel.G),$($Pixel.B)); expected near RGB($R,$G,$B)."
    }
}

function Count-DarkPixels {
    param(
        [pscustomobject] $Ppm,
        [int] $X,
        [int] $Y,
        [int] $Width,
        [int] $Height
    )

    $Count = 0
    for ($Row = $Y; $Row -lt ($Y + $Height); $Row++) {
        for ($Col = $X; $Col -lt ($X + $Width); $Col++) {
            $Pixel = Get-Pixel $Ppm $Col $Row
            if (($Pixel.R + $Pixel.G + $Pixel.B) -lt 120) {
                $Count += 1
            }
        }
    }
    return $Count
}

function Save-ScaledPng {
    param(
        [pscustomobject] $Ppm,
        [string] $Path,
        [int] $OutWidth = 960,
        [int] $OutHeight = 540
    )

    Add-Type -AssemblyName System.Drawing
    $Bitmap = [System.Drawing.Bitmap]::new($OutWidth, $OutHeight, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    try {
        for ($Y = 0; $Y -lt $OutHeight; $Y++) {
            $SourceY = [Math]::Min($Ppm.Height - 1, [int] ($Y * $Ppm.Height / $OutHeight))
            for ($X = 0; $X -lt $OutWidth; $X++) {
                $SourceX = [Math]::Min($Ppm.Width - 1, [int] ($X * $Ppm.Width / $OutWidth))
                $Pixel = Get-Pixel $Ppm $SourceX $SourceY
                $Bitmap.SetPixel($X, $Y, [System.Drawing.Color]::FromArgb($Pixel.R, $Pixel.G, $Pixel.B))
            }
        }
        $Bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $Bitmap.Dispose()
    }
}

$Listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$Listener.Start()
$MonitorPort = ([System.Net.IPEndPoint] $Listener.LocalEndpoint).Port
$Listener.Stop()

$StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
$StartInfo.FileName = $Qemu
$StartInfo.Arguments = "-name LeonOS-VisualTest -machine pc -cpu qemu32 -m 16M -drive file=`"$EscapedImagePath`",format=raw,if=floppy -boot a -vga std -display none -serial none -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -no-reboot"
$StartInfo.UseShellExecute = $false
$StartInfo.RedirectStandardError = $true

$Process = [System.Diagnostics.Process]::Start($StartInfo)
$Client = $null

try {
    for ($Attempt = 0; $Attempt -lt 20 -and -not $Client; $Attempt++) {
        try {
            Start-Sleep -Milliseconds 150
            $Client = [System.Net.Sockets.TcpClient]::new("127.0.0.1", $MonitorPort)
        } catch {
            $Client = $null
        }
    }

    if (-not $Client) {
        throw "Could not connect to QEMU monitor on port $MonitorPort."
    }

    Start-Sleep -Seconds 8
    $Stream = $Client.GetStream()
    $Writer = [System.IO.StreamWriter]::new($Stream)
    $Writer.AutoFlush = $true
    $Writer.WriteLine("mouse_move 300 180")
    Start-Sleep -Milliseconds 200
    $Writer.WriteLine("mouse_move 180 80")
    Start-Sleep -Milliseconds 200
    $Writer.WriteLine("screendump $PpmPath")
    Start-Sleep -Seconds 1
    $Writer.WriteLine("quit")
    $Writer.Dispose()
    $Client.Close()
    $Client = $null

    $Exited = $Process.WaitForExit($TimeoutSeconds * 1000)
    if (-not $Exited) {
        $Process.Kill()
        $Process.WaitForExit()
    }

    $Stderr = $Process.StandardError.ReadToEnd()
    if ($Stderr.Trim().Length -gt 0) {
        Write-Host "QEMU stderr (LeonOS-VisualTest):"
        Write-Host $Stderr
    }
} finally {
    if ($Client) {
        $Client.Close()
    }
    if (-not $Process.HasExited) {
        $Process.Kill()
        $Process.WaitForExit()
    }
}

if (-not (Test-Path -LiteralPath $PpmPath)) {
    throw "QEMU did not produce visual capture '$PpmPath'."
}

$Ppm = Read-Ppm $PpmPath
if ($Ppm.Width -ne 1920 -or $Ppm.Height -ne 1080) {
    throw "Expected 1920x1080 capture; got $($Ppm.Width)x$($Ppm.Height)."
}

Assert-PixelNear $Ppm 100 100 19 65 119 8 "Wallpaper"
Assert-PixelNear $Ppm 340 120 255 255 255 8 "Window title bar"
Assert-PixelNear $Ppm 1580 120 232 17 35 8 "Close button"
Assert-PixelNear $Ppm 540 360 243 244 246 8 "Sidebar"
Assert-PixelNear $Ppm 620 260 255 255 255 8 "Content pane"
Assert-PixelNear $Ppm 32 1030 0 164 239 8 "Start button"

$TitleDarkPixels = Count-DarkPixels $Ppm 330 100 220 36
if ($TitleDarkPixels -lt 80) {
    throw "Title text area has too few dark pixels; text may not be rendering."
}

$ContentDarkPixels = Count-DarkPixels $Ppm 580 250 500 130
if ($ContentDarkPixels -lt 120) {
    throw "Content text area has too few dark pixels; preview text may not be rendering."
}

Save-ScaledPng $Ppm $PngPath
Write-Host "QEMU visual test passed."
Write-Host "Capture: $PpmPath"
Write-Host "Preview: $PngPath"
