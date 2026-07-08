param(
    [string] $Image = "dist32\leonos32.img",
    [string] $HddImage = "dist32\leonos32-hdd.img",
    [int] $TimeoutSeconds = 120
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")

& (Join-Path $PSScriptRoot "build32-image.ps1") | Write-Host
& (Join-Path $PSScriptRoot "build32-hdd.ps1") | Write-Host

$Qemu = Get-LeonOsQemu
$ImagePath = Get-LeonOsImagePath $Image
$HddPath = Get-LeonOsImagePath $HddImage
$EscapedImagePath = $ImagePath -replace '"', '\"'
$EscapedHddPath = $HddPath -replace '"', '\"'
$SerialLog = Get-LeonOsSerialLogPath "LeonOS-UNetRun-Test"
if (Test-Path -LiteralPath $SerialLog) {
    Remove-Item -LiteralPath $SerialLog -Force
}
$SerialArg = Get-LeonOsQemuSerialFileArg $SerialLog

$Listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$Listener.Start()
$MonitorPort = ([System.Net.IPEndPoint] $Listener.LocalEndpoint).Port
$Listener.Stop()

$StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
$StartInfo.FileName = $Qemu
$StartInfo.Arguments = "-name LeonOS-UNetRun-Test -machine pc -cpu qemu32 -m 32M -drive file=`"$EscapedImagePath`",format=raw,if=floppy -drive file=`"$EscapedHddPath`",format=raw,if=ide,index=0,media=disk -boot a -serial $SerialArg -display none -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -nic user,model=rtl8139 -no-reboot"
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
    if (-not $Client) { throw "Could not connect to QEMU monitor." }

    $Stream = $Client.GetStream()
    $Writer = [System.IO.StreamWriter]::new($Stream)
    $Writer.AutoFlush = $true

    # Wait for the desktop shell before injecting the hotkey so a slow boot
    # cannot swallow it.
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS shell ready" 60000
    Start-Sleep -Seconds 1
    $Writer.WriteLine("sendkey n")
    Wait-QemuMonitorPrompt $Stream 3000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser URL open" ($TimeoutSeconds * 1000)
} finally {
    if ($Client) { $Client.Close() }
    if (-not $Process.HasExited) {
        $Process.Kill()
        $Process.WaitForExit()
    }
}

$Text = Read-LeonOsSerialLog $SerialLog
$Stderr = $Process.StandardError.ReadToEnd()
Write-Host "QEMU serial output (LeonOS-UNetRun-Test):"
Write-Host $Text
Write-Host "Serial log: $SerialLog"
if ($Stderr.Trim().Length -gt 0) {
    Write-Host "QEMU stderr:"
    Write-Host $Stderr
}
$Needles = @(
    "UNETRUN NetSurf port runtime",
    "UNETRUN runtime ticks",
    "LeonOS user browser open queued",
    "LeonOS net browser URL open"
)
foreach ($Needle in $Needles) {
    if (-not $Text.Contains($Needle)) {
        throw "Missing serial proof '$Needle'. Output:`n$Text"
    }
}
Write-Host "test32-hdd-unetsurf-qemu.ps1 passed."