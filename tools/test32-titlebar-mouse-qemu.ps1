param(
    [string] $Image = "dist32\leonos32.img",
    [string] $HddImage = "dist32\leonos32-hdd.img",
    [ValidateSet("1080p", "720p")]
    [string] $Resolution = "1080p",
    [int] $TimeoutSeconds = 60
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")
. (Join-Path $PSScriptRoot "visual-common.ps1")

& (Join-Path $PSScriptRoot "build32-image.ps1") -Resolution $Resolution | Write-Host
& (Join-Path $PSScriptRoot "build32-hdd.ps1") | Write-Host

$Size = Get-LeonOsResolutionSize $Resolution
$Sx = { param([int] $V) Scale-LeonOsX $V $Size.Width }
$Sy = { param([int] $V) Scale-LeonOsY $V $Size.Height }

$TaskbarH = if ($Size.Height -le 720) { (& $Sy 52) } else { (& $Sy 48) }
if ($TaskbarH -lt 40) { $TaskbarH = 40 }
$TaskbarY = $Size.Height - $TaskbarH
$TitleH = if ($Size.Height -le 720) { (& $Sy 48) } else { (& $Sy 42) }
if ($TitleH -lt 36) { $TitleH = 36 }
$BtnW = if ($Size.Height -le 720) { (& $Sx 72) } else { (& $Sx 54) }
if ($BtnW -lt 40) { $BtnW = 40 }

$AboutX = (& $Sx 150)
$AboutY = (& $Sy 110)
$AboutW = (& $Sx 520)
$AboutH = (& $Sy 310)
if ($AboutW -lt (& $Sx 300)) { $AboutW = (& $Sx 300) }
if ($AboutH -lt (& $Sy 220)) { $AboutH = (& $Sy 220) }
$PadX = (& $Sx 8)
$PadY = (& $Sy 8)
if ($AboutX -lt $PadX) { $AboutX = $PadX }
if ($AboutY -lt $PadY) { $AboutY = $PadY }
if (($AboutX + $AboutW) -gt ($Size.Width - $PadX)) { $AboutX = $Size.Width - $PadX - $AboutW }
if (($AboutY + $AboutH) -gt ($TaskbarY - $PadY)) { $AboutY = $TaskbarY - $PadY - $AboutH }

$MinX = $AboutX + $AboutW - $BtnW * 3 + [int]($BtnW / 2)
$CloseX = $AboutX + $AboutW - [int]($BtnW / 2)
$ChromeY = $AboutY + [int]($TitleH / 2)
$GhostDragX = $AboutX + $AboutW - [int]($BtnW * 2)
$GhostDragY = $ChromeY

$Qemu = Get-LeonOsQemu
$ImagePath = Get-LeonOsImagePath $Image
$HddPath = Get-LeonOsImagePath $HddImage
$EscapedImagePath = $ImagePath -replace '"', '\"'
$EscapedHddPath = $HddPath -replace '"', '\"'
$SerialLog = Get-LeonOsSerialLogPath "test32-titlebar-mouse-qemu"
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
$StartInfo.Arguments = "-name LeonOS-TitlebarMouse-$Resolution -machine pc -cpu qemu32 -m 32M -drive file=`"$EscapedImagePath`",format=raw,if=floppy -drive file=`"$EscapedHddPath`",format=raw,if=ide,index=0,media=disk -boot a -serial $SerialArg -display none -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -no-reboot"
$StartInfo.UseShellExecute = $false
$StartInfo.RedirectStandardError = $true

$Process = [System.Diagnostics.Process]::Start($StartInfo)
$Client = $null
$Stream = $null
$Writer = $null
$InitialMouse = Get-LeonOsInitialMousePosition $Size.Width $Size.Height
$MouseX = $InitialMouse.X
$MouseY = $InitialMouse.Y

function Send-MonitorCommand {
    param([string] $Command, [int] $PromptTimeoutMs = 3000)
    $script:Writer.WriteLine($Command)
    Wait-QemuMonitorPrompt $script:Stream $PromptTimeoutMs
}

function Move-TrackedMouse {
    param([int] $TargetX, [int] $TargetY)
    foreach ($Line in (Get-QemuRelativeMouseMoves $TargetX $TargetY -FromX $script:MouseX -FromY $script:MouseY -Step 50)) {
        Send-MonitorCommand $Line
        Start-Sleep -Milliseconds 40
    }
    $script:MouseX = $TargetX
    $script:MouseY = $TargetY
}

function Click-TrackedMouse {
    Send-MonitorCommand "mouse_button 0x01"
    Start-Sleep -Milliseconds 160
    Send-MonitorCommand "mouse_button 0"
    Start-Sleep -Milliseconds 260
}

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

    $null = Wait-LeonOsSerialLog $SerialLog "LeonOS cooperative scheduler OK" 60000
    $Stream = $Client.GetStream()
    $Writer = [System.IO.StreamWriter]::new($Stream)
    $Writer.AutoFlush = $true
    Wait-QemuMonitorPrompt $Stream 8000

    Send-MonitorCommand "sendkey f5"
    Start-Sleep -Milliseconds 1000
    Move-TrackedMouse $MinX $ChromeY
    Click-TrackedMouse

    Move-TrackedMouse $GhostDragX $GhostDragY
    Send-MonitorCommand "mouse_button 0x01"
    Start-Sleep -Milliseconds 120
    Move-TrackedMouse ($GhostDragX + 90) $GhostDragY
    Send-MonitorCommand "mouse_button 0"
    Start-Sleep -Milliseconds 500

    Send-MonitorCommand "sendkey f5"
    Start-Sleep -Milliseconds 1000
    Move-TrackedMouse $CloseX $ChromeY
    Click-TrackedMouse

    Start-Sleep -Milliseconds 1000
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
        Write-Host "QEMU stderr (LeonOS-TitlebarMouse-$Resolution):"
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

foreach ($Line in @(
    "LeonOS shell ready",
    "LeonOS shell app=about",
    "LeonOS shell win minimized",
    "LeonOS shell win restored",
    "LeonOS shell win closed"
)) {
    if ($Output -notlike "*$Line*") {
        throw "Missing titlebar mouse serial proof: $Line"
    }
}

if ($Output -like "*LeonOS shell win moved*") {
    throw "Unexpected ghost titlebar drag: minimized/closed chrome still generated a win moved event."
}

Write-Host "QEMU titlebar mouse button regression test passed ($Resolution / $($Size.Width)x$($Size.Height))."
Write-Host "Serial log: $SerialLog"
