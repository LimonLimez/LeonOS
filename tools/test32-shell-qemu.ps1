param(
    [string] $Image = "dist32\leonos32.img",
    [string] $HddImage = "dist32\leonos32-hdd.img",
    [int] $TimeoutSeconds = 60
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
$SerialLog = Get-LeonOsSerialLogPath "test32-shell-qemu"
if (Test-Path -LiteralPath $SerialLog) {
    Remove-Item -LiteralPath $SerialLog -Force
}
$SerialArg = Get-LeonOsQemuSerialFileArg $SerialLog

function Start-LeonOs32ShellTest {
    param([string] $Name, [string[]] $Commands)

    $Listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $Listener.Start()
    $MonitorPort = ([System.Net.IPEndPoint] $Listener.LocalEndpoint).Port
    $Listener.Stop()

    $StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $StartInfo.FileName = $script:Qemu
    $StartInfo.Arguments = "-name $Name -machine pc -cpu qemu32 -m 32M -drive file=`"$script:EscapedImagePath`",format=raw,if=floppy -drive file=`"$script:EscapedHddPath`",format=raw,if=ide,index=0,media=disk -boot a -serial $script:SerialArg -display none -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -no-reboot"
    $StartInfo.UseShellExecute = $false
    $StartInfo.RedirectStandardError = $true

    $Process = [System.Diagnostics.Process]::Start($StartInfo)
    $Client = $null

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

        $null = Wait-LeonOsSerialLog $script:SerialLog "LeonOS cooperative scheduler OK" 60000

        $Stream = $Client.GetStream()
        $Writer = [System.IO.StreamWriter]::new($Stream)
        $Writer.AutoFlush = $true
        Wait-QemuMonitorPrompt $Stream 8000
        Start-Sleep -Milliseconds 300
        foreach ($Command in $Commands) {
            if ($Command.StartsWith("sleep ")) {
                Start-Sleep -Milliseconds ([int] $Command.Substring(6))
            } else {
                $Writer.WriteLine($Command)
                Wait-QemuMonitorPrompt $Stream 3000
            }
        }
        Start-Sleep -Seconds 4
        $Writer.WriteLine("quit")
        $Writer.Dispose()
        $Client.Close()
        $Client = $null

        $Exited = $Process.WaitForExit($script:TimeoutSeconds * 1000)
        if (-not $Exited) {
            $Process.Kill()
            $Process.WaitForExit()
        }
        Start-Sleep -Milliseconds 300

        $Stderr = $Process.StandardError.ReadToEnd()
        if ($Stderr.Trim().Length -gt 0) {
            Write-Host "QEMU stderr ($Name):"
            Write-Host $Stderr
        }
    } finally {
        if ($Client) { $Client.Close() }
        if (-not $Process.HasExited) {
            $Process.Kill()
            $Process.WaitForExit()
        }
    }

    return Read-LeonOsSerialLog $script:SerialLog
}

# Shortcuts match shell_ui.inc.c:
# S=start, T=tab, Alt+X=max/restore, F3/F4/F5=window apps, F8/F9/F10=real app actions.
$Output = Start-LeonOs32ShellTest "LeonOS-32BitShell" @(
    "sendkey s",
    "sleep 1000",
    "sendkey s",
    "sleep 1000",
    "sendkey t",
    "sleep 1000",
    "sendkey alt-x",
    "sleep 1000",
    "sendkey alt-x",
    "sleep 1000",
    "sendkey f7",
    "sleep 1000",
    "sendkey f3",
    "sleep 1000",
    "sendkey f4",
    "sleep 1000",
    "sendkey f5",
    "sleep 1000",
    "sendkey c",
    "sleep 1000",
    "sendkey f9",
    "sleep 5000",
    "sendkey f8",
    "sleep 1800",
    "sendkey f10",
    "sleep 2200",
    "sendkey m",
    "sleep 1000",
    "sendkey m",
    "sleep 1000"
)

Write-Host $Output

$Expected = @(
    "LeonOS shell ready",
    "LeonOS cooperative scheduler OK",
    "LeonOS shell start-menu open",
    "LeonOS shell start-menu closed",
    "LeonOS shell tab=1",
    "LeonOS shell win maximized",
    "LeonOS shell win restored",
    "LeonOS shell app=apps",
    "LeonOS shell app=log",
    "LeonOS shell app=about",
    "LeonOS shell win closed",
    "LeonOS shell app=hello",
    "LeonOS shell app=uhello",
    "LeonOS shell app=write",
    "HELLOAPP ran",
    "UHELLO ran via syscall",
    "LeonOS 32-bit FAT32 write readback OK",
    "LeonOS shell win minimized",
    "LeonOS shell win moved"
)
foreach ($Line in $Expected) {
    if ($Output -notlike "*$Line*") {
        throw "Missing shell serial proof: $Line"
    }
}

foreach ($Bad in @("CPU exception", "PANIC", "unhandled")) {
    if ($Output -like "*$Bad*") {
        throw "Shell test hit fault: $Bad"
    }
}

Write-Host "QEMU 32-bit desktop shell test passed."
Write-Host "Serial log: $SerialLog"
