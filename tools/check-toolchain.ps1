Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "toolchain.ps1")

$Nasm = Get-LeonOsNasmPath
Write-Host "NASM: $Nasm"

$Toolchain = Get-LeonOsI386Toolchain
Write-Host "32-bit C toolchain: $($Toolchain.Kind)"
Write-Host "CC: $($Toolchain.Cc)"
Write-Host "LD: $($Toolchain.Ld)"
Write-Host "OBJCOPY: $($Toolchain.Objcopy)"
