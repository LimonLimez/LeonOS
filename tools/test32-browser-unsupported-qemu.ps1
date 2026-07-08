param(
    [ValidateSet("720p", "1080p")]
    [string] $Resolution = "1080p",
    [int] $TimeoutSeconds = 120
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")

& (Join-Path $PSScriptRoot "build32-image.ps1") -Resolution $Resolution | Write-Host
& (Join-Path $PSScriptRoot "build32-hdd.ps1") -ExcludeNetSurf | Write-Host

$Qemu = Get-LeonOsQemu
$ImagePath = (Get-LeonOsImagePath "dist32\leonos32.img") -replace '"', '\"'
$HddPath = (Get-LeonOsImagePath "dist32\leonos32-hdd.img") -replace '"', '\"'
$SerialLog = Get-LeonOsSerialLogPath "test32-browser-unsupported-$Resolution"
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
$StartInfo.Arguments = "-name LeonOS-BrowserUnsupported-$Resolution -machine pc -cpu qemu32 -m 32M -drive file=`"$ImagePath`",format=raw,if=floppy -drive file=`"$HddPath`",format=raw,if=ide,index=0,media=disk -boot a -vga std -display none -serial $SerialArg -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -nic user,model=rtl8139 -no-reboot"
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
    $Writer.WriteLine("sendkey j")
    Wait-QemuMonitorPrompt $Stream 3000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser unsupported selftest open" 10000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser unsupported banner NOJS" 10000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser unsupported summary mode NOJS" 10000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser JS scripts" 10000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser DOM nodes" 10000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser response complete fixture" 10000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser state COMPLETE leonos://unsupported-selftest" 10000
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
        Write-Host "QEMU stderr (LeonOS-BrowserUnsupported-$Resolution):"
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
    "LeonOS net browser unsupported selftest open",
    "LeonOS net browser unsupported banner NOJS",
    "LeonOS net browser unsupported summary mode NOJS jsrequired yes",
    "LeonOS net browser state COMPLETE leonos://unsupported-selftest",
    "LeonOS net browser layout boxes"
)
foreach ($Line in $Expected) {
    if ($Output -notlike "*$Line*") {
        throw "Missing unsupported-feature serial proof: $Line"
    }
}

$AttrMatch = [regex]::Match(
    $Output,
    'LeonOS net browser HTML attrs[^\r\n]* js (?<Js>\d+) events (?<Events>\d+) jsurls (?<JsUrls>\d+) srcset (?<Srcset>\d+) templates (?<Templates>\d+) noscript (?<Noscript>\d+)')
if (-not $AttrMatch.Success) {
    throw "Missing unsupported-feature HTML attr counters."
}
foreach ($Name in @("Js", "Events", "JsUrls", "Srcset", "Templates", "Noscript")) {
    if ([int] $AttrMatch.Groups[$Name].Value -le 0) {
        throw "Unsupported selftest expected attr counter $Name to be greater than zero."
    }
}

$JsMatch = [regex]::Match(
    $Output,
    'LeonOS net browser JS scripts\s+(?<Scripts>\d+) bytes (?<Bytes>\d+) tokens (?<Tokens>\d+) funcs (?<Funcs>\d+) vars (?<Vars>\d+) doc (?<Doc>\d+) win (?<Win>\d+) loc (?<Loc>\d+) writes (?<Writes>\d+)')
if (-not $JsMatch.Success) {
    throw "Missing expanded JS counters."
}
foreach ($Name in @("Scripts", "Bytes", "Tokens", "Funcs", "Vars", "Doc", "Win", "Loc", "Writes")) {
    if ([int] $JsMatch.Groups[$Name].Value -le 0) {
        throw "Unsupported selftest expected JS counter $Name to be greater than zero."
    }
}

$UnsupportedMatch = [regex]::Match(
    $Output,
    'LeonOS net browser unsupported summary mode NOJS jsrequired yes features (?<Features>\d+) events (?<Events>\d+) jsurls (?<JsUrls>\d+) noscript (?<Noscript>\d+) svg (?<Svg>\d+) media (?<Media>\d+) custom (?<Custom>\d+) templates (?<Templates>\d+) srcset (?<Srcset>\d+)')
if (-not $UnsupportedMatch.Success) {
    throw "Missing unsupported summary counters."
}
foreach ($Name in @("Features", "Events", "JsUrls", "Noscript", "Svg", "Media", "Custom", "Templates", "Srcset")) {
    if ([int] $UnsupportedMatch.Groups[$Name].Value -le 0) {
        throw "Unsupported selftest expected summary counter $Name to be greater than zero."
    }
}

$DomMatch = [regex]::Match(
    $Output,
    'LeonOS net browser DOM nodes\s+(?<Nodes>\d+).* unsupported (?<Unsupported>\d+) placeholders (?<Placeholders>\d+)')
if (-not $DomMatch.Success) {
    throw "Missing DOM unsupported counters."
}
foreach ($Name in @("Nodes", "Unsupported", "Placeholders")) {
    if ([int] $DomMatch.Groups[$Name].Value -le 0) {
        throw "Unsupported selftest expected DOM $Name to be greater than zero."
    }
}

foreach ($Bad in @("CPU exception", "PANIC", "unhandled")) {
    if ($Output -like "*$Bad*") {
        throw "Unsupported-feature test hit fault: $Bad"
    }
}

Write-Host "QEMU 32-bit browser unsupported-feature selftest passed ($Resolution)."
Write-Host "Serial log: $SerialLog"
