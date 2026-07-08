param(
    [string] $Url = "www.example.com",
    [ValidateSet("720p", "1080p")]
    [string] $Resolution = "1080p",
    [int] $TimeoutSeconds = 90,
    [switch] $PacketCapture
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")
. (Join-Path $PSScriptRoot "visual-common.ps1")

function Get-KeyNameForUrlChar {
    param([char] $Char)
    if ($Char -ge 'A' -and $Char -le 'Z') {
        return "shift-$([char]::ToLowerInvariant($Char))"
    }
    $Lower = [char]::ToLowerInvariant($Char)
    if ($Lower -ge 'a' -and $Lower -le 'z') { return [string] $Lower }
    if ($Lower -ge '0' -and $Lower -le '9') { return [string] $Lower }
    switch ($Lower) {
        '.' { return "dot" }
        '-' { return "minus" }
        '/' { return "slash" }
        ':' { return "shift-semicolon" }
        '?' { return "shift-slash" }
        '_' { return "shift-minus" }
        '=' { return "equal" }
        '&' { return "shift-7" }
        default { throw "URL character '$Char' is not mapped for QEMU sendkey." }
    }
}

function Wait-SerialPattern {
    param(
        [string] $LogPath,
        [string] $Pattern,
        [int] $TimeoutMs
    )

    $Text = Wait-LeonOsSerialLog $LogPath $Pattern $TimeoutMs
    if (-not $Text.Contains($Pattern)) {
        throw "Timed out waiting for serial proof: $Pattern"
    }
    return $Text
}

$TypedUrl = $Url.Trim()
if ($TypedUrl.Length -eq 0) {
    throw "Url must not be empty."
}
$ExpectedUrl = if ($TypedUrl -match '^https://') {
    $TypedUrl
} else {
    "https://$TypedUrl"
}
if ($ExpectedUrl -notmatch '/|\?') {
    $ExpectedUrl = "$ExpectedUrl/"
}
$ExpectedHost = ([System.Uri] $ExpectedUrl).Host
$ExpectedIsImage = $ExpectedUrl -match '\.(png|jpe?g)(\?|#|$)'

& (Join-Path $PSScriptRoot "build32-image.ps1") -Resolution $Resolution | Write-Host
& (Join-Path $PSScriptRoot "build32-hdd.ps1") -ExcludeNetSurf | Write-Host

$Qemu = Get-LeonOsQemu
$ImagePath = (Get-LeonOsImagePath "dist32\leonos32.img") -replace '"', '\"'
$HddPath = (Get-LeonOsImagePath "dist32\leonos32-hdd.img") -replace '"', '\"'
$SerialLog = Get-LeonOsSerialLogPath "test32-browser-url-$($ExpectedHost -replace '[^A-Za-z0-9-]', '-')"
if (Test-Path -LiteralPath $SerialLog) {
    Remove-Item -LiteralPath $SerialLog -Force
}
$SerialArg = Get-LeonOsQemuSerialFileArg $SerialLog
$PcapPath = Join-Path (Join-Path $PSScriptRoot "..\dist32") "test32-browser-url-$($ExpectedHost -replace '[^A-Za-z0-9-]', '-').pcap"
if ($PacketCapture -and (Test-Path -LiteralPath $PcapPath)) {
    Remove-Item -LiteralPath $PcapPath -Force
}

$Listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$Listener.Start()
$MonitorPort = ([System.Net.IPEndPoint] $Listener.LocalEndpoint).Port
$Listener.Stop()

$StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
$StartInfo.FileName = $Qemu
$NetArgs = if ($PacketCapture) {
    $EscapedPcap = $PcapPath -replace '"', '\"'
    "-netdev user,id=net0 -device rtl8139,netdev=net0 -object filter-dump,id=f0,netdev=net0,file=`"$EscapedPcap`""
} else {
    "-nic user,model=rtl8139"
}
$StartInfo.Arguments = "-name LeonOS-BrowserUrl-$Resolution -machine pc -cpu qemu32 -m 32M -drive file=`"$ImagePath`",format=raw,if=floppy -drive file=`"$HddPath`",format=raw,if=ide,index=0,media=disk -boot a -vga std -display none -serial $SerialArg -monitor tcp:127.0.0.1:$MonitorPort,server,nowait $NetArgs -no-reboot"
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

    $null = Wait-SerialPattern $SerialLog "LeonOS shell ready" 60000
    $Writer.WriteLine("sendkey f11")
    Wait-QemuMonitorPrompt $Stream 3000
    $null = Wait-SerialPattern $SerialLog "LeonOS shell app=net" 10000

    $Writer.WriteLine("sendkey ctrl-l")
    Wait-QemuMonitorPrompt $Stream 3000
    Start-Sleep -Milliseconds 100
    foreach ($Char in $TypedUrl.ToCharArray()) {
        $Writer.WriteLine("sendkey $(Get-KeyNameForUrlChar $Char)")
        Wait-QemuMonitorPrompt $Stream 1000
        Start-Sleep -Milliseconds 35
    }
    $Writer.WriteLine("sendkey ret")
    Wait-QemuMonitorPrompt $Stream 3000

    $null = Wait-SerialPattern $SerialLog "LeonOS net browser URL open $ExpectedUrl" 15000
    $null = Wait-SerialPattern $SerialLog "LeonOS net DNS query sent $ExpectedHost" 30000
    $null = Wait-SerialPattern $SerialLog "LeonOS net DNS A $ExpectedHost" 30000
    $null = Wait-SerialPattern $SerialLog "LeonOS net TLS ClientHello sent" 60000
    $null = Wait-SerialPattern $SerialLog "LeonOS net HTTPS status" 60000
    if ($ExpectedIsImage) {
        $null = Wait-SerialPattern $SerialLog "LeonOS net browser direct image document" 60000
        $null = Wait-SerialPattern $SerialLog "LeonOS net browser direct image summary" 60000
    }
    $null = Wait-SerialPattern $SerialLog "LeonOS net browser response complete" 60000
    if (-not $ExpectedIsImage) {
        $null = Wait-SerialPattern $SerialLog "LeonOS net browser HTML support" 60000
        $null = Wait-SerialPattern $SerialLog "LeonOS net browser HTML attrs" 60000
    }

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
        Write-Host "QEMU stderr (LeonOS-BrowserUrl-$Resolution):"
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
    "LeonOS net browser URL open $ExpectedUrl",
    "LeonOS net DNS query sent $ExpectedHost",
    "LeonOS net DNS A $ExpectedHost",
    "LeonOS net TLS ClientHello sent",
    "LeonOS net HTTPS status",
    "LeonOS net browser response complete"
)
if ($ExpectedIsImage) {
    $Expected += @(
        "LeonOS net browser direct image document",
        "LeonOS net browser direct image summary"
    )
} else {
    $Expected += @(
        "LeonOS net browser HTML support",
        "LeonOS net browser HTML attrs"
    )
}
foreach ($Line in $Expected) {
    if (-not $Output.Contains($Line)) {
        throw "Missing browser URL serial proof: $Line"
    }
}
foreach ($Bad in @("CPU exception", "PANIC", "unhandled")) {
    if ($Output -like "*$Bad*") {
        throw "Browser URL test hit fault: $Bad"
    }
}

Write-Host "QEMU typed browser URL test passed: $ExpectedUrl ($Resolution)."
Write-Host "Serial log: $SerialLog"
if ($PacketCapture) {
    Write-Host "Packet capture: $PcapPath"
}
