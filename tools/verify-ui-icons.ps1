Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Header = Join-Path $Root "src32\include\ui_icons.h"
$Refs = @(
    "assets\ui\leonos-desktop-spritesheet-transparent.png",
    "assets\ui\leonos-desktop-spritesheet-transparent-source.png"
)
if (-not (Test-Path -LiteralPath $Header)) { throw "Missing $Header" }
foreach ($Rel in $Refs) {
    $Path = Join-Path $Root $Rel
    if (-not (Test-Path -LiteralPath $Path)) { throw "Missing design reference: $Path" }
}
Write-Host "UI icon header and sprite-sheet references OK."
