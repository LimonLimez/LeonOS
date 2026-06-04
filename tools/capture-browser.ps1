param(
    [ValidateSet("1080p", "720p")]
    [string] $Resolution = "1080p",
    [switch] $Info,
    [switch] $ClickFirstLink,
    [string] $OutDir = "dist32",
    [int] $TimeoutSeconds = 60
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")
. (Join-Path $PSScriptRoot "visual-common.ps1")

& (Join-Path $PSScriptRoot "build32-image.ps1") -Resolution $Resolution | Write-Host
& (Join-Path $PSScriptRoot "build32-hdd.ps1") | Write-Host

$Root = Split-Path -Parent $PSScriptRoot
$OutputRoot = if ([System.IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $Root $OutDir }
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null

$Qemu = Get-LeonOsQemu
$ImagePath = (Get-LeonOsImagePath "dist32\leonos32.img") -replace '"', '\"'
$HddPath = (Get-LeonOsImagePath "dist32\leonos32-hdd.img") -replace '"', '\"'
$InfoSlug = if ($Info) { "-info" } else { "" }
$ClickSlug = if ($ClickFirstLink) { "-clicked" } else { "" }
$PpmPath = Join-Path $OutputRoot "leonos32-browser-real$InfoSlug$ClickSlug-$Resolution.ppm"
$PngPath = Join-Path $OutputRoot "leonos32-browser-real$InfoSlug$ClickSlug-$Resolution-scaled.png"
$SerialLog = Get-LeonOsSerialLogPath "capture-browser-real$InfoSlug$ClickSlug-$Resolution"
if (Test-Path -LiteralPath $PpmPath) { Remove-Item -LiteralPath $PpmPath -Force }
if (Test-Path -LiteralPath $PngPath) { Remove-Item -LiteralPath $PngPath -Force }
if (Test-Path -LiteralPath $SerialLog) { Remove-Item -LiteralPath $SerialLog -Force }
$SerialArg = Get-LeonOsQemuSerialFileArg $SerialLog

$Listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$Listener.Start()
$MonitorPort = ([System.Net.IPEndPoint] $Listener.LocalEndpoint).Port
$Listener.Stop()

$StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
$StartInfo.FileName = $Qemu
$StartInfo.Arguments = "-name LeonOS-Browser-$Resolution -machine pc -cpu qemu32 -m 32M -drive file=`"$ImagePath`",format=raw,if=floppy -drive file=`"$HddPath`",format=raw,if=ide,index=0,media=disk -boot a -vga std -display none -serial $SerialArg -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -nic user,model=rtl8139 -no-reboot"
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
    $Writer.WriteLine("sendkey h")
    Wait-QemuMonitorPrompt $Stream 3000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser URL open https://www.google.com/?igu=1&hl=en" 10000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net HTTPS body decoded bytes" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser response complete" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser HTML structure" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser HTML support" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser HTML attrs" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser HTML resources" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser resource queue" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser render blocks" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser DOM nodes" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser CSS rules" 60000
    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser JS scripts" 60000
    $LayoutOutput = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser layout boxes" 10000
    if ($ClickFirstLink) {
        $LinkBoxMatch = [regex]::Match($LayoutOutput, 'LeonOS net browser layout boxes[^\r\n]* firstlink (?<Link>\d+) (?<LX>\d+) (?<LY>\d+) (?<LW>\d+) (?<LH>\d+)')
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
        $ClickX = $ContentX + [int] $LinkBoxMatch.Groups["LX"].Value + [Math]::Max(1, [int] ([int] $LinkBoxMatch.Groups["LW"].Value / 2))
        $ClickY = $ContentY + [int] $LinkBoxMatch.Groups["LY"].Value + [Math]::Max(1, [int] ([int] $LinkBoxMatch.Groups["LH"].Value / 2))

        $InitialMouse = Get-LeonOsInitialMousePosition $Width $Height
        Move-QemuMouseTo $Writer $ClickX $ClickY -Step 80 -FromX $InitialMouse.X -FromY $InitialMouse.Y
        $Writer.WriteLine("mouse_button 0x01")
        Start-Sleep -Milliseconds 160
        $Writer.WriteLine("mouse_button 0")
        $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser link click" 10000
        $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser navigate" 10000
        $null = Wait-LeonOsSerialLog $SerialLog "LeonOS net browser URL open" 10000
    }
    if ($Info) {
        if ($ClickFirstLink) {
            $Btn = $ToolbarH - (Scale-LeonOsY 14 $Height)
            if ($Btn -lt 30) { $Btn = 30 }
            if ($Btn -gt 42) { $Btn = 42 }
            $InfoX = $WinX + $WinW - (Scale-LeonOsX 10 $Width) - $Btn + [int] ($Btn / 2)
            $InfoY = $ClientY + [int] (($ToolbarH - $Btn) / 2) + [int] ($Btn / 2)
            foreach ($Line in (Get-QemuRelativeMouseMoves $InfoX $InfoY -FromX $ClickX -FromY $ClickY -Step 80)) {
                $Writer.WriteLine($Line)
                Start-Sleep -Milliseconds 40
            }
            $Writer.WriteLine("mouse_button 0x01")
            Start-Sleep -Milliseconds 160
            $Writer.WriteLine("mouse_button 0")
        } else {
            $Writer.WriteLine("sendkey i")
            Wait-QemuMonitorPrompt $Stream 3000
        }
    }
    Start-Sleep -Seconds 2
    $Writer.WriteLine("screendump $PpmPath")
    Start-Sleep -Seconds 1
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
        Write-Host "QEMU stderr (LeonOS-Browser-$Resolution):"
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

if (-not (Test-Path -LiteralPath $PpmPath)) {
    throw "QEMU did not produce browser capture '$PpmPath'."
}

$Ppm = Read-Ppm $PpmPath
$ExpectedWidth = if ($Resolution -eq "1080p") { 1920 } else { 1280 }
$ExpectedHeight = if ($Resolution -eq "1080p") { 1080 } else { 720 }
if ($Ppm.Width -ne $ExpectedWidth -or $Ppm.Height -ne $ExpectedHeight) {
    throw "Expected ${ExpectedWidth}x${ExpectedHeight} capture; got $($Ppm.Width)x$($Ppm.Height)."
}

$ProbeW = [Math]::Min($Ppm.Width - 120, 1400)
$ProbeH = [Math]::Min($Ppm.Height - 260, 520)
$TextDark = Count-DarkPixels $Ppm 60 240 $ProbeW $ProbeH
if ($TextDark -lt 300) {
    throw "Browser real-text probe too light; only found $TextDark dark pixels."
}

$PreviewW = if ($Resolution -eq "1080p") { 960 } else { 640 }
$PreviewH = if ($Resolution -eq "1080p") { 540 } else { 360 }
Save-ScaledPng $Ppm $PngPath $PreviewW $PreviewH

Write-Host "Browser capture OK: $PpmPath"
Write-Host "Scaled preview: $PngPath"
Write-Host "Browser extracted text dark pixels: $TextDark"
Write-Host "Serial log: $SerialLog"
