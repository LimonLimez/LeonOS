Add-Type -AssemblyName System.Drawing
$p = "assets\ui\leonos-desktop-spritesheet-transparent.png"
$b = [System.Drawing.Bitmap]::new($p)
$w=$b.Width; $h=$b.Height
# find first opaque blue pixel clusters - sample grid every 32px center
for ($gy=0; $gy -lt $h; $gy+=32) {
  $line = ""
  for ($gx=0; $gx -lt $w; $gx+=32) {
    $c = $b.GetPixel($gx+16, $gy+16)
    if ($c.A -gt 128) { $line += "#" } else { $line += "." }
  }
  Write-Host ("{0,4} {1}" -f $gy, $line)
}
$b.Dispose()
