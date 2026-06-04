param(
    [string] $Image = "dist32\leonos32.img",
    [ValidateSet("Native1080p", "Small720p", "ScaledMaximize")]
    [string] $Mode = "Small720p",
    [switch] $FullScreen
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")

$Resolution = switch ($Mode) {
    "Native1080p" { "1080p" }
    "Small720p" { "720p" }
    "ScaledMaximize" { "1080p" }
    default { throw "Unknown Mode '$Mode'." }
}

& (Join-Path $PSScriptRoot "build32-image.ps1") -Resolution $Resolution | Write-Host

$Qemu = Get-LeonOsQemu
$ImagePath = Get-LeonOsImagePath $Image

$DisplayArg = switch ($Mode) {
    "Native1080p" { "gtk,zoom-to-fit=off,show-menubar=off,grab-on-hover=on,show-cursor=off" }
    "Small720p" { "gtk,zoom-to-fit=on,show-menubar=off,grab-on-hover=on,show-cursor=off" }
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
    "-name", "LeonOS-32Bit-$Mode",
    "-machine", "pc",
    "-cpu", "qemu32",
    "-m", "32M",
    "-drive", "file=$ImagePath,format=raw,if=floppy",
    "-boot", "a",
    "-vga", "std",
    "-display", $DisplayArg
) + @(
    "-serial", "stdio",
    "-monitor", "none",
    "-no-reboot"
)

if ($FullScreen) {
    $Arguments += "-full-screen"
}

Write-Host "LeonOS 32-bit runner: Mode=$Mode guest preference=$Resolution ($($Size.W)x$($Size.H))."
if ($Mode -eq "ScaledMaximize") {
    Write-Host "Host GTK zoom-to-fit scales the guest framebuffer; resizing the QEMU window does NOT change guest VBE mode."
} elseif ($Mode -eq "Small720p") {
    Write-Host "Guest VBE prefers 1280x720 with GTK zoom-to-fit (default friendly mode)."
    Write-Host "Live keys: S/F1 start menu, T/F2 tab, M min, X max, C close (Alt+M/X/C also work)."
} else {
    Write-Host "Guest VBE prefers 1920x1080 native 1:1 window."
}
Write-Host "Close the QEMU window to stop."
Write-Host "Mouse: hover or click inside LeonOS to grab/lock the pointer; press Ctrl+Alt+G to release it."

& $Qemu @Arguments
