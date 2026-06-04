Add-Type -AssemblyName System.Drawing
$b=[System.Drawing.Bitmap]::new("assets\ui\leonos-desktop-spritesheet-transparent.png")
foreach ($r in @(@(16,16,32,32),@(80,16,32,32),@(144,16,32,32),@(16,240,48,48),@(80,240,48,48),@(272,368,48,48),@(336,368,48,48),@(464,368,48,48),@(528,368,48,48),@(592,368,48,48))) {
  $x=$r[0];$y=$r[1];$w=$r[2];$h=$r[3]
  $opaque=0
  for ($py=$y;$py -lt $y+$h;$py++) { for ($px=$x;$px -lt $x+$w;$px++) { if ($b.GetPixel($px,$py).A -gt 32) { $opaque++ } } }
  Write-Host "rect $x,$y ${w}x$h opaque=$opaque"
}
$b.Dispose()
