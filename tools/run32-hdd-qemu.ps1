param(
    [string] $Image = "dist32\leonos32.img",
    [string] $HddImage = "dist32\leonos32-hdd.img",
    [ValidateSet("Native1080p", "Small720p", "Small720pNet", "ScaledMaximize")]
    [string] $Mode = "Small720p",
    [switch] $Network,
    [switch] $FullScreen
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")

$Resolution = switch ($Mode) {
    "Native1080p" { "1080p" }
    "Small720p" { "720p" }
    "Small720pNet" { "720p" }
    "ScaledMaximize" { "1080p" }
    default { throw "Unknown Mode '$Mode'." }
}

& (Join-Path $PSScriptRoot "build32-image.ps1") -Resolution $Resolution | Write-Host
& (Join-Path $PSScriptRoot "build32-hdd.ps1") | Write-Host

$Qemu = Get-LeonOsQemu
$ImagePath = Get-LeonOsImagePath $Image
$HddPath = Get-LeonOsImagePath $HddImage

$DisplayArg = switch ($Mode) {
    "Native1080p" { "gtk,zoom-to-fit=off,show-menubar=off,grab-on-hover=on,show-cursor=off" }
    "Small720p" { "gtk,zoom-to-fit=on,show-menubar=off,grab-on-hover=on,show-cursor=off" }
    "Small720pNet" { "gtk,zoom-to-fit=on,show-menubar=off,grab-on-hover=on,show-cursor=off" }
    "ScaledMaximize" { "gtk,zoom-to-fit=on,show-menubar=off,grab-on-hover=on,show-cursor=off" }
    default { "gtk,zoom-to-fit=off,show-menubar=off,grab-on-hover=on,show-cursor=off" }
}

$Size = switch ($Resolution) {
    "1080p" { @{ W = 1920; H = 1080 } }
    "720p" { @{ W = 1280; H = 720 } }
    "768p" { @{ W = 1024; H = 768 } }
    "1366" { @{ W = 1366; H = 768 } }
    default { @{ W = 1920; H = 1080 } }
}

$Arguments = @(
    "-name", "LeonOS-32BitHdd-$Mode",
    "-machine", "pc",
    "-cpu", "qemu32",
    "-m", "32M",
    "-drive", "file=$ImagePath,format=raw,if=floppy",
    "-drive", "file=$HddPath,format=raw,if=ide,index=0,media=disk",
    "-boot", "a",
    "-vga", "std",
    "-display", $DisplayArg
) + @(
    "-serial", "stdio",
    "-monitor", "none",
    "-no-reboot"
)

if ($Network -or $Mode -eq "Small720pNet") {
    $Arguments += @("-nic", "user,model=rtl8139")
}

if ($FullScreen) {
    $Arguments += "-full-screen"
}

Write-Host "LeonOS 32-bit FAT32 HDD runner: Mode=$Mode guest preference=$Resolution ($($Size.W)x$($Size.H))."
if ($Network -or $Mode -eq "Small720pNet") {
    Write-Host "Networking: QEMU user-mode RTL8139 enabled. Guest static IP is 10.0.2.15."
}
if ($Mode -eq "ScaledMaximize") {
    Write-Host "Host GTK zoom-to-fit scales the guest framebuffer; resizing the QEMU window does NOT change guest VBE mode."
} elseif ($Mode -eq "Small720p" -or $Mode -eq "Small720pNet") {
    Write-Host "Guest VBE prefers 1280x720 with GTK zoom-to-fit so the full desktop stays visible."
    Write-Host "Live keys: S/F1 start menu, T/F2 tab, F11 browser, M min, X max, C close."
} else {
    Write-Host "Guest VBE prefers 1920x1080 native 1:1 window (no zoom-to-fit)."
}
Write-Host "Close the QEMU window to stop."
Write-Host "Mouse: hover or click inside LeonOS to grab/lock the pointer; press Ctrl+Alt+G to release it."

& $Qemu @Arguments
