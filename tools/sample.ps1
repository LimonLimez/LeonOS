Add-Type -AssemblyName System.Drawing
$b=[System.Drawing.Bitmap]::new("assets\ui\leonos-desktop-spritesheet-transparent.png")
function S($x,$y){$c=$b.GetPixel($x,$y);"($x,$y) A=$($c.A) R=$($c.R) G=$($c.G) B=$($c.B)"}
@(S 48 48; S 112 48; S 176 48; S 48 272; S 112 272; S 176 272; S 240 272; S 304 272; S 368 272; S 48 336; S 176 400; S 240 400; S 304 400) | ForEach-Object { Write-Host $_ }
$b.Dispose()
