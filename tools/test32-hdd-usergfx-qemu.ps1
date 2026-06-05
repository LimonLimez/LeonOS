param(
    [string] $Image = "dist32\leonos32.img",
    [string] $HddImage = "dist32\leonos32-hdd.img",
    [int] $TimeoutSeconds = 25
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")

# Boots the FAT12 floppy with the FAT32 hard disk attached, then presses 'g' so
# the kernel loads UGFX.LEO from FAT32. UGFX is a ring-3 LEO1 v2 app that draws
# through int 0x80 framebuffer syscalls. This is the first browser-port
# prerequisite: user mode can talk to a small graphics/event ABI without
# receiving kernel function pointers.
& (Join-Path $PSScriptRoot "build32-image.ps1") | Write-Host
& (Join-Path $PSScriptRoot "build32-hdd.ps1") | Write-Host

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$UgfxPath = Join-Path $Root "dist32\UGFX.LEO"
$UgfxSize = (Get-Item -LiteralPath $UgfxPath).Length
if ($UgfxSize -le 4096) {
    throw "UGFX.LEO must be larger than one page to exercise the multi-page user app loader."
}

$Qemu = Get-LeonOsQemu
$ImagePath = Get-LeonOsImagePath $Image
$HddPath = Get-LeonOsImagePath $HddImage
$EscapedImagePath = $ImagePath -replace '"', '\"'
$EscapedHddPath = $HddPath -replace '"', '\"'

function Start-LeonOs32UserGfxTest {
    param([string] $Name, [string[]] $Commands)

    $Listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $Listener.Start()
    $MonitorPort = ([System.Net.IPEndPoint] $Listener.LocalEndpoint).Port
    $Listener.Stop()

    $StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $StartInfo.FileName = $script:Qemu
    $StartInfo.Arguments = "-name $Name -machine pc -cpu qemu32 -m 32M -drive file=`"$script:EscapedImagePath`",format=raw,if=floppy -drive file=`"$script:EscapedHddPath`",format=raw,if=ide,index=0,media=disk -boot a -serial stdio -display none -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -no-reboot"
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

$Output = Start-LeonOs32UserGfxTest "LeonOS-32BitHddUserGfx" @(
    "sendkey g",
    "sleep 1500"
)

$ExpectedLines = @(
    "LeonOS stage2 VBE 1920x1080x32 ready",
    "LeonOS 32-bit FAT32 volume mounted",
    "LeonOS 32-bit GUI ready",
    "LeonOS shell ready",
    "LeonOS user mode enter UGFX.LEO",
    "UGFX framebuffer syscall app",
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
        throw "Kernel reported a user-mode framebuffer failure: $Bad"
    }
}

Write-Host "QEMU 32-bit ring-3 framebuffer syscall app test passed."
