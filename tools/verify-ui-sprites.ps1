param()
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Png = Join-Path $Root "assets\ui\leonos-desktop-spritesheet-transparent.png"
$Manifest = Join-Path $Root "assets\ui\spritesheet-manifest.json"
$Header = Join-Path $Root "src32\include\ui_sprites.h"
$Probes = Join-Path $Root "assets\ui\sprite-visual-probes.json"
$Gen = Join-Path $PSScriptRoot "generate-ui-sprites.ps1"

foreach ($p in @($Png, $Manifest, $Gen)) {
    if (-not (Test-Path -LiteralPath $p)) { throw "Missing required file: $p" }
}
if (-not (Test-Path -LiteralPath $Header)) { throw "Missing generated header: $Header (run tools/generate-ui-sprites.ps1 or build32.ps1)." }

$Doc = Get-Content -Raw -LiteralPath $Manifest | ConvertFrom-Json
$Names = @($Doc.sprites | ForEach-Object { $_.name })
$Required = @("chrome_min","chrome_max","chrome_close","start","folder","info","apps","log")
foreach ($n in $Required) {
    if ($Names -notcontains $n) { throw "Manifest missing sprite '$n'." }
    $macro = "UI_SPRITE_$($n.ToUpper())"
    $text = Get-Content -Raw -LiteralPath $Header
    if ($text -notmatch [regex]::Escape($macro)) { throw "Header missing enum $macro." }
    if ($text -notmatch "ui_sprite_${n}_pixels") { throw "Header missing pixel array ui_sprite_${n}_pixels." }
}
if (-not (Test-Path -LiteralPath $Probes)) { throw "Missing probe file: $Probes (regenerate sprites)." }

Add-Type -AssemblyName System.Drawing
$Bmp = [System.Drawing.Bitmap]::new($Png)
if ($Bmp.Width -lt 1000 -or $Bmp.Height -lt 1000) { throw "Unexpected sprite sheet dimensions $($Bmp.Width)x$($Bmp.Height)." }
$HasAlpha = $false
for ($y = 0; $y -lt 64 -and -not $HasAlpha; $y += 8) {
    for ($x = 0; $x -lt 64 -and -not $HasAlpha; $x += 8) {
        if ($Bmp.GetPixel($x, $y).A -lt 255) { $HasAlpha = $true }
    }
}
$Bmp.Dispose()
if (-not $HasAlpha) { throw "Sprite sheet appears to lack transparency." }

Write-Host "UI sprites OK: $($Names.Count) sprites, header and probes present."
