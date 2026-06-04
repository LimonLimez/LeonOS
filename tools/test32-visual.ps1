param(
    [string] $Image = "dist32\leonos32.img",
    [string] $OutDir = "dist32",
    [ValidateSet("1080p", "720p", "768p", "1366")]
    [string] $Resolution = "1080p",
    [int] $TimeoutSeconds = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")
. (Join-Path $PSScriptRoot "visual-common.ps1")

& (Join-Path $PSScriptRoot "build32-image.ps1") -Resolution $Resolution | Write-Host

$Size = Get-LeonOsResolutionSize $Resolution
$Expect = Get-LeonOsVisualExpectations $Size.Width $Size.Height

$Root = Split-Path -Parent $PSScriptRoot
$OutputRoot = if ([System.IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $Root $OutDir }
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null

$Qemu = Get-LeonOsQemu
$ImagePath = Get-LeonOsImagePath $Image
$EscapedImagePath = $ImagePath -replace '"', '\"'
$Suffix = $Resolution
$PpmPath = Join-Path $OutputRoot "leonos32-visual-test-$Suffix.ppm"
$PngPath = Join-Path $OutputRoot "leonos32-visual-test-$Suffix-scaled.png"

$Listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$Listener.Start()
$MonitorPort = ([System.Net.IPEndPoint] $Listener.LocalEndpoint).Port
$Listener.Stop()

$StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
$StartInfo.FileName = $Qemu
$StartInfo.Arguments = "-name LeonOS-32BitVisual-$Suffix -machine pc -cpu qemu32 -m 32M -drive file=`"$EscapedImagePath`",format=raw,if=floppy -boot a -vga std -display none -serial none -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -no-reboot"
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
    $InitialMouse = Get-LeonOsInitialMousePosition $Size.Width $Size.Height
    Move-QemuMouseTo $Writer $Expect.CursorTargetX $Expect.CursorTargetY -Step 60 -FromX $InitialMouse.X -FromY $InitialMouse.Y
    Start-Sleep -Seconds 2
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
        Write-Host "QEMU stderr (LeonOS-32BitVisual-$Suffix):"
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
Assert-LeonOsVisualCapture $Ppm $Expect $Size.Width $Size.Height

$PreviewH = [Math]::Max(270, [int](540 * $Size.Height / 1080))
$PreviewW = [Math]::Max(480, [int](960 * $Size.Width / 1920))
Save-ScaledPng $Ppm $PngPath $PreviewW $PreviewH

Write-Host "QEMU 32-bit visual test passed ($Resolution / $($Size.Width)x$($Size.Height))."
Write-Host "Capture: $PpmPath"
Write-Host "Preview: $PngPath"
