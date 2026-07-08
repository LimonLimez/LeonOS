param(
    [ValidateSet("720p", "1080p")]
    [string] $Resolution = "1080p",
    [int] $TimeoutSeconds = 140
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")
. (Join-Path $PSScriptRoot "visual-common.ps1")

function Get-KeyNameForUrlChar {
    param([char] $Char)

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

function Get-BrowserLinkClickPoint {
    param(
        [string] $LayoutOutput,
        [string] $Resolution
    )

    $LinkBoxMatch = [regex]::Match(
        $LayoutOutput,
        'LeonOS net browser layout boxes[^\r\n]* firstlink (?<Link>\d+) (?<LX>\d+) (?<LY>\d+) (?<LW>\d+) (?<LH>\d+)')
    if (-not $LinkBoxMatch.Success) {
        throw "Browser layout did not expose a clickable first link box."
    }

    $Size = Get-LeonOsResolutionSize $Resolution
    $Width = [int] $Size.Width
    $Height = [int] $Size.Height
    $Compact = $Height -le 720
    $TaskbarH = if ($Compact) { Scale-LeonOsY 52 $Height } else { Scale-LeonOsY 48 $Height }
    if ($TaskbarH -lt 40) { $TaskbarH = 40 }
    $TaskbarY = $Height - $TaskbarH
    $TitleH = if ($Compact) { Scale-LeonOsY 48 $Height } else { Scale-LeonOsY 42 $Height }
    if ($TitleH -lt 36) { $TitleH = 36 }
    $WinX = Scale-LeonOsX 72 $Width
    $WinY = Scale-LeonOsY 70 $Height
    $MinWinW = Scale-LeonOsX 300 $Width
    $MinWinH = Scale-LeonOsY 220 $Height
    $WinW = if ($Width -gt (Scale-LeonOsX 144 $Width)) { $Width - (Scale-LeonOsX 144 $Width) } else { Scale-LeonOsX 720 $Width }
    $WinH = if ($TaskbarY -gt (Scale-LeonOsY 118 $Height)) { $TaskbarY - (Scale-LeonOsY 92 $Height) } else { Scale-LeonOsY 520 $Height }
    if ($WinW -lt $MinWinW) { $WinW = $MinWinW }
    if ($WinH -lt $MinWinH) { $WinH = $MinWinH }
    $PadX = Scale-LeonOsX 8 $Width
    $PadY = Scale-LeonOsY 8 $Height
    $MaxW = if ($Width -gt ($PadX * 2)) { $Width - ($PadX * 2) } else { $Width }
    $MaxH = if ($TaskbarY -gt ($PadY * 2)) { $TaskbarY - ($PadY * 2) } else { $TaskbarY }
    if ($WinW -gt $MaxW) { $WinW = $MaxW }
    if ($WinH -gt $MaxH) { $WinH = $MaxH }
    if ($WinX -lt $PadX) { $WinX = $PadX }
    if ($WinY -lt $PadY) { $WinY = $PadY }
    if (($WinX + $WinW) -gt ($Width - $PadX)) { $WinX = $Width - $PadX - $WinW }
    if (($WinY + $WinH) -gt ($TaskbarY - $PadY)) { $WinY = $TaskbarY - $PadY - $WinH }
    if ($WinX -lt 0) { $WinX = 0 }
    if ($WinY -lt 0) { $WinY = 0 }

    $ToolbarH = 21 + (Scale-LeonOsY 26 $Height)
    if ($ToolbarH -lt 46) { $ToolbarH = 46 }
    $HeaderH = 21 + (Scale-LeonOsY 16 $Height)
    if ($HeaderH -lt 34) { $HeaderH = 34 }
    $ClientY = $WinY + $TitleH
    $PageX = $WinX + (Scale-LeonOsX 10 $Width)
    $PageY = $ClientY + $ToolbarH + (Scale-LeonOsY 10 $Height)
    $PageH = $WinH - $TitleH - $ToolbarH - (Scale-LeonOsY 20 $Height)
    $ContentX = $PageX + (Scale-LeonOsX 16 $Width)
    $ContentY = $PageY + $HeaderH + (Scale-LeonOsY 12 $Height)
    $ContentH = $PageH - $HeaderH - (Scale-LeonOsY 24 $Height)
    if ($ContentH -le 0) {
        throw "Browser content height was not positive."
    }

    return [pscustomobject] @{
        X = $ContentX + [int] $LinkBoxMatch.Groups["LX"].Value + [Math]::Max(1, [int] ([int] $LinkBoxMatch.Groups["LW"].Value / 2))
        Y = $ContentY + [int] $LinkBoxMatch.Groups["LY"].Value + [Math]::Max(1, [int] ([int] $LinkBoxMatch.Groups["LH"].Value / 2))
        Width = $Width
        Height = $Height
    }
}

& (Join-Path $PSScriptRoot "build32-image.ps1") -Resolution $Resolution | Write-Host
& (Join-Path $PSScriptRoot "build32-hdd.ps1") -ExcludeNetSurf | Write-Host

$Qemu = Get-LeonOsQemu
$ImagePath = (Get-LeonOsImagePath "dist32\leonos32.img") -replace '"', '\"'
$HddPath = (Get-LeonOsImagePath "dist32\leonos32-hdd.img") -replace '"', '\"'
$SerialLog = Get-LeonOsSerialLogPath "test32-browser-navigation-$Resolution"
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
$StartInfo.Arguments = "-name LeonOS-BrowserNavigation-$Resolution -machine pc -cpu qemu32 -m 32M -drive file=`"$ImagePath`",format=raw,if=floppy -drive file=`"$HddPath`",format=raw,if=ide,index=0,media=disk -boot a -vga std -display none -serial $SerialArg -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -nic user,model=rtl8139 -no-reboot"
$StartInfo.UseShellExecute = $false
$StartInfo.RedirectStandardError = $true

$Process = [System.Diagnostics.Process]::Start($StartInfo)
$Client = $null
$Writer = $null
$script:SerialCursor = 0

function Mark-SerialCursor {
    $script:SerialCursor = (Read-LeonOsSerialLog $SerialLog).Length
}

function Wait-SerialAfter {
    param(
        [string] $Pattern,
        [int] $TimeoutMs = 10000
    )

    $Deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $Deadline) {
        $Text = Read-LeonOsSerialLog $SerialLog
        $Start = [Math]::Min($script:SerialCursor, $Text.Length)
        $Index = $Text.IndexOf($Pattern, $Start, [System.StringComparison]::Ordinal)
        if ($Index -ge 0) {
            $script:SerialCursor = $Index + $Pattern.Length
            return $Text
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for serial proof after cursor: $Pattern"
}

function Wait-SerialRegexAfter {
    param(
        [string] $Pattern,
        [int] $TimeoutMs = 10000
    )

    $Regex = [regex] $Pattern
    $Deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $Deadline) {
        $Text = Read-LeonOsSerialLog $SerialLog
        $Start = [Math]::Min($script:SerialCursor, $Text.Length)
        $Tail = $Text.Substring($Start)
        $Match = $Regex.Match($Tail)
        if ($Match.Success) {
            $script:SerialCursor = $Start + $Match.Index + $Match.Length
            return [pscustomobject] @{ Text = $Text; Match = $Match }
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for serial regex after cursor: $Pattern"
}

function Send-MonitorCommand {
    param([string] $Command)

    $Writer.WriteLine($Command)
    Wait-QemuMonitorPrompt $Stream 3000
}

function Move-TrackedMouse {
    param(
        [int] $TargetX,
        [int] $TargetY,
        [int] $FromX,
        [int] $FromY
    )

    foreach ($Line in (Get-QemuRelativeMouseMoves $TargetX $TargetY -FromX $FromX -FromY $FromY -Step 60)) {
        Send-MonitorCommand $Line
        Start-Sleep -Milliseconds 40
    }
}

function Click-TrackedMouse {
    Send-MonitorCommand "mouse_button 0x01"
    Start-Sleep -Milliseconds 160
    Send-MonitorCommand "mouse_button 0"
    Start-Sleep -Milliseconds 260
}

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
    Mark-SerialCursor
    Send-MonitorCommand "sendkey f11"
    $null = Wait-SerialAfter "LeonOS shell app=net" 10000

    Send-MonitorCommand "sendkey d"
    $null = Wait-SerialAfter "LeonOS net browser history push 0/1 leonos://dom-layout-selftest" 10000
    $null = Wait-SerialAfter "LeonOS net browser DOM layout selftest open" 10000
    $null = Wait-SerialAfter "LeonOS net browser state COMPLETE leonos://dom-layout-selftest" 10000
    $LayoutText = Wait-SerialAfter "LeonOS net browser layout boxes" 10000
    $ClickPoint = Get-BrowserLinkClickPoint $LayoutText $Resolution

    for ($I = 0; $I -lt 6; $I += 1) {
        Send-MonitorCommand "sendkey down"
    }

    Send-MonitorCommand "sendkey y"
    $null = Wait-SerialAfter "LeonOS net browser history push 1/2 leonos://css-subset-selftest" 10000
    $null = Wait-SerialAfter "LeonOS net browser CSS selftest open" 10000
    $null = Wait-SerialAfter "LeonOS net browser state COMPLETE leonos://css-subset-selftest" 10000

    Send-MonitorCommand "sendkey alt-left"
    $Back = Wait-SerialRegexAfter 'LeonOS net browser history back 0/2 scroll (?<Scroll>[1-9][0-9]*) leonos://dom-layout-selftest' 10000
    $BackScroll = [int] $Back.Match.Groups["Scroll"].Value
    if ($BackScroll -le 0) {
        throw "Back history did not restore a nonzero scroll value."
    }
    $null = Wait-SerialAfter "LeonOS net browser DOM layout selftest open" 10000
    $null = Wait-SerialAfter "LeonOS net browser state COMPLETE leonos://dom-layout-selftest" 10000

    Send-MonitorCommand "sendkey alt-right"
    $null = Wait-SerialAfter "LeonOS net browser history forward 1/2 scroll 0 leonos://css-subset-selftest" 10000
    $null = Wait-SerialAfter "LeonOS net browser CSS selftest open" 10000
    $null = Wait-SerialAfter "LeonOS net browser state COMPLETE leonos://css-subset-selftest" 10000

    Send-MonitorCommand "sendkey r"
    $null = Wait-SerialAfter "LeonOS net browser history reload leonos://css-subset-selftest" 10000
    $null = Wait-SerialAfter "LeonOS net browser CSS selftest open" 10000
    $null = Wait-SerialAfter "LeonOS net browser state COMPLETE leonos://css-subset-selftest" 10000

    Send-MonitorCommand "sendkey d"
    $null = Wait-SerialAfter "LeonOS net browser history push 2/3 leonos://dom-layout-selftest" 10000
    $null = Wait-SerialAfter "LeonOS net browser DOM layout selftest open" 10000
    $null = Wait-SerialAfter "LeonOS net browser state COMPLETE leonos://dom-layout-selftest" 10000

    $InitialMouse = Get-LeonOsInitialMousePosition $ClickPoint.Width $ClickPoint.Height
    Move-TrackedMouse $ClickPoint.X $ClickPoint.Y -FromX $InitialMouse.X -FromY $InitialMouse.Y
    Click-TrackedMouse
    $null = Wait-SerialAfter "LeonOS net browser link click" 10000
    $null = Wait-SerialAfter "LeonOS net browser navigate https://layout.local/next" 10000
    $null = Wait-SerialAfter "LeonOS net browser state LOADING https://layout.local/next" 10000
    $null = Wait-SerialAfter "LeonOS net browser URL open https://layout.local/next" 10000

    Send-MonitorCommand "sendkey esc"
    $null = Wait-SerialAfter "LeonOS net browser state STOPPED https://layout.local/next" 10000
    $null = Wait-SerialAfter "LeonOS net browser history stop https://layout.local/next" 10000

    Send-MonitorCommand "sendkey ctrl-l"
    foreach ($Char in "http://not-supported.local".ToCharArray()) {
        Send-MonitorCommand "sendkey $(Get-KeyNameForUrlChar $Char)"
    }
    Send-MonitorCommand "sendkey ret"
    $null = Wait-SerialAfter "LeonOS net browser state ERROR https://layout.local/next" 10000
    $null = Wait-SerialAfter "LeonOS net browser URL rejected http://not-supported.local" 10000

    Send-MonitorCommand "quit"
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
        Write-Host "QEMU stderr (LeonOS-BrowserNavigation-$Resolution):"
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
    "LeonOS net browser history push 0/1 leonos://dom-layout-selftest",
    "LeonOS net browser history push 1/2 leonos://css-subset-selftest",
    "LeonOS net browser history back 0/2",
    "LeonOS net browser history forward 1/2",
    "LeonOS net browser history reload leonos://css-subset-selftest",
    "LeonOS net browser link click",
    "LeonOS net browser navigate https://layout.local/next",
    "LeonOS net browser state LOADING https://layout.local/next",
    "LeonOS net browser history stop https://layout.local/next",
    "LeonOS net browser state STOPPED https://layout.local/next",
    "LeonOS net browser URL rejected http://not-supported.local",
    "LeonOS net browser state ERROR"
)
foreach ($Line in $Expected) {
    if ($Output -notlike "*$Line*") {
        throw "Missing navigation serial proof: $Line"
    }
}

foreach ($Bad in @("CPU exception", "PANIC", "unhandled")) {
    if ($Output -like "*$Bad*") {
        throw "Browser navigation test hit fault: $Bad"
    }
}

Write-Host "QEMU 32-bit browser navigation selftest passed ($Resolution)."
Write-Host "Serial log: $SerialLog"
