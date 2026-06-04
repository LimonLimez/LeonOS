param(
    [string] $OutDir = "dist32"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "toolchain.ps1")

$Root = Split-Path -Parent $PSScriptRoot
$Dist = if ([System.IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $Root $OutDir }
$Src32 = Join-Path $Root "src32"
$KernelSrc = Join-Path $Src32 "kernel"
$Include = Join-Path $Src32 "include"
$UiSpritesHeader = Join-Path $Include "ui_sprites.h"
$UiSpriteGen = Join-Path $PSScriptRoot "generate-ui-sprites.ps1"

& $UiSpriteGen -OutHeader $UiSpritesHeader
if (-not $?) {
    throw "UI sprite generation failed."
}
if (-not (Test-Path -LiteralPath $UiSpritesHeader)) {
    throw "Missing generated header: $UiSpritesHeader (run tools/generate-ui-sprites.ps1)."
}

New-Item -ItemType Directory -Path $Dist -Force | Out-Null

$Nasm = Get-LeonOsNasmPath
$Toolchain = Get-LeonOsI386Toolchain

$EntryObj = Join-Path $Dist "entry.o"
$InterruptsObj = Join-Path $Dist "interrupts.o"
$KernelObj = Join-Path $Dist "kernel.o"
$KernelElf = Join-Path $Dist "kernel32.elf"
$KernelBin = Join-Path $Dist "kernel32.bin"

& $Nasm -f elf32 (Join-Path $KernelSrc "entry.asm") -o $EntryObj
if ($LASTEXITCODE -ne 0) {
    throw "NASM failed while building src32\kernel\entry.asm."
}

& $Nasm -f elf32 (Join-Path $KernelSrc "interrupts.asm") -o $InterruptsObj
if ($LASTEXITCODE -ne 0) {
    throw "NASM failed while building src32\kernel\interrupts.asm."
}

if ($Toolchain.Kind -eq "clang-lld") {
    & $Toolchain.Cc `
        "--target=i686-elf" `
        "-std=c11" `
        "-Os" `
        "-ffreestanding" `
        "-fno-builtin" `
        "-fno-stack-protector" `
        "-fno-jump-tables" `
        "-fno-pic" `
        "-fno-pie" `
        "-mno-sse" `
        "-mno-mmx" `
        "-m32" `
        "-nostdinc" `
        "-Wall" `
        "-Wextra" `
        "-I$Include" `
        "-I$KernelSrc" `
        "-c" (Join-Path $KernelSrc "kernel.c") `
        "-o" $KernelObj
    if ($LASTEXITCODE -ne 0) {
        throw "clang failed while building src32\kernel\kernel.c."
    }

    & $Toolchain.Ld `
        "-m" "elf_i386" `
        "-T" (Join-Path $Src32 "linker.ld") `
        "-nostdlib" `
        "-Map" (Join-Path $Dist "kernel32.link.map") `
        "-o" $KernelElf `
        $EntryObj `
        $InterruptsObj `
        $KernelObj
    if ($LASTEXITCODE -ne 0) {
        throw "ld.lld failed while linking kernel32.elf."
    }
} elseif ($Toolchain.Kind -eq "i686-elf-gcc") {
    & $Toolchain.Cc `
        "-std=c11" `
        "-Os" `
        "-ffreestanding" `
        "-fno-builtin" `
        "-fno-stack-protector" `
        "-fno-jump-tables" `
        "-fno-pic" `
        "-fno-pie" `
        "-mno-sse" `
        "-mno-mmx" `
        "-m32" `
        "-nostdinc" `
        "-Wall" `
        "-Wextra" `
        "-I$Include" `
        "-I$KernelSrc" `
        "-c" (Join-Path $KernelSrc "kernel.c") `
        "-o" $KernelObj
    if ($LASTEXITCODE -ne 0) {
        throw "i686-elf-gcc failed while building src32\kernel\kernel.c."
    }

    & $Toolchain.Ld `
        "-T" (Join-Path $Src32 "linker.ld") `
        "-nostdlib" `
        "-Map" (Join-Path $Dist "kernel32.link.map") `
        "-o" $KernelElf `
        $EntryObj `
        $InterruptsObj `
        $KernelObj
    if ($LASTEXITCODE -ne 0) {
        throw "i686-elf-ld failed while linking kernel32.elf."
    }
} else {
    throw "Unsupported toolchain kind '$($Toolchain.Kind)'."
}

& $Toolchain.Objcopy "-O" "binary" $KernelElf $KernelBin
if ($LASTEXITCODE -ne 0) {
    throw "objcopy failed while producing kernel32.bin."
}

$Map = @(
    "LeonOS 32-bit kernel preview build",
    "toolchain = $($Toolchain.Kind)",
    "entry_obj = $EntryObj",
    "interrupts_obj = $InterruptsObj",
    "kernel_obj = $KernelObj",
    "kernel_elf = $KernelElf",
    "kernel_bin = $KernelBin",
    "load_address = 0x00100000"
)
[System.IO.File]::WriteAllLines((Join-Path $Dist "kernel32.map"), $Map)

Write-Host "Built $KernelElf"
Write-Host "Built $KernelBin"
