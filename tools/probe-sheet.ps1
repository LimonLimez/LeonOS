Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Drawing
$Path = Join-Path (Split-Path $PSScriptRoot -Parent) "assets\ui\leonos-desktop-spritesheet-transparent.png"
$Bmp = [System.Drawing.Bitmap]::new($Path)
Write-Host "Size $($Bmp.Width)x$($Bmp.Height)"
function Sample($x, $y) {
    $c = $Bmp.GetPixel($x, $y)
    return "$x,$y RGB($($c.R),$($c.G),$($c.B)) A=$($c.A)"
}
foreach ($pt in @(@(24,24),@(56,24),@(88,24),@(24,88),@(24,152),@(88,152),@(152,152),@(24,216),@(88,280),@(152,344))) {
    Write-Host (Sample $pt[0] $pt[1])
}
$Bmp.Dispose()
