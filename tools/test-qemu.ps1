param(
    [string] $Image = "dist\leonos.img",
    [int] $TimeoutSeconds = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")

& (Join-Path $PSScriptRoot "build.ps1") | Write-Host

$Qemu = Get-LeonOsQemu
$ImagePath = Get-LeonOsImagePath $Image
$EscapedImagePath = $ImagePath -replace '"', '\"'

function Start-LeonOsQemuInputTest {
    param(
        [string] $Name,
        [string[]] $Commands
    )

    $Listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $Listener.Start()
    $MonitorPort = ([System.Net.IPEndPoint] $Listener.LocalEndpoint).Port
    $Listener.Stop()

    $StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $StartInfo.FileName = $script:Qemu
    $StartInfo.Arguments = "-name $Name -machine pc -cpu qemu32 -m 16M -drive file=`"$script:EscapedImagePath`",format=raw,if=floppy -boot a -serial stdio -display none -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -no-reboot"
    $StartInfo.UseShellExecute = $false
    $StartInfo.RedirectStandardOutput = $true
    $StartInfo.RedirectStandardError = $true

    $Process = [System.Diagnostics.Process]::Start($StartInfo)
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
            throw "Could not connect to QEMU monitor on port $MonitorPort."
        }

        Start-Sleep -Seconds 8
        $Stream = $Client.GetStream()
        $Writer = [System.IO.StreamWriter]::new($Stream)
        $Writer.AutoFlush = $true

        foreach ($Command in $Commands) {
            if ($Command.StartsWith("sleep ")) {
                Start-Sleep -Milliseconds ([int] $Command.Substring(6))
            } else {
                $Writer.WriteLine($Command)
            }
        }

        Start-Sleep -Seconds 5
        $Writer.WriteLine("quit")
        $Writer.Dispose()
        $Client.Close()
        $Client = $null

        $Exited = $Process.WaitForExit($script:TimeoutSeconds * 1000)
        if (-not $Exited) {
            $Process.Kill()
            $Process.WaitForExit()
        }

        $Stdout = $Process.StandardOutput.ReadToEnd()
        $Stderr = $Process.StandardError.ReadToEnd()

        Write-Host "QEMU serial output ($Name):"
        Write-Host $Stdout

        if ($Stderr.Trim().Length -gt 0) {
            Write-Host "QEMU stderr ($Name):"
            Write-Host $Stderr
        }

        return $Stdout
    } finally {
        if ($Client) {
            $Client.Close()
        }
        if (-not $Process.HasExited) {
            $Process.Kill()
            $Process.WaitForExit()
        }
    }
}

$KeyboardOutput = Start-LeonOsQemuInputTest "LeonOS-KeyboardTest" @(
    "sendkey down",
    "sleep 200",
    "sendkey ret"
)

$MouseOutput = Start-LeonOsQemuInputTest "LeonOS-MouseTest" @(
    "mouse_move -100 -45",
    "sleep 200",
    "mouse_button 1",
    "sleep 200",
    "mouse_button 0"
)

$CombinedOutput = $KeyboardOutput + "`n" + $MouseOutput
$ExpectedLines = @(
    "LeonOS kernel loaded",
    "FAT12 root loaded",
    "VBE 1920x1080x32 framebuffer ready",
    "LeonOS GUI ready"
)

foreach ($Expected in $ExpectedLines) {
    if ($CombinedOutput -notlike "*$Expected*") {
        throw "QEMU did not emit expected serial line: $Expected"
    }
}

foreach ($NameAndOutput in @(
    @{ Name = "keyboard"; Output = $KeyboardOutput },
    @{ Name = "mouse"; Output = $MouseOutput }
)) {
    if ($NameAndOutput.Output -notlike "*FAT12 file opened*") {
        throw "Expected $($NameAndOutput.Name) input to open a FAT12 file."
    }
}

Write-Host "QEMU boot/input test passed."

