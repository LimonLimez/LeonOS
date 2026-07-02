param(
    [string] $Image = "dist32\leonos32.img",
    [string] $HddImage = "dist32\leonos32-hdd.img",
    [int] $TimeoutSeconds = 120
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")

& (Join-Path $PSScriptRoot "build32-image.ps1") | Write-Host
& (Join-Path $PSScriptRoot "build32-hdd.ps1") | Write-Host

$Qemu = Get-LeonOsQemu
$ImagePath = Get-LeonOsImagePath $Image
$HddPath = Get-LeonOsImagePath $HddImage
$EscapedImagePath = $ImagePath -replace '"', '\"'
$EscapedHddPath = $HddPath -replace '"', '\"'

$Listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$Listener.Start()
$MonitorPort = ([System.Net.IPEndPoint] $Listener.LocalEndpoint).Port
$Listener.Stop()

$StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
$StartInfo.FileName = $Qemu
$StartInfo.Arguments = "-name LeonOS-UNetRun-Test -machine pc -cpu qemu32 -m 32M -drive file=`"$EscapedImagePath`",format=raw,if=floppy -drive file=`"$EscapedHddPath`",format=raw,if=ide,index=0,media=disk -boot a -serial stdio -display none -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -nic user,model=rtl8139 -no-reboot"
$StartInfo.UseShellExecute = $false
$StartInfo.RedirectStandardOutput = $true
$StartInfo.RedirectStandardError = $true

$Process = [System.Diagnostics.Process]::Start($StartInfo)
$Output = [System.Text.StringBuilder]::new()
$Client = $null

try {
    for ($Attempt = 0; $Attempt -lt 20 -and -not $Client; $Attempt++) {
        try {
            Start-Sleep -Milliseconds 150
            $Client = [System.Net.Sockets.TcpClient]::new("127.0.0.1", $MonitorPort)
        } catch {
            $Client = $null
        }
    }
    if (-not $Client) { throw "Could not connect to QEMU monitor." }

    $Stream = $Client.GetStream()
    $Writer = [System.IO.StreamWriter]::new($Stream)
    $Writer.AutoFlush = $true

    # Wait for the desktop shell before injecting the hotkey so a slow boot
    # cannot swallow it.
    $BootDeadline = [DateTime]::UtcNow.AddSeconds(60)
    while ([DateTime]::UtcNow -lt $BootDeadline -and -not $Process.HasExited) {
        $Line = $Process.StandardOutput.ReadLine()
        if ($null -eq $Line) {
            break
        }
        [void] $Output.AppendLine($Line)
        if ($Line.Contains("LeonOS shell ready")) {
            break
        }
    }
    Start-Sleep -Seconds 1
    $Writer.WriteLine("sendkey n")

    $Deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $Deadline -and -not $Process.HasExited) {
        $Line = $Process.StandardOutput.ReadLine()
        if ($null -eq $Line) {
            break
        }
        [void] $Output.AppendLine($Line)
        # Stop once the last proof line has arrived instead of waiting out
        # the full deadline.
        if ($Line.Contains("LeonOS net browser URL open")) {
            break
        }
    }
} finally {
    if ($Client) { $Client.Close() }
    if (-not $Process.HasExited) {
        $Process.Kill()
        $Process.WaitForExit()
    }
}

$Text = $Output.ToString()
$Needles = @(
    "UNETRUN NetSurf port runtime",
    "UNETRUN runtime ticks",
    "LeonOS user browser open queued",
    "LeonOS net browser URL open"
)
foreach ($Needle in $Needles) {
    if (-not $Text.Contains($Needle)) {
        throw "Missing serial proof '$Needle'. Output:`n$Text"
    }
}
Write-Host "test32-hdd-unetsurf-qemu.ps1 passed."
