param(
    [string] $Image = "dist32\leonos32.img",
    [string] $HddImage = "dist32\leonos32-hdd.img",
    [int] $TimeoutSeconds = 25
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")

# Boots the FAT12 floppy with the FAT32 hard disk attached, then presses 'c' so
# the kernel loads UCDEMO.LEO from FAT32. UCDEMO is compiled from freestanding C
# through the LEO1 crt0/linker path and uses only the public syscall ABI.
& (Join-Path $PSScriptRoot "build32-image.ps1") | Write-Host
& (Join-Path $PSScriptRoot "build32-hdd.ps1") | Write-Host

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$AppPath = Join-Path $Root "dist32\UCDEMO.LEO"
$AppBytes = [System.IO.File]::ReadAllBytes($AppPath)
if ($AppBytes.Length -lt 32 -or
    $AppBytes[0] -ne 0x4C -or $AppBytes[1] -ne 0x45 -or
    $AppBytes[2] -ne 0x4F -or $AppBytes[3] -ne 0x31) {
    throw "UCDEMO.LEO is missing the LEO1 header."
}
if ($AppBytes[4] -ne 0x02) {
    throw "UCDEMO.LEO must declare LEO1 ABI version 2."
}

$Qemu = Get-LeonOsQemu
$ImagePath = Get-LeonOsImagePath $Image
$HddPath = Get-LeonOsImagePath $HddImage
$EscapedImagePath = $ImagePath -replace '"', '\"'
$EscapedHddPath = $HddPath -replace '"', '\"'

function Start-LeonOs32UserCTest {
    param([string] $Name, [string[]] $Commands)

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
    $StartInfo.Arguments = "-name $Name -machine pc -cpu qemu32 -m 32M -drive file=`"$script:EscapedImagePath`",format=raw,if=floppy -drive file=`"$script:EscapedHddPath`",format=raw,if=ide,index=0,media=disk -boot a -serial $SerialArg -display none -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -no-reboot"
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

        # Wait for the desktop shell instead of sleeping a fixed time so a
        # slow boot cannot swallow the injected keystrokes.
        $null = Wait-LeonOsSerialLog $SerialLog "LeonOS shell ready" 60000
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

        $null = Wait-LeonOsSerialLog $SerialLog "LeonOS user app returned to kernel" 15000
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

$Output = Start-LeonOs32UserCTest "LeonOS-32BitHddUserC" @(
    "sendkey ctrl-c",
    "sleep 1500"
)

$ExpectedLines = @(
    "LeonOS stage2 VBE 1920x1080x32 ready",
    "LeonOS 32-bit FAT32 volume mounted",
    "LeonOS 32-bit GUI ready",
    "LeonOS shell ready",
    "LeonOS user mode enter UCDEMO.LEO",
    "UCDEMO C userland app",
    "LeonOS user fb info",
    "LeonOS user fb fill",
    "LeonOS user fb present",
    "LeonOS user app exited",
    "LeonOS user app returned to kernel"
)
foreach ($Expected in $ExpectedLines) {
    if ($Output -notlike "*$Expected*") {
        throw "QEMU did not emit expected serial line: $Expected"
    }
}

foreach ($Bad in @(
    "user app not found", "user app bad", "user app read failed",
    "user app too large", "user app no allocator", "user app no memory",
    "user syscall bad ptr", "user syscall bad number",
    "CPU exception", "PANIC")) {
    if ($Output -like "*$Bad*") {
        throw "Kernel reported a C user-mode app failure: $Bad"
    }
}

Write-Host "QEMU 32-bit freestanding C user-mode app test passed."
