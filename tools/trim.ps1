Add-Type -AssemblyName System.Drawing
$b=[System.Drawing.Bitmap]::new("assets\ui\leonos-desktop-spritesheet-transparent.png")
function TrimRect($x,$y,$w,$h) {
  $minx=$x+$w; $maxx=$x; $miny=$y+$h; $maxy=$y
  for ($py=$y;$py -lt $y+$h;$py++) {
    for ($px=$x;$px -lt $x+$w;$px++) {
      if ($b.GetPixel($px,$py).A -gt 24) {
        if ($px -lt $minx) { $minx=$px }
        if ($px -gt $maxx) { $maxx=$px }
        if ($py -lt $miny) { $miny=$py }
        if ($py -gt $maxy) { $maxy=$py }
      }
    }
  }
  if ($maxx -lt $minx) { return $null }
  return @{ X=$minx; Y=$miny; W=$maxx-$minx+1; H=$maxy-$miny+1 }
}
$regions = @{
  chrome_min = @(0,0,96,96)
  chrome_max = @(64,0,96,96)
  chrome_close = @(128,0,96,96)
  start = @(64,224,96,96)
  folder = @(224,352,96,96)
  info = @(416,352,96,96)
  apps = @(512,352,96,96)
  log = @(608,352,96,96)
}
foreach ($k in $regions.Keys) {
  $r=$regions[$k]
  $t=TrimRect $r[0] $r[1] $r[2] $r[3]
  Write-Host "$k -> $($t.X),$($t.Y) $($t.W)x$($t.H)"
}
$b.Dispose()
