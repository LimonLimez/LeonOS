Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$LeonOsRoot = Split-Path -Parent $PSScriptRoot
$LeonOsLocalLlvm = Join-Path $LeonOsRoot ".tools\llvm-mingw\bin"

function Find-LeonOsCommand {
    param(
        [string[]] $Names,
        [string[]] $Candidates = @()
    )

    foreach ($Name in $Names) {
        $Command = Get-Command $Name -ErrorAction SilentlyContinue
        if ($Command) {
            return $Command.Source
        }
    }

    foreach ($Candidate in $Candidates) {
        if (Test-Path -LiteralPath $Candidate) {
            return (Resolve-Path -LiteralPath $Candidate).Path
        }
    }

    return $null
}

function Get-LeonOsNasmPath {
    $Candidates = @(
        "C:\Program Files\NASM\nasm.exe",
        "C:\Program Files (x86)\NASM\nasm.exe"
    )
    if ($env:LOCALAPPDATA) {
        $Candidates = @(Join-Path $env:LOCALAPPDATA "bin\NASM\nasm.exe") + $Candidates
    }

    $Path = Find-LeonOsCommand `
        -Names @("nasm.exe", "nasm") `
        -Candidates $Candidates

    if (-not $Path) {
        throw @"
nasm.exe was not found.

Install NASM, then retry:
  winget install --id NASM.NASM --exact --accept-source-agreements --accept-package-agreements
"@
    }

    return $Path
}

function Get-LeonOsI386Toolchain {
    $Clang = Find-LeonOsCommand `
        -Names @("clang.exe", "clang") `
        -Candidates @(
            (Join-Path $LeonOsLocalLlvm "clang.exe"),
            "C:\Program Files\LLVM\bin\clang.exe",
            "C:\Program Files (x86)\LLVM\bin\clang.exe"
        )

    $LdLld = Find-LeonOsCommand `
        -Names @("ld.lld.exe", "ld.lld") `
        -Candidates @(
            (Join-Path $LeonOsLocalLlvm "ld.lld.exe"),
            "C:\Program Files\LLVM\bin\ld.lld.exe",
            "C:\Program Files (x86)\LLVM\bin\ld.lld.exe"
        )

    $LlvmObjcopy = Find-LeonOsCommand `
        -Names @("llvm-objcopy.exe", "llvm-objcopy") `
        -Candidates @(
            (Join-Path $LeonOsLocalLlvm "llvm-objcopy.exe"),
            "C:\Program Files\LLVM\bin\llvm-objcopy.exe",
            "C:\Program Files (x86)\LLVM\bin\llvm-objcopy.exe"
        )

    if ($Clang -and $LdLld -and $LlvmObjcopy) {
        return [pscustomobject] @{
            Kind = "clang-lld"
            Cc = $Clang
            Ld = $LdLld
            Objcopy = $LlvmObjcopy
        }
    }

    $Gcc = Find-LeonOsCommand -Names @("i686-elf-gcc.exe", "i686-elf-gcc")
    $Ld = Find-LeonOsCommand -Names @("i686-elf-ld.exe", "i686-elf-ld")
    $Objcopy = Find-LeonOsCommand -Names @("i686-elf-objcopy.exe", "i686-elf-objcopy")

    if ($Gcc -and $Ld -and $Objcopy) {
        return [pscustomobject] @{
            Kind = "i686-elf-gcc"
            Cc = $Gcc
            Ld = $Ld
            Objcopy = $Objcopy
        }
    }

    throw @"
A freestanding 32-bit C toolchain was not found.

Install one of these supported toolchains:

Option A - LLVM:
  winget install --id LLVM.LLVM --exact --accept-source-agreements --accept-package-agreements

Option B - i686-elf GCC:
  Install i686-elf-gcc, i686-elf-ld, and i686-elf-objcopy, then make them available on PATH.

The current 16-bit OS still builds with:
  powershell -ExecutionPolicy Bypass -File .\tools\build.ps1
"@
}
