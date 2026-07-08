param(
    [string] $StartUrl = "https://www.google.com/?igu=1&hl=en&gbv=1",
    [int] $TimeoutSeconds = 300,
    [switch] $SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")
. (Join-Path $PSScriptRoot "visual-common.ps1")

function Wait-SerialAfter {
    param(
        [string] $LogPath,
        [string] $Pattern,
        [int] $StartLength,
        [int] $TimeoutMs
    )

    $Deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $Deadline) {
        $Text = Read-LeonOsSerialLog $LogPath
        if ($Text.Length -gt $StartLength) {
            $Tail = $Text.Substring($StartLength)
            if ($Tail.Contains($Pattern)) {
                return $Text
            }
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for serial proof after marker: $Pattern"
}

function Send-MonitorCommand {
    param(
        [System.IO.StreamWriter] $Writer,
        [System.IO.Stream] $Stream,
        [string] $Command,
        [int] $TimeoutMs = 3000
    )

    $Writer.WriteLine($Command)
    Wait-QemuMonitorPrompt $Stream $TimeoutMs
}

if (-not $SkipBuild.IsPresent) {
    & (Join-Path $PSScriptRoot "..\ports\netsurf\build-leonos-probe.ps1") `
        -StartUrl $StartUrl -Interactive | Write-Host
    & (Join-Path $PSScriptRoot "build32-image.ps1") | Write-Host
    & (Join-Path $PSScriptRoot "build32-hdd.ps1") | Write-Host
}

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$Qemu = Get-LeonOsQemu
$ImagePath = (Get-LeonOsImagePath "dist32\leonos32.img") -replace '"', '\"'
$HddPath = (Get-LeonOsImagePath "dist32\leonos32-hdd.img") -replace '"', '\"'
$SerialLog = Get-LeonOsSerialLogPath "LeonOS-NetSurf-Input-Test"
Remove-Item -LiteralPath $SerialLog -Force -ErrorAction SilentlyContinue
$SerialArg = Get-LeonOsQemuSerialFileArg $SerialLog

$Listener = [System.Net.Sockets.TcpListener]::new(
    [System.Net.IPAddress]::Loopback, 0)
$Listener.Start()
$MonitorPort = ([System.Net.IPEndPoint] $Listener.LocalEndpoint).Port
$Listener.Stop()

$StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
$StartInfo.FileName = $Qemu
$StartInfo.Arguments = "-name LeonOS-NetSurf-Input-Test -machine pc -cpu qemu32 -m 256M -drive file=`"$ImagePath`",format=raw,if=floppy -drive file=`"$HddPath`",format=raw,if=ide,index=0,media=disk -boot a -vga std -serial $SerialArg -display none -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -nic user,model=rtl8139 -no-reboot"
$StartInfo.UseShellExecute = $false
$StartInfo.RedirectStandardError = $true

$Process = [System.Diagnostics.Process]::Start($StartInfo)
$Client = $null
$Writer = $null

try {
    for ($Attempt = 0; $Attempt -lt 120 -and -not $Client; $Attempt += 1) {
        try {
            Start-Sleep -Milliseconds 150
            $Client = [System.Net.Sockets.TcpClient]::new(
                "127.0.0.1", $MonitorPort)
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

    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS shell ready" 90000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS cooperative scheduler OK" 90000
    Send-MonitorCommand $Writer $Stream "sendkey f11"
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS shell app=net" 90000

    $LoadedText = Wait-LeonOsSerialLog `
        $SerialLog "HTML REDRAW TEXT Google Search" ($TimeoutSeconds * 1000)
    if (-not $LoadedText.Contains("WINDOW TITLE WIN 0 STR Google")) {
        throw "Google did not render before input test."
    }

    $InputStartLength = (Read-LeonOsSerialLog $SerialLog).Length

    $InitialMouse = Get-LeonOsInitialMousePosition -Width 1920 -Height 1080
    Move-QemuMouseTo $Writer 774 494 -Step 80 `
        -FromX $InitialMouse.X -FromY $InitialMouse.Y
    Start-Sleep -Milliseconds 200
    Send-MonitorCommand $Writer $Stream "mouse_button 0x01"
    Start-Sleep -Milliseconds 220
    Move-QemuMouseTo $Writer 775 494 -Step 1 -FromX 774 -FromY 494
    Start-Sleep -Milliseconds 120
    Send-MonitorCommand $Writer $Stream "mouse_button 0"
    Start-Sleep -Milliseconds 120
    Move-QemuMouseTo $Writer 774 494 -Step 1 -FromX 775 -FromY 494

    $null = Wait-SerialAfter `
        $SerialLog "COMMAND CLICK PROOF LEONOS_EVENT CLICK WIN 0" `
        $InputStartLength 10000

    Send-MonitorCommand $Writer $Stream "quit"
    $Writer.Dispose()
    $Writer = $null
    $Client.Close()
    $Client = $null

    if (-not $Process.WaitForExit(10000)) {
        $Process.Kill()
        $Process.WaitForExit()
    }

    $Stderr = $Process.StandardError.ReadToEnd()
    if ($Stderr.Trim().Length -gt 0) {
        Write-Host "QEMU stderr (LeonOS-NetSurf-Input-Test):"
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

foreach ($Bad in @(
    "user app not found", "user app bad", "user app read failed",
    "LeonOS user heap exhausted", "NetSurf abort", "DIE ",
    "CPU exception", "PANIC", "NETSURF QUICKJS EXCEPTION",
    "NETSURF QUICKJS EVENT EXCEPTION", "WINDOW TRACK ARGS BAD",
    "WINDOW CLICK ARGS BAD", "WINDOW BUTTON BAD", "WINDOW KIND BAD")) {
    if ($Output -like "*$Bad*") {
        throw "NetSurf input test hit failure: $Bad"
    }
}

Write-Host "QEMU NetSurf input test passed: live Google search field click reached NetSurf."
Write-Host "Serial log: $SerialLog"
