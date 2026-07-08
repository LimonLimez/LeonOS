param(
    [string] $Image = "dist\leonos.img",
    [string] $OutDir = "dist",
    [int] $TimeoutSeconds = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")
. (Join-Path $PSScriptRoot "visual-common.ps1")

& (Join-Path $PSScriptRoot "build.ps1") | Write-Host

$Root = Split-Path -Parent $PSScriptRoot
$OutputRoot = if ([System.IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $Root $OutDir }
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null

$Qemu = Get-LeonOsQemu
$ImagePath = Get-LeonOsImagePath $Image
$EscapedImagePath = $ImagePath -replace '"', '\"'
$PpmPath = Join-Path $OutputRoot "leonos-visual-test.ppm"
$PngPath = Join-Path $OutputRoot "leonos-visual-test-scaled.png"

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
