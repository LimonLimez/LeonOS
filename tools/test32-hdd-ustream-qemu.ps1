param(
    [string] $Image = "dist32\leonos32.img",
    [string] $HddImage = "dist32\leonos32-hdd.img",
    [int] $TimeoutSeconds = 600
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
$StartInfo.Arguments = "-name LeonOS-UStream-Test -machine pc -cpu qemu32 -m 32M -drive file=`"$EscapedImagePath`",format=raw,if=floppy -drive file=`"$EscapedHddPath`",format=raw,if=ide,index=0,media=disk -boot a -serial stdio -display none -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -nic user,model=rtl8139 -no-reboot"
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
    if (-not $Client) {
        throw "Could not connect to QEMU monitor."
    }

    $Stream = $Client.GetStream()
    $Writer = [System.IO.StreamWriter]::new($Stream)
    $Writer.AutoFlush = $true

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
    $Writer.WriteLine("sendkey ctrl-s")

    $Deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $SawDone = $false
    while ([DateTime]::UtcNow -lt $Deadline -and -not $Process.HasExited) {
        $Line = $Process.StandardOutput.ReadLine()
        if ($null -eq $Line) {
            break
        }
        [void] $Output.AppendLine($Line)
        if ($Line.Contains("USTREAM total ")) {
            $SawDone = $true
        }
        if ($SawDone -and $Line.Contains("LeonOS user app returned to kernel")) {
            break
        }
    }

    $Writer.WriteLine("quit")
    $Writer.Dispose()
    $Client.Close()
    $Client = $null

    $Exited = $Process.WaitForExit(10000)
    if (-not $Exited) {
        $Process.Kill()
        $Process.WaitForExit()
    }

    [void] $Output.Append($Process.StandardOutput.ReadToEnd())
    $Text = $Output.ToString()
    $Stderr = $Process.StandardError.ReadToEnd()
    Write-Host "QEMU serial output (LeonOS-UStream-Test):"
    Write-Host $Text
    if ($Stderr.Trim().Length -gt 0) {
        Write-Host "QEMU stderr:"
        Write-Host $Stderr
    }

    foreach ($Needle in @(
        "LeonOS stage2 VBE 1920x1080x32 ready",
        "LeonOS 32-bit FAT32 volume mounted",
        "LeonOS user mode enter USTREAM.LEO",
        "USTREAM user HTTPS stream",
        "LeonOS user net stream open https://www.google.com/",
        "USTREAM open handle 1",
        "LeonOS user net fetch meta status 200 type text/html",
        "LeonOS user net stream read bytes",
        "USTREAM chunk ",
        "USTREAM meta status 200",
        "USTREAM total ",
        "LeonOS user net stream close",
        "LeonOS user app returned to kernel"
    )) {
        if (-not $Text.Contains($Needle)) {
            throw "Missing serial proof '$Needle'. Output:`n$Text"
        }
    }

    foreach ($Bad in @(
        "user app not found", "user app bad", "user app read failed",
        "user app no memory", "user page range bad", "user syscall bad number",
        "USTREAM open failed", "USTREAM meta failed", "USTREAM final state 17",
        "LeonOS net browser render blocks", "LeonOS net browser unsupported banner",
        "CPU exception", "PANIC")) {
        if ($Text.Contains($Bad)) {
            throw "Kernel reported a USTREAM failure: $Bad"
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

Write-Host "test32-hdd-ustream-qemu.ps1 passed."
