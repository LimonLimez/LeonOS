param(
    [string] $Image = "dist32\leonos32.img",
    [int] $TimeoutSeconds = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")
. (Join-Path $PSScriptRoot "visual-common.ps1")

& (Join-Path $PSScriptRoot "build32-image.ps1") | Write-Host

$Qemu = Get-LeonOsQemu
$ImagePath = Get-LeonOsImagePath $Image
$EscapedImagePath = $ImagePath -replace '"', '\"'

function Start-LeonOs32InputTest {
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
    $StartInfo.Arguments = "-name $Name -machine pc -cpu qemu32 -m 32M -drive file=`"$script:EscapedImagePath`",format=raw,if=floppy -boot a -serial stdio -display none -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -no-reboot"
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

        Start-Sleep -Seconds 3
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

$KeyboardOutput = Start-LeonOs32InputTest "LeonOS-32BitKeyboard" @(
    "sendkey down",
    "sleep 200",
    "sendkey ret"
)

$UiExpect = Get-LeonOsVisualExpectations 1920 1080
$TitleClickX = $UiExpect.TitleBar.X
$TitleClickY = $UiExpect.TitleBar.Y
$FileClickX = $UiExpect.TitleTextBox.X + 24
$FileClickY = $UiExpect.TitleTextBox.Y + 14
$InitialMouse = Get-LeonOsInitialMousePosition 1920 1080
$MouseCmds = [System.Collections.Generic.List[string]]::new()
foreach ($Move in (Get-QemuRelativeMouseMoves $TitleClickX $TitleClickY -FromX $InitialMouse.X -FromY $InitialMouse.Y)) {
    $MouseCmds.Add($Move) | Out-Null
    $MouseCmds.Add("sleep 60") | Out-Null
}
$MouseCmds.Add("sleep 200")
$MouseCmds.Add("mouse_button 1")
$MouseCmds.Add("sleep 120")
$MouseCmds.Add("mouse_button 0")
$MouseCmds.Add("sleep 200")
foreach ($Move in (Get-QemuRelativeMouseMoves $FileClickX $FileClickY $TitleClickX $TitleClickY)) {
    $MouseCmds.Add($Move) | Out-Null
    $MouseCmds.Add("sleep 60") | Out-Null
}
$MouseCmds.Add("sleep 300")
$MouseCmds.Add("mouse_button 1")
$MouseCmds.Add("sleep 200")
$MouseCmds.Add("mouse_button 0")
$MouseOutput = Start-LeonOs32InputTest "LeonOS-32BitMouse" $MouseCmds.ToArray()

$CombinedOutput = $KeyboardOutput + "`n" + $MouseOutput
$ExpectedLines = @(
    "LeonOS stage2 entered",
    "LeonOS stage2 VBE 1920x1080x32 ready",
    "LeonOS 32-bit framebuffer 1920x1080x32",
    "LeonOS stage2 FAT12 buffers ready",
    "LeonOS stage2 entering protected mode",
    "LeonOS stage2 protected mode OK",
    "LeonOS 32-bit kernel entered",
    "LeonOS 32-bit IDT/PIC/PIT OK",
    "LeonOS 32-bit BootInfo OK",
    "LeonOS memory map entries:",
    "LeonOS physical page allocator OK",
    "LeonOS physical page alloc/free OK",
    "LeonOS paging enabled",
    "LeonOS page-backed heap OK",
    "LeonOS kernel heap OK",
    "LeonOS 32-bit PS/2 mouse OK",
    "LeonOS 32-bit FAT12 root loaded",
    "LeonOS kernel tasks registered",
    "LeonOS 32-bit GUI ready",
    "LeonOS timer/events OK",
    "LeonOS cooperative scheduler OK"
)

foreach ($Expected in $ExpectedLines) {
    if ($CombinedOutput -notlike "*$Expected*") {
        throw "QEMU did not emit expected serial line: $Expected"
    }
}

if ($KeyboardOutput -notlike "*LeonOS 32-bit FAT12 file opened*") {
    throw "Expected keyboard input to open a FAT12 file."
}
if ($MouseOutput -notlike "*LeonOS 32-bit PS/2 mouse OK*") {
    throw "Expected PS/2 mouse initialization in the mouse test run."
}
if ($MouseOutput -notlike "*LeonOS shell ready*") {
    throw "Expected desktop shell ready during the mouse test run."
}

Write-Host "QEMU 32-bit boot/input test passed."
