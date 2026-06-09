param(
    [string] $Image = "dist32\leonos32.img",
    [string] $HddImage = "dist32\leonos32-hdd.img",
    [int] $TimeoutSeconds = 360,
    [string] $StartUrl = "https://www.google.com/?igu=1&hl=en&gbv=1",
    [switch] $RequireSubresource,
    [switch] $LiveSerial,
    [switch] $SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")

# Build the real NetSurf-port smoke app, place it on the FAT32 HDD image, boot
# LeonOS, then press 'm' to launch NETSURF.LEO from ring 3.
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "..\ports\netsurf\build-leonos-probe.ps1") `
        -StartUrl $StartUrl -Interactive | Write-Host
    & (Join-Path $PSScriptRoot "build32-image.ps1") | Write-Host
    & (Join-Path $PSScriptRoot "build32-hdd.ps1") | Write-Host
}

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$AppPath = Join-Path $Root "dist32\netsurf-probe\NETSURF.LEO"
$AppBytes = [System.IO.File]::ReadAllBytes($AppPath)
if ($AppBytes.Length -lt 32 -or
    $AppBytes[0] -ne 0x4C -or $AppBytes[1] -ne 0x45 -or
    $AppBytes[2] -ne 0x4F -or $AppBytes[3] -ne 0x31) {
    throw "NETSURF.LEO is missing the LEO1 header."
}
if ($AppBytes[4] -ne 0x02) {
    throw "NETSURF.LEO must declare LEO1 ABI version 2."
}

$Qemu = Get-LeonOsQemu
$ImagePath = Get-LeonOsImagePath $Image
$HddPath = Get-LeonOsImagePath $HddImage
$EscapedImagePath = $ImagePath -replace '"', '\"'
$EscapedHddPath = $HddPath -replace '"', '\"'
$SerialLog = Get-LeonOsSerialLogPath "LeonOS-NetSurf-LEO1-Test"
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
$StartInfo.Arguments = "-name LeonOS-NetSurf-LEO1-Test -machine pc -cpu qemu32 -m 256M -drive file=`"$EscapedImagePath`",format=raw,if=floppy -drive file=`"$EscapedHddPath`",format=raw,if=ide,index=0,media=disk -boot a -vga std -serial $SerialArg -display none -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -nic user,model=rtl8139 -no-reboot"
$StartInfo.UseShellExecute = $false
$StartInfo.RedirectStandardError = $true

$Process = [System.Diagnostics.Process]::Start($StartInfo)
$Client = $null
$LastLiveLength = 0

try {
    for ($Attempt = 0; $Attempt -lt 120 -and -not $Client; $Attempt++) {
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

    $BootText = Wait-LeonOsSerialLog $SerialLog "LeonOS shell ready" 90000
    if ($LiveSerial -and $BootText.Length -gt $LastLiveLength) {
        Write-Host $BootText.Substring($LastLiveLength)
        $LastLiveLength = $BootText.Length
    }
    if (-not $BootText.Contains("LeonOS shell ready")) {
        throw "QEMU did not reach LeonOS shell ready before launch timeout."
    }
    $SchedulerText = Wait-LeonOsSerialLog $SerialLog "LeonOS cooperative scheduler OK" 90000
    if ($LiveSerial -and $SchedulerText.Length -gt $LastLiveLength) {
        Write-Host $SchedulerText.Substring($LastLiveLength)
        $LastLiveLength = $SchedulerText.Length
    }
    if (-not $SchedulerText.Contains("LeonOS cooperative scheduler OK")) {
        throw "QEMU did not reach LeonOS cooperative scheduler before launch timeout."
    }

    $Writer.WriteLine("sendkey m")
    Wait-QemuMonitorPrompt $Stream 3000

    $SawFetchBytes = $false
    $SawFetchFinished = $false
    $SawRedrawAfterFetch = $false
    $Deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $Deadline -and -not $Process.HasExited) {
        $Text = Read-LeonOsSerialLog $SerialLog
        if ($LiveSerial -and $Text.Length -gt $LastLiveLength) {
            Write-Host $Text.Substring($LastLiveLength)
            $LastLiveLength = $Text.Length
        }
        if ($Text.Contains("NETSURF LEO HTTPS fetch bytes")) {
            $SawFetchBytes = $true
        }
        if ($Text.Contains("NETSURF LEO HTTPS fetch finished callback done")) {
            $SawFetchFinished = $true
        }
        if ($SawFetchFinished -and $Text.Contains("WINDOW REDRAW WIN 0 STOP")) {
            $SawRedrawAfterFetch = $true
        }
        if ($SawFetchBytes -and $SawFetchFinished -and $SawRedrawAfterFetch) {
            break
        }
        Start-Sleep -Milliseconds 100
    }

    $Writer.WriteLine("quit")
    $Writer.Dispose()
    $Client.Close()
    $Client = $null

    # The proof loop already spent the real timeout budget. Once the monitor
    # quit command is sent, give QEMU a short grace period and then tear it
    # down so failed tests do not double-wait or leave orphaned QEMU instances.
    $Exited = $Process.WaitForExit(10000)
    if (-not $Exited) {
        $Process.Kill()
        $Process.WaitForExit()
    }

    $Stdout = Read-LeonOsSerialLog $SerialLog
    $Stderr = $Process.StandardError.ReadToEnd()
    Write-Host "QEMU serial output (LeonOS-NetSurf-LEO1-Test):"
    Write-Host $Stdout
    if ($Stderr.Trim().Length -gt 0) {
        Write-Host "QEMU stderr:"
        Write-Host $Stderr
    }

    foreach ($Expected in @(
        "LeonOS stage2 VBE 1920x1080x32 ready",
        "LeonOS 32-bit FAT32 volume mounted",
        "LeonOS user mode enter NETSURF.LEO",
        "NETSURF.LEO NetSurf monkey frontend starting",
        "GENERIC JAVASCRIPT OPTION ENABLED",
        "NETSURF QUICKJS CORE HEAP",
        "GENERIC STARTED",
        "WINDOW NEW WIN 0",
        "WINDOW SET_URL WIN 0 URL $StartUrl",
        "NETSURF LEO HTTPS fetch begin $StartUrl",
        "NETSURF LEO HTTPS stream open handle 1",
        "LeonOS user net stream open $StartUrl",
        "LeonOS user net fetch done bytes",
        "LeonOS user net fetch meta status 200 type ",
        "LeonOS user net stream read bytes",
        "LeonOS user net fetch meta copy status 200",
        "LeonOS user net stream close",
        "NETSURF LEO HTTPS metadata status 200 type ",
        "NETSURF LEO HTTPS stream completed polls ",
        "NETSURF LEO HTTPS fetch bytes",
        "NETSURF LEO HTTPS fetch data callbacks begin",
        "NETSURF LEO HTTPS stream callback bytes ",
        "NETSURF LEO HTTPS fetch data callbacks done",
        "BEFORE_HTML",
        "IN_BODY",
        "WINDOW REDRAW WIN 0 START",
        "PLOT CLIP X0 0 Y0 0 X1 ",
        "PLOT RECT X0 ",
        "PLOT BITMAP X ",
        "WINDOW REDRAW WIN 0 STOP",
        "NETSURF QUICKJS EXEC ?inline script?",
        "HTML LEONOS DOM REBUILD DEFER ACTIVE",
        "HTML LEONOS DOM REBUILD REFLOW",
        "GENERIC LEONOS STB IMAGE DECODED"
    )) {
        if ($Stdout -notlike "*$Expected*") {
            throw "QEMU did not emit expected serial line: $Expected"
        }
    }

    foreach ($Bad in @(
        "user app not found", "user app bad", "user app read failed",
        "user app too large", "user app no allocator", "user app no memory",
        "LeonOS user heap exhausted",
        "NETSURF LEO HTTPS stream buffered Google compatibility",
        "NETSURF LEO HTTPS reduced real body excerpt",
        "NETSURF LEO HTTPS kept real body without unsupported head",
        "NETSURF LEO HTTPS strip unsupported",
        "user page range bad", "user syscall bad ptr", "user syscall bad number",
        "NetSurf abort", "DIE ", "CPU exception", "PANIC")) {
        if ($Stdout -like "*$Bad*") {
            throw "Kernel reported a NetSurf LEO1 app failure: $Bad"
        }
    }
    if ($RequireSubresource) {
        $FetchSetupCount = ([regex]::Matches($Stdout,
            "NETSURF LEO HTTPS fetch setup ")).Count
        if ($FetchSetupCount -lt 2) {
            throw "NetSurf did not start a real subresource fetch for $StartUrl."
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

Write-Host "test32-hdd-netsurf-qemu.ps1 passed."
