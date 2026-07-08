param(
    [string] $Image = "dist32\leonos32.img",
    [string] $HddImage = "dist32\leonos32-hdd.img",
    [int] $TimeoutSeconds = 120
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")

& (Join-Path $PSScriptRoot "..\ports\quickjs\build-quickjs-probe.ps1") | Write-Host
& (Join-Path $PSScriptRoot "build32-image.ps1") | Write-Host
& (Join-Path $PSScriptRoot "build32-hdd.ps1") | Write-Host

$Qemu = Get-LeonOsQemu
$ImagePath = Get-LeonOsImagePath $Image
$HddPath = Get-LeonOsImagePath $HddImage
$EscapedImagePath = $ImagePath -replace '"', '\"'
$EscapedHddPath = $HddPath -replace '"', '\"'
$SerialLog = Get-LeonOsSerialLogPath "LeonOS-UQuickJS-Test"
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
$StartInfo.Arguments = "-name LeonOS-UQuickJS-Test -machine pc -cpu qemu32 -m 64M -drive file=`"$EscapedImagePath`",format=raw,if=floppy -drive file=`"$EscapedHddPath`",format=raw,if=ide,index=0,media=disk -boot a -serial $SerialArg -display none -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -no-reboot"
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
        throw "Could not connect to QEMU monitor."
    }

    $Stream = $Client.GetStream()
    $Writer = [System.IO.StreamWriter]::new($Stream)
    $Writer.AutoFlush = $true

    # Wait for the desktop shell before injecting the hotkey so a slow boot
    # cannot swallow it.
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS shell ready" 60000
    Start-Sleep -Seconds 1
    $Writer.WriteLine("sendkey j")
    Wait-QemuMonitorPrompt $Stream 3000
    $null = Wait-LeonOsSerialLog $SerialLog "UQJS OK modern JavaScript core" ($TimeoutSeconds * 1000)
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS user app returned to kernel" 15000

    $Writer.WriteLine("quit")
    $Writer.Dispose()
    $Client.Close()
    $Client = $null

    $Exited = $Process.WaitForExit(10000)
    if (-not $Exited) {
        $Process.Kill()
        $Process.WaitForExit()
    }

    $Text = Read-LeonOsSerialLog $SerialLog
    $Stderr = $Process.StandardError.ReadToEnd()
    Write-Host "QEMU serial output (LeonOS-UQuickJS-Test):"
    Write-Host $Text
    Write-Host "Serial log: $SerialLog"
    if ($Stderr.Trim().Length -gt 0) {
        Write-Host "QEMU stderr:"
        Write-Host $Stderr
    }

    foreach ($Needle in @(
        "LeonOS stage2 VBE 1920x1080x32 ready",
        "LeonOS 32-bit FAT32 volume mounted",
        "LeonOS user mode enter UQJS.LEO",
        "UQJS QuickJS proof app",
        "UQJS eval modern-js-1",
        "UQJS result 14",
        "UQJS eval modern-js-2",
        "UQJS result 42",
        "UQJS eval modern-js-3",
        "UQJS OK modern JavaScript core",
        "LeonOS user app returned to kernel"
    )) {
        if (-not $Text.Contains($Needle)) {
            throw "Missing serial proof '$Needle'. Output:`n$Text"
        }
    }

    foreach ($Bad in @(
        "user app not found", "user app bad", "user app read failed",
        "user app no memory", "user page range bad", "user syscall bad number",
        "UQJS runtime alloc failed", "UQJS context alloc failed",
        "UQJS exception", "UQJS FAIL", "CPU exception", "PANIC")) {
        if ($Text.Contains($Bad)) {
            throw "Kernel reported a UQJS failure: $Bad"
        }
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

Write-Host "test32-hdd-uquickjs-qemu.ps1 passed."