param(
    [ValidateSet("1080p", "720p")]
    [string] $Resolution = "720p",
    [string] $OutDir = "dist32",
    [int] $TimeoutSeconds = 25
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")
. (Join-Path $PSScriptRoot "visual-common.ps1")

& (Join-Path $PSScriptRoot "build32-image.ps1") -Resolution $Resolution | Write-Host
& (Join-Path $PSScriptRoot "build32-hdd.ps1") | Write-Host

$Root = Split-Path -Parent $PSScriptRoot
$OutputRoot = if ([System.IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $Root $OutDir }
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null

$Qemu = Get-LeonOsQemu
$ImagePath = (Get-LeonOsImagePath "dist32\leonos32.img") -replace '"', '\"'
$HddPath = (Get-LeonOsImagePath "dist32\leonos32-hdd.img") -replace '"', '\"'
$PpmPath = Join-Path $OutputRoot "leonos32-start-menu-$Resolution.ppm"
$PngPath = Join-Path $OutputRoot "leonos32-start-menu-$Resolution-scaled.png"

$Listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$Listener.Start()
$MonitorPort = ([System.Net.IPEndPoint] $Listener.LocalEndpoint).Port
$Listener.Stop()

$StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
$StartInfo.FileName = $Qemu
$StartInfo.Arguments = "-name LeonOS-StartMenu-$Resolution -machine pc -cpu qemu32 -m 32M -drive file=`"$ImagePath`",format=raw,if=floppy -drive file=`"$HddPath`",format=raw,if=ide,index=0,media=disk -boot a -vga std -display none -serial none -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -no-reboot"
$StartInfo.UseShellExecute = $false
$StartInfo.RedirectStandardError = $true

$Process = [System.Diagnostics.Process]::Start($StartInfo)
$Client = $null

try {
    for ($Attempt = 0; $Attempt -lt 40 -and -not $Client; $Attempt += 1) {
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
    Wait-QemuMonitorPrompt $Stream 8000
    $Writer.WriteLine("sendkey s")
    Wait-QemuMonitorPrompt $Stream 3000
    Start-Sleep -Seconds 1
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
        Write-Host "QEMU stderr (LeonOS-StartMenu-$Resolution):"
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
    throw "QEMU did not produce start menu capture '$PpmPath'."
}

$Ppm = Read-Ppm $PpmPath
$PreviewW = if ($Resolution -eq "1080p") { 960 } else { 640 }
$PreviewH = if ($Resolution -eq "1080p") { 540 } else { 360 }
Save-ScaledPng $Ppm $PngPath $PreviewW $PreviewH
Write-Host "Start menu capture: $PngPath"
