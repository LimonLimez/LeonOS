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
    Send-MonitorCommand $Writer $Stream "sendkey m"

    $LoadedText = Wait-LeonOsSerialLog `
        $SerialLog "HTML REDRAW TEXT Google Search" ($TimeoutSeconds * 1000)
    if (-not $LoadedText.Contains("WINDOW TITLE WIN 0 STR Google")) {
        throw "Google did not render before input test."
    }

    $InputStartLength = (Read-LeonOsSerialLog $SerialLog).Length

    $InitialMouse = Get-LeonOsInitialMousePosition -Width 1920 -Height 1080
    Move-QemuMouseTo $Writer 700 370 -Step 80 `
        -FromX $InitialMouse.X -FromY $InitialMouse.Y
    Start-Sleep -Milliseconds 200
    Send-MonitorCommand $Writer $Stream "mouse_button 0x01"
    Start-Sleep -Milliseconds 80
    Send-MonitorCommand $Writer $Stream "mouse_button 0"

    $null = Wait-SerialAfter `
        $SerialLog "LEONOS_EVENT CLICK WIN 0" $InputStartLength 10000

    foreach ($Key in @("l", "e", "o", "n", "o", "s")) {
        Send-MonitorCommand $Writer $Stream "sendkey $Key" 1500
        Start-Sleep -Milliseconds 60
    }
    $AfterTyping = Wait-SerialAfter `
        $SerialLog "PLACE_CARET WIN 0 X 502 Y 267 HEIGHT 20" `
        $InputStartLength 10000
    if ($AfterTyping -notmatch "PLACE_CARET WIN 0 X 502 Y 267 HEIGHT 20") {
        throw "Google search input caret did not advance after typing leonos."
    }

    $EnterStartLength = (Read-LeonOsSerialLog $SerialLog).Length
    Send-MonitorCommand $Writer $Stream "sendkey ret"
    $AfterEnter = Wait-SerialAfter `
        $SerialLog "HTML FORM ENTER KEY" `
        $EnterStartLength 10000
    if ($AfterEnter -notmatch "WINDOW INVALIDATE_AREA WIN 0 X 420 Y 264 WIDTH 543 HEIGHT 26") {
        throw "Google search input did not redraw after Enter."
    }
    if ($AfterEnter -notmatch "HTML FORM ENTER KEY (10|13) TYPE 1 FORM 1 NAME q VALUE leonos ACTION /search") {
        throw "Google q input was not associated with the real search form on Enter."
    }
    if ($AfterEnter -notmatch "HTML FORM ENCODE .*q=leonos") {
        throw "Google search form did not encode q=leonos."
    }
    if ($AfterEnter -notmatch "HTML FORM NAVIGATE https://www\.google\.com/search\?.*q=leonos") {
        throw "Google search form did not navigate to a real /search URL."
    }
    if ($AfterEnter -notmatch "HTML FORM NAVIGATE RESULT 0") {
        throw "Google search form navigation did not return success."
    }
    if ($AfterEnter -notmatch "WINDOW SET_URL WIN 0 URL https://www\.google\.com/search\?.*q=leonos") {
        throw "NetSurf window URL did not switch to the Google search result URL."
    }

    $SearchFetchText = Wait-SerialAfter `
        $SerialLog "NETSURF LEO HTTPS fetch begin https://www.google.com/search?" `
        $InputStartLength 15000
    if ($SearchFetchText -notmatch "NETSURF LEO HTTPS fetch begin https://www\.google\.com/search\?.*q=leonos") {
        throw "NetSurf did not start fetching the submitted Google search URL."
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
    "NETSURF QUICKJS EVENT EXCEPTION")) {
    if ($Output -like "*$Bad*") {
        throw "NetSurf input test hit failure: $Bad"
    }
}

Write-Host "QEMU NetSurf input test passed: live Google search field focused, accepted leonos, and submitted to /search."
Write-Host "Serial log: $SerialLog"
