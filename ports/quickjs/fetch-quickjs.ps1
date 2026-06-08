param(
    [string] $Version = "2026-06-04"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Vendor = Join-Path $Root "ports\quickjs\vendor"
$Archive = Join-Path $Vendor "quickjs-$Version.tar.xz"
$Source = Join-Path $Vendor "quickjs-$Version"
$Url = "https://bellard.org/quickjs/quickjs-$Version.tar.xz"

New-Item -ItemType Directory -Path $Vendor -Force | Out-Null

if (-not (Test-Path -LiteralPath $Archive)) {
    Write-Host "Downloading QuickJS $Version..."
    & curl.exe -L $Url -o $Archive
    if ($LASTEXITCODE -ne 0) {
        throw "curl failed while downloading $Url"
    }
}

if (-not (Test-Path -LiteralPath $Source)) {
    Write-Host "Extracting QuickJS $Version..."
    & tar.exe -xf $Archive -C $Vendor
    if ($LASTEXITCODE -ne 0) {
        throw "tar failed while extracting $Archive"
    }
}

Write-Host "QuickJS source ready at $Source"
