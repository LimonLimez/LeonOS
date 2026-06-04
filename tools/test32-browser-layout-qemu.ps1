param(
    [ValidateSet("720p", "1080p")]
    [string] $Resolution = "1080p",
    [int] $TimeoutSeconds = 120
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")
. (Join-Path $PSScriptRoot "visual-common.ps1")

& (Join-Path $PSScriptRoot "build32-image.ps1") -Resolution $Resolution | Write-Host
& (Join-Path $PSScriptRoot "build32-hdd.ps1") | Write-Host

$Qemu = Get-LeonOsQemu
$ImagePath = (Get-LeonOsImagePath "dist32\leonos32.img") -replace '"', '\"'
$HddPath = (Get-LeonOsImagePath "dist32\leonos32-hdd.img") -replace '"', '\"'
$SerialLog = Get-LeonOsSerialLogPath "test32-browser-layout-$Resolution"
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
$StartInfo.Arguments = "-name LeonOS-BrowserLayout-$Resolution -machine pc -cpu qemu32 -m 32M -drive file=`"$ImagePath`",format=raw,if=floppy -drive file=`"$HddPath`",format=raw,if=ide,index=0,media=disk -boot a -vga std -display none -serial $SerialArg -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -nic user,model=rtl8139 -no-reboot"
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

    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS shell ready" 60000
    $Writer.WriteLine("sendkey f11")
    Wait-QemuMonitorPrompt $Stream 3000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS shell app=net" 10000
    $Writer.WriteLine("sendkey d")
    Wait-QemuMonitorPrompt $Stream 3000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser DOM layout selftest open" 10000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser HTML structure" 10000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser DOM nodes" 10000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser response complete fixture" 10000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser layout boxes" 10000

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
        Write-Host "QEMU stderr (LeonOS-BrowserLayout-$Resolution):"
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
    "LeonOS net browser DOM layout selftest open",
    "LeonOS net browser HTML structure",
    "LeonOS net browser HTML support",
    "LeonOS net browser DOM nodes",
    "LeonOS net browser response complete fixture",
    "LeonOS net browser layout boxes"
)
foreach ($Line in $Expected) {
    if ($Output -notlike "*$Line*") {
        throw "Missing DOM/layout serial proof: $Line"
    }
}

$DomMatch = [regex]::Match(
    $Output,
    'LeonOS net browser DOM nodes\s+(?<Nodes>\d+).* supported (?<Supported>\d+) block (?<Block>\d+) inline (?<Inline>\d+) table (?<Table>\d+) controls (?<Controls>\d+) resources (?<Resources>\d+) unsupported (?<Unsupported>\d+) placeholders (?<Placeholders>\d+)')
if (-not $DomMatch.Success) {
    throw "Missing expanded DOM support counters."
}
foreach ($Name in @("Nodes", "Supported", "Block", "Inline", "Table", "Controls", "Unsupported", "Placeholders")) {
    if ([int] $DomMatch.Groups[$Name].Value -le 0) {
        throw "DOM/layout selftest expected $Name to be greater than zero."
    }
}

$LayoutMatch = [regex]::Match(
    $Output,
    'LeonOS net browser layout boxes\s+(?<Boxes>\d+) lines (?<Lines>\d+) links (?<Links>\d+) controls (?<Controls>\d+) block (?<Block>\d+) inline (?<Inline>\d+) table (?<Table>\d+) boxes (?<Boxed>\d+) wraps (?<Wraps>\d+) margin (?<Margin>\d+) padding (?<Padding>\d+) border (?<Border>\d+)')
if (-not $LayoutMatch.Success) {
    throw "Missing expanded layout box counters."
}
foreach ($Name in @("Boxes", "Lines", "Links", "Controls", "Block", "Inline", "Table", "Boxed", "Wraps", "Margin", "Padding", "Border")) {
    if ([int] $LayoutMatch.Groups[$Name].Value -le 0) {
        throw "DOM/layout selftest expected layout $Name to be greater than zero."
    }
}

foreach ($Bad in @("CPU exception", "PANIC", "unhandled")) {
    if ($Output -like "*$Bad*") {
        throw "DOM/layout test hit fault: $Bad"
    }
}

Write-Host "QEMU 32-bit browser DOM/layout selftest passed ($Resolution)."
Write-Host "Serial log: $SerialLog"
