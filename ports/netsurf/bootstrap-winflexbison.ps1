param(
    [string] $Version = "2.5.25"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$ToolsDir = Join-Path $Root ".tools"
$OutDir = Join-Path $ToolsDir "winflexbison"
$ZipName = "win_flex_bison-$Version.zip"
$ZipPath = Join-Path $ToolsDir $ZipName
$Url = "https://github.com/lexxmark/winflexbison/releases/download/v$Version/$ZipName"

New-Item -ItemType Directory -Path $ToolsDir -Force | Out-Null

$Flex = Join-Path $OutDir "win_flex.exe"
$Bison = Join-Path $OutDir "win_bison.exe"
if ((Test-Path -LiteralPath $Flex) -and (Test-Path -LiteralPath $Bison)) {
    Write-Host "win_flex_bison $Version already bootstrapped at $OutDir"
    exit 0
}

if (-not (Test-Path -LiteralPath $ZipPath)) {
    Write-Host "Downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile $ZipPath
}

if (Test-Path -LiteralPath $OutDir) {
    Remove-Item -LiteralPath $OutDir -Recurse -Force
}
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
Expand-Archive -LiteralPath $ZipPath -DestinationPath $OutDir -Force

if (-not (Test-Path -LiteralPath $Flex) -or
    -not (Test-Path -LiteralPath $Bison)) {
    throw "win_flex.exe or win_bison.exe was not found after extracting $ZipPath."
}

Write-Host "Bootstrapped win_flex_bison $Version to $OutDir"
