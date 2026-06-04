param(
    [string] $Image = "dist32\leonos32.img",
    [string] $OutDir = "dist32",
    [int] $TimeoutSeconds = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

& (Join-Path $PSScriptRoot "test32-visual.ps1") `
    -Image $Image `
    -OutDir $OutDir `
    -Resolution "720p" `
    -TimeoutSeconds $TimeoutSeconds
