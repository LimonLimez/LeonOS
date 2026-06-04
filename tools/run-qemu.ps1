param(
    [string] $Image = "dist\leonos.img",
    [switch] $FullScreen
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")

& (Join-Path $PSScriptRoot "build.ps1") | Write-Host

$Qemu = Get-LeonOsQemu
$ImagePath = Get-LeonOsImagePath $Image

$Arguments = @(
    "-name", "LeonOS-GUI",
    "-machine", "pc",
    "-cpu", "qemu32",
    "-m", "16M",
    "-drive", "file=$ImagePath,format=raw,if=floppy",
    "-boot", "a",
    "-vga", "std",
    "-display", "gtk,zoom-to-fit=on,show-menubar=off",
    "-serial", "stdio",
    "-monitor", "none",
    "-no-reboot"
)

if ($FullScreen) {
    $Arguments += "-full-screen"
}

Write-Host "Starting QEMU. Close the QEMU window to stop LeonOS."
& $Qemu @Arguments
