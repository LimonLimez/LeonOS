param(
    [Parameter(Mandatory = $true)]
    [string] $Url,
    [ValidateSet("720p", "1080p")]
    [string] $Resolution = "1080p",
    [int] $TimeoutSeconds = 120
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

function Wait-SerialContains {
    param(
        [string] $LogPath,
        [string] $Needle,
        [int] $TimeoutMs
    )
    $Deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    while ([DateTime]::UtcNow -lt $Deadline) {
        if (Test-Path -LiteralPath $LogPath) {
            $Text = Get-Content -LiteralPath $LogPath -Raw -ErrorAction SilentlyContinue
            if ($Text -and $Text.Contains($Needle)) {
                return $Text
            }
        }
        Start-Sleep -Milliseconds 200
    }
    throw "Timed out waiting for serial proof: $Needle"
}

function Wait-SerialContainsAfter {
    param(
        [string] $LogPath,
        [string] $Needle,
        [int] $Offset,
        [int] $TimeoutMs
    )
    $Deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    while ([DateTime]::UtcNow -lt $Deadline) {
        if (Test-Path -LiteralPath $LogPath) {
            $Text = Get-Content -LiteralPath $LogPath -Raw -ErrorAction SilentlyContinue
            if ($Text -and $Text.Length -ge $Offset) {
                $Tail = $Text.Substring($Offset)
                if ($Tail.Contains($Needle)) {
                    return $Text
                }
            }
        }
        Start-Sleep -Milliseconds 200
    }
    throw "Timed out waiting for serial proof after navigation: $Needle"
}

& (Join-Path $PSScriptRoot "build32-image.ps1") -Resolution $Resolution | Write-Host
& (Join-Path $PSScriptRoot "build32-hdd.ps1") -ExcludeNetSurf | Write-Host

$Qemu = Get-LeonOsQemu
$ImagePath = Get-LeonOsImagePath "dist32\leonos32.img"
$HddPath = Get-LeonOsImagePath "dist32\leonos32-hdd.img"
$SerialLog = Get-LeonOsSerialLogPath "leonos-browser-visible"
$ShotPath = Join-Path (Join-Path $PSScriptRoot "..\dist32") "leonos-browser-visible.ppm"
Remove-Item -LiteralPath $SerialLog, $ShotPath -Force -ErrorAction SilentlyContinue

$Listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$Listener.Start()
$MonitorPort = ([System.Net.IPEndPoint] $Listener.LocalEndpoint).Port
$Listener.Stop()

$DisplayArg = if ($Resolution -eq "1080p") {
    "gtk,zoom-to-fit=off,show-menubar=off,grab-on-hover=on,show-cursor=on"
} else {
    "gtk,zoom-to-fit=on,show-menubar=off,grab-on-hover=on,show-cursor=on"
}

$Arguments = @(
    "-name", "LeonOS-BrowserUrl-Visible-$Resolution",
    "-machine", "pc",
    "-cpu", "qemu32",
    "-m", "256M",
    "-drive", "file=$ImagePath,format=raw,if=floppy",
    "-drive", "file=$HddPath,format=raw,if=ide,index=0,media=disk",
    "-boot", "a",
    "-vga", "std",
    "-display", $DisplayArg,
    "-serial", (Get-LeonOsQemuSerialFileArg $SerialLog),
    "-monitor", "tcp:127.0.0.1:$MonitorPort,server,nowait",
    "-nic", "user,model=rtl8139",
    "-no-reboot"
)

$Process = Start-Process -FilePath $Qemu -ArgumentList $Arguments `
    -WorkingDirectory (Resolve-Path (Join-Path $PSScriptRoot "..")).Path `
    -PassThru

$Client = $null
for ($Attempt = 0; $Attempt -lt 80 -and -not $Client; $Attempt += 1) {
    try {
        Start-Sleep -Milliseconds 150
        $Client = [System.Net.Sockets.TcpClient]::new("127.0.0.1", $MonitorPort)
    } catch {
        $Client = $null
    }
}
if (-not $Client) {
    throw "Could not connect to visible QEMU monitor on port $MonitorPort."
}

$Stream = $Client.GetStream()
$Writer = [System.IO.StreamWriter]::new($Stream)
$Writer.AutoFlush = $true
try {
    Wait-QemuMonitorPrompt $Stream 8000
    $null = Wait-SerialContains $SerialLog "LeonOS shell ready" 60000
    $Writer.WriteLine("sendkey f11")
    Wait-QemuMonitorPrompt $Stream 3000
    $null = Wait-SerialContains $SerialLog "LeonOS shell app=net" 15000
    $Writer.WriteLine("sendkey ctrl-l")
    Wait-QemuMonitorPrompt $Stream 3000
    Start-Sleep -Milliseconds 100
    foreach ($Char in $Url.ToCharArray()) {
        $Writer.WriteLine("sendkey $(Get-KeyNameForUrlChar $Char)")
        Wait-QemuMonitorPrompt $Stream 1000
        Start-Sleep -Milliseconds 20
    }
    $BeforeNavigate = Get-Content -LiteralPath $SerialLog -Raw -ErrorAction SilentlyContinue
    $BeforeNavigateLength = if ($BeforeNavigate) { $BeforeNavigate.Length } else { 0 }
    $Writer.WriteLine("sendkey ret")
    Wait-QemuMonitorPrompt $Stream 3000
    $null = Wait-SerialContainsAfter $SerialLog "LeonOS net HTTPS status" $BeforeNavigateLength ($TimeoutSeconds * 1000)
    $null = Wait-SerialContainsAfter $SerialLog "WINDOW REDRAW WIN 0 STOP" $BeforeNavigateLength ($TimeoutSeconds * 1000)
    $Writer.WriteLine("screendump $($ShotPath -replace '\\', '/')")
    Wait-QemuMonitorPrompt $Stream 5000
} finally {
    $Writer.Dispose()
    $Client.Close()
}

Write-Host "Visible QEMU PID: $($Process.Id)"
Write-Host "Serial log: $SerialLog"
Write-Host "Screenshot: $ShotPath"
