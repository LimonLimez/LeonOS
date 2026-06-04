param(
    [string] $Image = "dist32\leonos32.img",
    [string] $HddImage = "dist32\leonos32-hdd.img",
    [int] $TimeoutSeconds = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")
. (Join-Path $PSScriptRoot "visual-common.ps1")

# The 32-bit kernel still boots from the FAT12 floppy (regression baseline).
# We attach the FAT32 hard disk as the primary IDE master so the kernel's own
# ATA PIO driver can mount it and serve the file browser from FAT32.
& (Join-Path $PSScriptRoot "build32-image.ps1") | Write-Host
& (Join-Path $PSScriptRoot "build32-hdd.ps1") | Write-Host

$Qemu = Get-LeonOsQemu
$ImagePath = Get-LeonOsImagePath $Image
$HddPath = Get-LeonOsImagePath $HddImage
$EscapedImagePath = $ImagePath -replace '"', '\"'
$EscapedHddPath = $HddPath -replace '"', '\"'

function Start-LeonOs32HddTest {
    param(
        [string] $Name,
        [string[]] $Commands
    )

    $SerialLog = Get-LeonOsSerialLogPath $Name
    if (Test-Path -LiteralPath $SerialLog) {
        Remove-Item -LiteralPath $SerialLog -Force
    }
    $SerialArg = Get-LeonOsQemuSerialFileArg $SerialLog

    $Listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $Listener.Start()
    $MonitorPort = ([System.Net.IPEndPoint] $Listener.LocalEndpoint).Port
    $Listener.Stop()

    $StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $StartInfo.FileName = $script:Qemu
    $StartInfo.Arguments = "-name $Name -machine pc -cpu qemu32 -m 32M -drive file=`"$script:EscapedImagePath`",format=raw,if=floppy -drive file=`"$script:EscapedHddPath`",format=raw,if=ide,index=0,media=disk -boot a -vga std -serial $SerialArg -display none -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -no-reboot"
    $StartInfo.UseShellExecute = $false
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

        $null = Wait-LeonOsSerialLog $SerialLog "LeonOS cooperative scheduler OK" 60000
        $Stream = $Client.GetStream()
        $Writer = [System.IO.StreamWriter]::new($Stream)
        $Writer.AutoFlush = $true
        Wait-QemuMonitorPrompt $Stream 8000

        foreach ($Command in $Commands) {
            if ($Command.StartsWith("sleep ")) {
                Start-Sleep -Milliseconds ([int] $Command.Substring(6))
            } else {
                $Writer.WriteLine($Command)
                Wait-QemuMonitorPrompt $Stream 3000
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

        $Stderr = $Process.StandardError.ReadToEnd()
        $Stdout = Read-LeonOsSerialLog $SerialLog

        Write-Host "QEMU serial output ($Name):"
        Write-Host $Stdout
        Write-Host "Serial log: $SerialLog"

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

$KeyboardOutput = Start-LeonOs32HddTest "LeonOS-32BitHddKeyboard" @(
    "sendkey down",
    "sleep 200",
    "sendkey ret"
)

$UiExpect = Get-LeonOsVisualExpectations 1920 1080
$FileClickX = $UiExpect.TitleTextBox.X + 24
$FileClickY = $UiExpect.TitleTextBox.Y + 14
$InitialMouse = Get-LeonOsInitialMousePosition 1920 1080
$MouseCmds = [System.Collections.Generic.List[string]]::new()
foreach ($Move in (Get-QemuRelativeMouseMoves $FileClickX $FileClickY -FromX $InitialMouse.X -FromY $InitialMouse.Y -Step 60)) {
    $MouseCmds.Add($Move) | Out-Null
    $MouseCmds.Add("sleep 300") | Out-Null
}
$MouseCmds.Add("sleep 300")
$MouseCmds.Add("mouse_button 0x01")
$MouseCmds.Add("sleep 200")
$MouseCmds.Add("mouse_move 1 0")
$MouseCmds.Add("sleep 120")
$MouseCmds.Add("mouse_button 0")
$MouseCmds.Add("sleep 120")
$MouseCmds.Add("mouse_move -1 0")
$MouseOutput = Start-LeonOs32HddTest "LeonOS-32BitHddMouse" $MouseCmds.ToArray()

$CombinedOutput = $KeyboardOutput + "`n" + $MouseOutput
$ExpectedLines = @(
    "LeonOS stage2 entered",
    "LeonOS stage2 VBE 1920x1080x32 ready",
    "LeonOS stage2 entering protected mode",
    "LeonOS physical page allocator OK",
    "LeonOS 32-bit FAT32 volume mounted",
    "LeonOS 32-bit GUI ready",
    "LeonOS shell ready",
    "LeonOS cooperative scheduler OK"
)

foreach ($Expected in $ExpectedLines) {
    if ($CombinedOutput -notlike "*$Expected*") {
        throw "QEMU did not emit expected serial line: $Expected"
    }
}

if ($CombinedOutput -like "*LeonOS 32-bit FAT12 root loaded*") {
    throw "Kernel fell back to FAT12 even though a FAT32 disk was attached."
}

if ($KeyboardOutput -notlike "*NOTES32.TXT*" -or $KeyboardOutput -notlike "*287*") {
    throw "Expected keyboard input to open a FAT32 file."
}
if ($MouseOutput -notlike "*LeonOS shell ready*") {
    throw "Expected desktop shell ready during the mouse test run."
}
if ($MouseOutput -notlike "*HELLO32.TXT*" -or $MouseOutput -notlike "*235*") {
    throw "Expected mouse input to open a FAT32 file."
}

Write-Host "QEMU 32-bit FAT32 HDD boot/read test passed."
