param(
    [string] $OutDir = "dist32"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "toolchain.ps1")

$Root = Split-Path -Parent $PSScriptRoot
$Dist = if ([System.IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $Root $OutDir }
$Src = Join-Path $Root "src"

New-Item -ItemType Directory -Path $Dist -Force | Out-Null

$Nasm = Get-LeonOsNasmPath
$Stage2Bin = Join-Path $Dist "stage2.bin"

& $Nasm -f bin (Join-Path $Src "stage2.asm") -o $Stage2Bin
if ($LASTEXITCODE -ne 0) {
    throw "NASM failed while building src\stage2.asm."
}

$Bytes = [System.IO.File]::ReadAllBytes($Stage2Bin)
$Map = @(
    "LeonOS stage2 preview build",
    "stage2_bin = $Stage2Bin",
    "stage2_bytes = $($Bytes.Length)",
    "load_address = 0x00008000",
    "bootinfo_address = 0x00009000",
    "kernel32_load_address = 0x00100000",
    "integrated_with_boot_image = false"
)
[System.IO.File]::WriteAllLines((Join-Path $Dist "stage2.map"), $Map)

Write-Host "Built $Stage2Bin ($($Bytes.Length) bytes)."
Write-Host "Stage2 is a preview artifact and is not wired into dist\leonos.img yet."
