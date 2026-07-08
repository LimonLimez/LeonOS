param(
    [ValidateSet("720p", "1080p")]
    [string] $Resolution = "1080p",
    [int] $TimeoutSeconds = 120
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")
. (Join-Path $PSScriptRoot "visual-common.ps1")

& (Join-Path $PSScriptRoot "build32-image.ps1") -Resolution $Resolution | Write-Host
& (Join-Path $PSScriptRoot "build32-hdd.ps1") -ExcludeNetSurf | Write-Host

$Qemu = Get-LeonOsQemu
$ImagePath = (Get-LeonOsImagePath "dist32\leonos32.img") -replace '"', '\"'
$HddPath = (Get-LeonOsImagePath "dist32\leonos32-hdd.img") -replace '"', '\"'
$SerialLog = Get-LeonOsSerialLogPath "test32-browser-images-$Resolution"
$PpmPath = Join-Path (Join-Path $PSScriptRoot "..\dist32") "test32-browser-images-$Resolution.ppm"
foreach ($Path in @($SerialLog, $PpmPath)) {
    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Force
    }
}
$SerialArg = Get-LeonOsQemuSerialFileArg $SerialLog

$Listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$Listener.Start()
$MonitorPort = ([System.Net.IPEndPoint] $Listener.LocalEndpoint).Port
$Listener.Stop()

$StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
$StartInfo.FileName = $Qemu
$StartInfo.Arguments = "-name LeonOS-BrowserImages-$Resolution -machine pc -cpu qemu32 -m 32M -drive file=`"$ImagePath`",format=raw,if=floppy -drive file=`"$HddPath`",format=raw,if=ide,index=0,media=disk -boot a -vga std -display none -serial $SerialArg -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -nic user,model=rtl8139 -no-reboot"
$StartInfo.UseShellExecute = $false
$StartInfo.RedirectStandardError = $true

$Process = [System.Diagnostics.Process]::Start($StartInfo)
$Client = $null
$Writer = $null

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

    $Stream = $Client.GetStream()
    $Writer = [System.IO.StreamWriter]::new($Stream)
    $Writer.AutoFlush = $true
    Wait-QemuMonitorPrompt $Stream 8000

    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS shell ready" 60000
    $Writer.WriteLine("sendkey f11")
    Wait-QemuMonitorPrompt $Stream 3000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS shell app=net" 10000
    $Writer.WriteLine("sendkey g")
    Wait-QemuMonitorPrompt $Stream 3000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser image selftest open" 10000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser image PNG decoded" 10000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser image JPEG decoded" 10000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser image unsupported" 10000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser image summary decoded 2 unsupported 1 png 1 jpeg 1" 10000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser response complete fixture" 10000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser layout boxes" 10000

    Start-Sleep -Seconds 2
    $Writer.WriteLine("screendump $PpmPath")
    Start-Sleep -Seconds 1
    $Writer.WriteLine("quit")
    $Writer.Dispose()
    $Writer = $null
    $Client.Close()
    $Client = $null

    $Exited = $Process.WaitForExit($TimeoutSeconds * 1000)
    if (-not $Exited) {
        $Process.Kill()
        $Process.WaitForExit()
    }

    $Stderr = $Process.StandardError.ReadToEnd()
    if ($Stderr.Trim().Length -gt 0) {
        Write-Host "QEMU stderr (LeonOS-BrowserImages-$Resolution):"
        Write-Host $Stderr
    }
} finally {
    if ($Writer) { $Writer.Dispose() }
    if ($Client) { $Client.Close() }
    if (-not $Process.HasExited) {
        $Process.Kill()
        $Process.WaitForExit()
    }
}

$Output = Read-LeonOsSerialLog $SerialLog
Write-Host $Output

$Expected = @(
    "LeonOS net browser image selftest open",
    "LeonOS net browser image PNG decoded",
    "LeonOS net browser image JPEG decoded",
    "LeonOS net browser image unsupported",
    "LeonOS net browser image summary decoded 2 unsupported 1 png 1 jpeg 1",
    "LeonOS net browser response complete fixture",
    "LeonOS net browser layout boxes"
)
foreach ($Line in $Expected) {
    if ($Output -notlike "*$Line*") {
        throw "Missing browser image serial proof: $Line"
    }
}

$ImageSummary = [regex]::Match(
    $Output,
    'LeonOS net browser image summary decoded\s+(?<Decoded>\d+) unsupported (?<Unsupported>\d+) png (?<Png>\d+) jpeg (?<Jpeg>\d+)')
if (-not $ImageSummary.Success) {
    throw "Missing image summary counters."
}
foreach ($Name in @("Decoded", "Unsupported", "Png", "Jpeg")) {
    if ([int] $ImageSummary.Groups[$Name].Value -le 0) {
        throw "Image selftest expected $Name to be greater than zero."
    }
}

foreach ($Bad in @("CPU exception", "PANIC", "unhandled")) {
    if ($Output -like "*$Bad*") {
        throw "Browser image test hit fault: $Bad"
    }
}

if (-not (Test-Path -LiteralPath $PpmPath)) {
    throw "QEMU did not produce browser image screendump '$PpmPath'."
}

Write-Host "QEMU 32-bit browser image decode/render selftest passed ($Resolution)."
Write-Host "Serial log: $SerialLog"
Write-Host "Screendump: $PpmPath"
