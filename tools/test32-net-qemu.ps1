param(
    [string] $Image = "dist32\leonos32.img",
    [string] $HddImage = "dist32\leonos32-hdd.img",
    [ValidateSet("720p", "1080p")]
    [string] $Resolution = "720p",
    [int] $TimeoutSeconds = 60
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")
. (Join-Path $PSScriptRoot "visual-common.ps1")

& (Join-Path $PSScriptRoot "build32-image.ps1") -Resolution $Resolution | Write-Host
& (Join-Path $PSScriptRoot "build32-hdd.ps1") | Write-Host

$Qemu = Get-LeonOsQemu
$ImagePath = Get-LeonOsImagePath $Image
$HddPath = Get-LeonOsImagePath $HddImage
$EscapedImagePath = $ImagePath -replace '"', '\"'
$EscapedHddPath = $HddPath -replace '"', '\"'
$SerialLog = Get-LeonOsSerialLogPath "test32-net-qemu"
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
$StartInfo.Arguments = "-name LeonOS-32BitNet-$Resolution -machine pc -cpu qemu32 -m 32M -drive file=`"$EscapedImagePath`",format=raw,if=floppy -drive file=`"$EscapedHddPath`",format=raw,if=ide,index=0,media=disk -boot a -vga std -serial $SerialArg -display none -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -nic user,model=rtl8139 -no-reboot"
$StartInfo.UseShellExecute = $false
$StartInfo.RedirectStandardError = $true

$Process = [System.Diagnostics.Process]::Start($StartInfo)
$Client = $null
$Stream = $null
$Writer = $null

try {
    for ($Attempt = 0; $Attempt -lt 40 -and -not $Client; $Attempt++) {
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

    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS cooperative scheduler OK" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net ARP gateway request sent" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net ARP gateway resolved" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net ICMP echo reply received" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net ARP DNS resolved" 60000
    $Writer.WriteLine("sendkey f11")
    Wait-QemuMonitorPrompt $Stream 3000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS shell app=net" 10000
    $Writer.WriteLine("sendkey h")
    Wait-QemuMonitorPrompt $Stream 3000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser URL open https://www.google.com/?igu=1&hl=en" 10000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net DNS A www.google.com" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net TCP 443 connected" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net TLS ServerHello received" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net TLS handshake keys ready" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net TLS Finished sent" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net HTTPS GET sent" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net HTTPS status" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net HTTPS headers bytes" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net HTTPS body raw bytes" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net HTTPS body decoded bytes" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser response complete" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser HTML structure" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser HTML support" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser HTML attrs" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser HTML resources" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser resource queue" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser render blocks" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser DOM nodes" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser JS scripts" 60000
    Start-Sleep -Seconds 2

    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser layout boxes" 10000
    Start-Sleep -Seconds 2
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
        Write-Host "QEMU stderr (LeonOS-32BitNet-$Resolution):"
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
    "LeonOS net ARP gateway request sent",
    "LeonOS net ARP gateway resolved",
    "LeonOS net ICMP echo request sent",
    "LeonOS net ICMP echo reply received",
    "LeonOS net ARP DNS request sent",
    "LeonOS net ARP DNS resolved",
    "LeonOS net browser URL open https://www.google.com/?igu=1&hl=en",
    "LeonOS net DNS query sent www.google.com",
    "LeonOS net DNS A www.google.com",
    "LeonOS net TCP 443 SYN sent",
    "LeonOS net TCP 443 connected",
    "LeonOS net TLS ClientHello sent",
    "LeonOS net TLS ServerHello received",
    "LeonOS net TLS handshake keys ready",
    "LeonOS net TLS Finished sent",
    "LeonOS net HTTPS GET sent",
    "LeonOS net HTTPS status",
    "LeonOS net HTTPS headers bytes",
    "LeonOS net HTTPS body raw bytes",
    "LeonOS net HTTPS body decoded bytes",
    "LeonOS net browser HTML structure",
    "LeonOS net browser HTML support",
    "LeonOS net browser HTML attrs",
    "LeonOS net browser HTML resources",
    "LeonOS net browser response complete",
    "LeonOS net browser resource queue",
    "LeonOS net browser render blocks",
    "LeonOS net browser DOM nodes",
    "LeonOS net browser JS scripts",
    "LeonOS shell app=net",
    "LeonOS net browser layout boxes"
)

foreach ($Line in $Expected) {
    if ($Output -notlike "*$Line*") {
        throw "Missing network serial proof: $Line"
    }
}

$CompleteMatch = [regex]::Match(
    $Output,
    'LeonOS net browser response complete\s+\S+\s+decoded\s+(?<Bytes>\d+)')
if (-not $CompleteMatch.Success) {
    throw "Missing decoded-byte count on browser response completion."
}
$DecodedBytes = [int] $CompleteMatch.Groups["Bytes"].Value
if ($DecodedBytes -lt 3000) {
    throw "Large HTTPS page proof decoded only $DecodedBytes bytes; expected at least 3000."
}

foreach ($Bad in @("CPU exception", "PANIC", "unhandled")) {
    if ($Output -like "*$Bad*") {
        throw "Network test hit fault: $Bad"
    }
}

Write-Host "QEMU 32-bit RTL8139 ARP/ICMP/DNS/TCP/TLS/HTTPS real-HTML browser preview smoke test passed ($Resolution)."
Write-Host "Serial log: $SerialLog"
