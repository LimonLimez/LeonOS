param(
    [string] $StartUrl = "https://www.google.com/?igu=1&hl=en&gbv=1",
    [int] $TimeoutSeconds = 300,
    [switch] $SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")
. (Join-Path $PSScriptRoot "visual-common.ps1")

function Wait-SerialContainsOrThrow {
    param(
        [string] $LogPath,
        [string] $Pattern,
        [int] $TimeoutMs
    )

    $Deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $Deadline) {
        $Text = Read-LeonOsSerialLog $LogPath
        if ($Text.Contains($Pattern)) {
            return $Text
        }
        Start-Sleep -Milliseconds 150
    }
    throw "Timed out waiting for serial proof: $Pattern"
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

function Move-TrackedMouse {
    param(
        [System.IO.StreamWriter] $Writer,
        [System.IO.Stream] $Stream,
        [int] $TargetX,
        [int] $TargetY,
        [ref] $MouseX,
        [ref] $MouseY
    )

    foreach ($Line in (Get-QemuRelativeMouseMoves `
            $TargetX $TargetY -FromX $MouseX.Value -FromY $MouseY.Value -Step 60)) {
        Send-MonitorCommand $Writer $Stream $Line
        Start-Sleep -Milliseconds 40
    }
    $MouseX.Value = $TargetX
    $MouseY.Value = $TargetY
}

function Click-TrackedMouse {
    param(
        [System.IO.StreamWriter] $Writer,
        [System.IO.Stream] $Stream
    )

    Send-MonitorCommand $Writer $Stream "mouse_button 0x01"
    Start-Sleep -Milliseconds 200
    Send-MonitorCommand $Writer $Stream "mouse_move 1 0"
    Start-Sleep -Milliseconds 120
    Send-MonitorCommand $Writer $Stream "mouse_button 0"
    Start-Sleep -Milliseconds 160
    Send-MonitorCommand $Writer $Stream "mouse_move -1 0"
    Start-Sleep -Milliseconds 160
}

if (-not $SkipBuild.IsPresent) {
    & (Join-Path $PSScriptRoot "..\ports\netsurf\build-leonos-probe.ps1") `
        -StartUrl $StartUrl -Interactive | Write-Host
    & (Join-Path $PSScriptRoot "build32-image.ps1") | Write-Host
    & (Join-Path $PSScriptRoot "build32-hdd.ps1") | Write-Host
}

$Qemu = Get-LeonOsQemu
$ImagePath = (Get-LeonOsImagePath "dist32\leonos32.img") -replace '"', '\"'
$HddPath = (Get-LeonOsImagePath "dist32\leonos32-hdd.img") -replace '"', '\"'
$SerialLog = Get-LeonOsSerialLogPath "LeonOS-NetSurf-ClickLaunch-Test"
Remove-Item -LiteralPath $SerialLog -Force -ErrorAction SilentlyContinue
$SerialArg = Get-LeonOsQemuSerialFileArg $SerialLog

$Listener = [System.Net.Sockets.TcpListener]::new(
    [System.Net.IPAddress]::Loopback, 0)
$Listener.Start()
$MonitorPort = ([System.Net.IPEndPoint] $Listener.LocalEndpoint).Port
$Listener.Stop()

$StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
$StartInfo.FileName = $Qemu
$StartInfo.Arguments = "-name LeonOS-NetSurf-ClickLaunch-Test -machine pc -cpu qemu32 -m 256M -drive file=`"$ImagePath`",format=raw,if=floppy -drive file=`"$HddPath`",format=raw,if=ide,index=0,media=disk -boot a -vga std -display none -serial $SerialArg -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -nic user,model=rtl8139 -no-reboot"
$StartInfo.UseShellExecute = $false
$StartInfo.RedirectStandardError = $true

$Process = [System.Diagnostics.Process]::Start($StartInfo)
$Client = $null
$Writer = $null

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
        throw "Could not connect to QEMU monitor on port $MonitorPort."
    }

    $Stream = $Client.GetStream()
    $Writer = [System.IO.StreamWriter]::new($Stream)
    $Writer.AutoFlush = $true
    Wait-QemuMonitorPrompt $Stream 8000

    $null = Wait-SerialContainsOrThrow `
        $SerialLog "LeonOS cooperative scheduler OK" 90000
    Send-MonitorCommand $Writer $Stream "sendkey f1"
    $null = Wait-SerialContainsOrThrow `
        $SerialLog "LeonOS shell start-menu open" 10000

    $InitialMouse = Get-LeonOsInitialMousePosition -Width 1920 -Height 1080
    $MouseX = $InitialMouse.X
    $MouseY = $InitialMouse.Y
    Move-TrackedMouse $Writer $Stream 120 905 ([ref] $MouseX) ([ref] $MouseY)
    Click-TrackedMouse $Writer $Stream

    $null = Wait-SerialContainsOrThrow `
        $SerialLog "LeonOS shell app=net" 10000
    $null = Wait-SerialContainsOrThrow `
        $SerialLog "NETSURF.LEO NetSurf monkey frontend starting" ($TimeoutSeconds * 1000)
    $null = Wait-SerialContainsOrThrow `
        $SerialLog "WINDOW NEW WIN 0" ($TimeoutSeconds * 1000)
    Start-Sleep -Seconds 12

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
        Write-Host "QEMU stderr (LeonOS-NetSurf-ClickLaunch-Test):"
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
    "ERROR WINDOW NUM BAD", "user app not found", "user app bad",
    "user app read failed", "LeonOS user heap exhausted", "NetSurf abort",
    "LeonOS user app returned to kernel", "DIE ", "CPU exception", "PANIC", "NETSURF QUICKJS EXCEPTION",
    "NETSURF QUICKJS EVENT EXCEPTION", "WINDOW TRACK ARGS BAD",
    "WINDOW CLICK ARGS BAD", "WINDOW BUTTON BAD", "WINDOW KIND BAD")) {
    if ($Output -like "*$Bad*") {
        throw "NetSurf click-launch test hit failure: $Bad"
    }
}

$MouseEvents = [regex]::Matches(
    $Output,
    "WINDOW LEONOS_EVENT (?:MOUSE|MOUSE_DOWN|CLICK) WIN \d+ X (-?\d+) Y (-?\d+)")
foreach ($Event in $MouseEvents) {
    $X = [int] $Event.Groups[1].Value
    $Y = [int] $Event.Groups[2].Value
    if ($X -lt 0 -or $Y -lt 0 -or $X -ge 800 -or $Y -ge 600) {
        throw "NetSurf click-launch test leaked an out-of-content mouse event: $($Event.Value)"
    }
}

Write-Host "QEMU NetSurf click-launch test passed: Start menu Browser click launched NetSurf cleanly."
Write-Host "Serial log: $SerialLog"
