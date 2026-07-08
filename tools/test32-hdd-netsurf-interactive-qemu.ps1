param(
    [string] $Image = "dist32\leonos32.img",
    [string] $HddImage = "dist32\leonos32-hdd.img",
    [int] $TimeoutSeconds = 360,
    [string] $StartUrl = "https://www.google.com/?igu=1&hl=en&gbv=1",
    [string] $TypedUrl = "www.google.com",
    [switch] $SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")
. (Join-Path $PSScriptRoot "visual-common.ps1")

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "..\ports\netsurf\build-leonos-probe.ps1") `
        -StartUrl $StartUrl -SettlePolls 16 -Interactive | Write-Host
    & (Join-Path $PSScriptRoot "build32-image.ps1") | Write-Host
    & (Join-Path $PSScriptRoot "build32-hdd.ps1") | Write-Host
}

function Send-LeonOsMonitorLine {
    param(
        [System.IO.StreamWriter] $Writer,
        [System.IO.Stream] $Stream,
        [string] $Line
    )

    $Writer.WriteLine($Line)
    Wait-QemuMonitorPrompt $Stream 3000 | Out-Null
}

function Send-LeonOsTextKeys {
    param(
        [System.IO.StreamWriter] $Writer,
        [System.IO.Stream] $Stream,
        [string] $Text
    )

    foreach ($Char in $Text.ToCharArray()) {
        $Key = switch ($Char) {
            "." { "dot" }
            "/" { "slash" }
            "-" { "minus" }
            "_" { "minus" }
            ":" { "colon" }
            default { [string] $Char }
        }
        Send-LeonOsMonitorLine $Writer $Stream "sendkey $Key"
        Start-Sleep -Milliseconds 180
    }
}

$Qemu = Get-LeonOsQemu
$ImagePath = Get-LeonOsImagePath $Image
$HddPath = Get-LeonOsImagePath $HddImage
$SerialLog = Get-LeonOsSerialLogPath "LeonOS-NetSurf-Interactive-Test"
if (Test-Path -LiteralPath $SerialLog) {
    Remove-Item -LiteralPath $SerialLog -Force
}
$SerialArg = Get-LeonOsQemuSerialFileArg $SerialLog

$Listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$Listener.Start()
$MonitorPort = ([System.Net.IPEndPoint] $Listener.LocalEndpoint).Port
$Listener.Stop()

$Arguments = @(
    "-name", "LeonOS-NetSurf-Interactive-Test",
    "-machine", "pc",
    "-cpu", "qemu32",
    "-m", "256M",
    "-drive", "file=$ImagePath,format=raw,if=floppy",
    "-drive", "file=$HddPath,format=raw,if=ide,index=0,media=disk",
    "-boot", "a",
    "-vga", "std",
    "-serial", $SerialArg,
    "-display", "none",
    "-monitor", "tcp:127.0.0.1:$MonitorPort,server,nowait",
    "-nic", "user,model=rtl8139",
    "-no-reboot"
)

$StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
$StartInfo.FileName = $Qemu
$StartInfo.Arguments = Join-QemuArguments $Arguments
$StartInfo.UseShellExecute = $false
$StartInfo.RedirectStandardError = $true

$Process = [System.Diagnostics.Process]::Start($StartInfo)
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
        throw "Could not connect to QEMU monitor on port $MonitorPort."
    }

    $Stream = $Client.GetStream()
    $Writer = [System.IO.StreamWriter]::new($Stream)
    $Writer.AutoFlush = $true
    Wait-QemuMonitorPrompt $Stream 8000 | Out-Null

    $BootText = Wait-LeonOsSerialLog $SerialLog "LeonOS shell ready" 90000
    if (-not $BootText.Contains("LeonOS shell ready")) {
        throw "QEMU did not reach the LeonOS shell."
    }

    Send-LeonOsMonitorLine $Writer $Stream "sendkey f11"
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS shell app=net" 90000
    Send-LeonOsMonitorLine $Writer $Stream "sendkey n"

    $Deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $Loaded = ""
    while ([DateTime]::UtcNow -lt $Deadline -and -not $Process.HasExited) {
        $Loaded = Read-LeonOsSerialLog $SerialLog
        if ($Loaded.Contains("WINDOW REDRAW WIN 0 STOP")) {
            break
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $Loaded.Contains("WINDOW REDRAW WIN 0 STOP")) {
        throw "Interactive NetSurf did not complete the first redraw."
    }

    for ($Move = 0; $Move -lt 40; $Move += 1) {
        $Writer.WriteLine("mouse_move -32 -32")
        Start-Sleep -Milliseconds 25
    }
    Start-Sleep -Milliseconds 250
    Move-QemuMouseTo $Writer 294 150 -Step 32 `
        -FromX 0 -FromY 0
    Start-Sleep -Milliseconds 200
    Send-LeonOsMonitorLine $Writer $Stream "mouse_button 0x01"
    Start-Sleep -Milliseconds 100
    Send-LeonOsMonitorLine $Writer $Stream "mouse_button 0"

    $FocusText = Wait-LeonOsSerialLog $SerialLog "NETSURF URL EDIT FOCUS" 30000
    if (-not $FocusText.Contains("NETSURF URL EDIT FOCUS")) {
        throw "NetSurf address bar did not focus after click."
    }
    Start-Sleep -Milliseconds 300

    $BeforeNavigate = Read-LeonOsSerialLog $SerialLog
    $BeforeNavigateLength = $BeforeNavigate.Length
    Send-LeonOsTextKeys $Writer $Stream $TypedUrl
    Send-LeonOsMonitorLine $Writer $Stream "sendkey ret"

    $ExpectedUrl = if ($TypedUrl.StartsWith("https://", [System.StringComparison]::OrdinalIgnoreCase)) {
        $TypedUrl
    } else {
        "https://$TypedUrl"
    }
    $ExpectedUrlPattern = [regex]::Escape($ExpectedUrl)
    if (-not $ExpectedUrl.EndsWith("/", [System.StringComparison]::Ordinal)) {
        $ExpectedUrlPattern += "/?"
    }
    $Navigated = ""
    $NavDeadline = [DateTime]::UtcNow.AddMilliseconds(160000)
    while ([DateTime]::UtcNow -lt $NavDeadline -and -not $Process.HasExited) {
        $AllText = Read-LeonOsSerialLog $SerialLog
        if ($AllText.Length -gt $BeforeNavigateLength) {
            $Navigated = $AllText.Substring($BeforeNavigateLength)
            if ($Navigated -match "(?m)^WINDOW SET_URL WIN 0 URL $ExpectedUrlPattern$") {
                break
            }
        }
        Start-Sleep -Milliseconds 100
    }
    if ($Navigated -notmatch "(?m)^WINDOW SET_URL WIN 0 URL $ExpectedUrlPattern$") {
        throw "Typed URL did not navigate to $ExpectedUrl."
    }

    $Writer.WriteLine("quit")
    $Writer.Dispose()
    $Client.Close()
    $Client = $null

    if (-not $Process.WaitForExit(10000)) {
        $Process.Kill()
        $Process.WaitForExit()
    }

    $Stdout = Read-LeonOsSerialLog $SerialLog
    foreach ($Bad in @(
        "NetSurf abort", "DIE ", "CPU exception", "PANIC",
        "user syscall bad ptr", "user app bad", "user app no memory")) {
        if ($Stdout -like "*$Bad*") {
            throw "Interactive NetSurf run emitted failure marker: $Bad"
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

Write-Host "test32-hdd-netsurf-interactive-qemu.ps1 passed."
