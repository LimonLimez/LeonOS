param(
    [string] $Image = "dist32\leonos32.img",
    [string] $HddImage = "dist32\leonos32-hdd.img",
    [ValidateSet("720p", "1080p")]
    [string] $Resolution = "1080p",
    [string] $StartUrl = "https://www.google.com/?igu=1&hl=en&gbv=1",
    [switch] $SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "..\ports\netsurf\build-leonos-probe.ps1") `
        -StartUrl $StartUrl -Interactive | Write-Host
    & (Join-Path $PSScriptRoot "build32-image.ps1") -Resolution $Resolution | Write-Host
    & (Join-Path $PSScriptRoot "build32-hdd.ps1") | Write-Host
}

$AppPath = Join-Path $Root "dist32\netsurf-probe\NETSURF.LEO"
if (-not (Test-Path -LiteralPath $AppPath)) {
    throw "NETSURF.LEO was not found at '$AppPath'. Run without -SkipBuild first."
}

$Qemu = Get-LeonOsQemu
$ImagePath = Get-LeonOsImagePath $Image
$HddPath = Get-LeonOsImagePath $HddImage
$SerialLog = Get-LeonOsSerialLogPath "LeonOS-NetSurf-Visible"
Remove-Item -LiteralPath $SerialLog -Force -ErrorAction SilentlyContinue

$Listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$Listener.Start()
$MonitorPort = ([System.Net.IPEndPoint] $Listener.LocalEndpoint).Port
$Listener.Stop()

$DisplayArg = if ($Resolution -eq "1080p") {
    "gtk,zoom-to-fit=off,show-menubar=off,grab-on-hover=on,show-cursor=on"
} else {
    "gtk,zoom-to-fit=on,show-menubar=off,grab-on-hover=on,show-cursor=on"
}

$Arguments = @(
    "-name", "LeonOS-NetSurf-Visible-$Resolution",
    "-machine", "pc",
    "-cpu", "qemu32",
    "-m", "256M",
    "-drive", "file=$ImagePath,format=raw,if=floppy",
    "-drive", "file=$HddPath,format=raw,if=ide,index=0,media=disk",
    "-boot", "a",
    "-vga", "std",
    "-display", $DisplayArg,
    "-serial", (Get-LeonOsQemuSerialFileArg $SerialLog),
    "-monitor", "tcp:127.0.0.1:$MonitorPort,server,nowait",
    "-nic", "user,model=rtl8139",
    "-no-reboot"
)

$Process = Start-Process -FilePath $Qemu -ArgumentList $Arguments `
    -WorkingDirectory $Root.Path `
    -PassThru

$Client = $null
try {
    for ($Attempt = 0; $Attempt -lt 120 -and -not $Client; $Attempt += 1) {
        try {
            Start-Sleep -Milliseconds 150
            $Client = [System.Net.Sockets.TcpClient]::new("127.0.0.1", $MonitorPort)
        } catch {
            $Client = $null
        }
    }
    if (-not $Client) {
        throw "Could not connect to visible QEMU monitor on port $MonitorPort."
    }

    $Stream = $Client.GetStream()
    $Writer = [System.IO.StreamWriter]::new($Stream)
    $Writer.AutoFlush = $true

    Wait-QemuMonitorPrompt $Stream 8000
    $BootText = Wait-LeonOsSerialLog $SerialLog "LeonOS shell ready" 90000
    if (-not $BootText.Contains("LeonOS shell ready")) {
        throw "QEMU did not reach LeonOS shell ready before launch timeout."
    }

    $SchedulerText = Wait-LeonOsSerialLog $SerialLog "LeonOS cooperative scheduler OK" 90000
    if (-not $SchedulerText.Contains("LeonOS cooperative scheduler OK")) {
        throw "QEMU did not reach LeonOS cooperative scheduler before launch timeout."
    }

    $Writer.WriteLine("sendkey f11")
    Wait-QemuMonitorPrompt $Stream 3000

    $LaunchText = Wait-LeonOsSerialLog $SerialLog "NETSURF.LEO NetSurf monkey frontend starting" 60000
    if (-not $LaunchText.Contains("NETSURF.LEO NetSurf monkey frontend starting")) {
        throw "NetSurf did not start before launch timeout."
    }
} finally {
    if ($Writer) {
        $Writer.Dispose()
    }
    if ($Client) {
        $Client.Close()
    }
}

Write-Host "Visible NetSurf QEMU PID: $($Process.Id)"
Write-Host "Serial log: $SerialLog"
Write-Host "Start URL baked into NETSURF.LEO: $StartUrl"
Write-Host "Close the QEMU window to stop."
