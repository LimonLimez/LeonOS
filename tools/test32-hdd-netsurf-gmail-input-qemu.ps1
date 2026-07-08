param(
    [string] $StartUrl = "https://mail.google.com/",
    [int] $TimeoutSeconds = 420,
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

function Try-WaitSerialAfter {
    param(
        [string] $LogPath,
        [string] $Pattern,
        [int] $StartLength,
        [int] $TimeoutMs
    )

    try {
        return Wait-SerialAfter $LogPath $Pattern $StartLength $TimeoutMs
    } catch {
        return $null
    }
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
$SerialLog = Get-LeonOsSerialLogPath "LeonOS-NetSurf-Gmail-Input-Test"
Remove-Item -LiteralPath $SerialLog -Force -ErrorAction SilentlyContinue
$SerialArg = Get-LeonOsQemuSerialFileArg $SerialLog

$Listener = [System.Net.Sockets.TcpListener]::new(
    [System.Net.IPAddress]::Loopback, 0)
$Listener.Start()
$MonitorPort = ([System.Net.IPEndPoint] $Listener.LocalEndpoint).Port
$Listener.Stop()

$StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
$StartInfo.FileName = $Qemu
$StartInfo.Arguments = "-name LeonOS-NetSurf-Gmail-Input-Test -machine pc -cpu qemu32 -m 256M -drive file=`"$ImagePath`",format=raw,if=floppy -drive file=`"$HddPath`",format=raw,if=ide,index=0,media=disk -boot a -vga std -serial $SerialArg -display none -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -nic user,model=rtl8139 -no-reboot"
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
    Send-MonitorCommand $Writer $Stream "sendkey n"

    $LoadedText = Wait-LeonOsSerialLog `
        $SerialLog "HTML REDRAW TEXT Email or phone" ($TimeoutSeconds * 1000)
    if (-not $LoadedText.Contains("WINDOW TITLE WIN 0 STR Gmail")) {
        throw "Gmail sign-in page did not render before input test."
    }
    $LoadedText = Wait-LeonOsSerialLog `
        $SerialLog "HTML REDRAW TEXT Next" ($TimeoutSeconds * 1000)
    if (-not $LoadedText.Contains("HTML REDRAW TEXT Next")) {
        throw "Gmail sign-in page did not expose the Next control."
    }
    $null = Wait-LeonOsSerialLog `
        $SerialLog "PLOT TEXT X 19 Y 214 LEN 4 STR Next" 30000
    $ReadyText = Wait-LeonOsSerialLog `
        $SerialLog "WINDOW PAGE_STATUS WIN 0 STATUS SECURE" 30000
    $null = Try-WaitSerialAfter `
        $SerialLog "HTML CSS LOADING EVENT SKIP" $ReadyText.Length 7000
    Start-Sleep -Seconds 2

    $InputStartLength = (Read-LeonOsSerialLog $SerialLog).Length

    $FocusedText = $null
    for ($Tab = 0; $Tab -lt 10 -and -not $FocusedText; $Tab += 1) {
        Send-MonitorCommand $Writer $Stream "sendkey tab" 1500
        Start-Sleep -Milliseconds 140
        $FocusedText = Try-WaitSerialAfter `
            $SerialLog "PLACE_CARET WIN 0" $InputStartLength 1500
    }
    if (-not $FocusedText) {
        throw "Gmail identifier field did not place a caret after Tab focus."
    }

    $TypingStartLength = (Read-LeonOsSerialLog $SerialLog).Length
    foreach ($Key in @("l", "e", "o", "n", "o", "s")) {
        Send-MonitorCommand $Writer $Stream "sendkey $Key" 1500
        Start-Sleep -Milliseconds 60
    }
    $AfterTyping = Wait-SerialAfter `
        $SerialLog "PLACE_CARET WIN 0" $TypingStartLength 10000
    if ($AfterTyping -notmatch "PLACE_CARET WIN 0") {
        throw "Gmail identifier input caret did not move after typing leonos."
    }

    $EnterStartLength = (Read-LeonOsSerialLog $SerialLog).Length
    Send-MonitorCommand $Writer $Stream "sendkey ret"
    $AfterEnter = Wait-SerialAfter `
        $SerialLog "HTML FORM ENTER KEY" `
        $EnterStartLength 10000
    if ($AfterEnter -notmatch "HTML FORM ENTER KEY (10|13).*VALUE leonos") {
        throw "Gmail identifier value was not attached to the real account form on Enter."
    }
    if ($AfterEnter -notmatch "HTML FORM (SUBMIT START|NAVIGATE|DATA)") {
        throw "Gmail identifier form did not produce any submit/data diagnostics."
    }

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
        Write-Host "QEMU stderr (LeonOS-NetSurf-Gmail-Input-Test):"
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
    "NETSURF QUICKJS EVENT EXCEPTION")) {
    if ($Output -like "*$Bad*") {
        throw "NetSurf Gmail input test hit failure: $Bad"
    }
}

Write-Host "QEMU NetSurf Gmail input test passed: Gmail sign-in identifier focused, accepted leonos, and produced form submit diagnostics."
Write-Host "Serial log: $SerialLog"
