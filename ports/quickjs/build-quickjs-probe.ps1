param(
    [string] $OutDir = "dist32\quickjs-probe",
    [string] $Version = "2026-06-04"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
. (Join-Path $Root "tools\toolchain.ps1")

& (Join-Path $PSScriptRoot "fetch-quickjs.ps1") -Version $Version | Write-Host

$Dist = if ([System.IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $Root $OutDir }
$QuickJsSource = Join-Path $Root "ports\quickjs\vendor\quickjs-$Version"
$Src32 = Join-Path $Root "src32"
$UserRoot = Join-Path $Src32 "user"
$Include = Join-Path $Src32 "include"
$LibcRoot = Join-Path $Root "ports\netsurf\leonos_libc"
$LibcInclude = Join-Path $LibcRoot "include"
$LibcSrc = Join-Path $LibcRoot "src"

New-Item -ItemType Directory -Path $Dist -Force | Out-Null

function Patch-QuickJsSource {
    $QuickJsC = Join-Path $QuickJsSource "quickjs.c"
    $Text = [System.IO.File]::ReadAllText($QuickJsC) -replace "`r`n", "`n"

    $Text = $Text.Replace(
        "#if !defined(__EMSCRIPTEN__)`n#define CONFIG_ATOMICS`n#endif",
        "#if !defined(__EMSCRIPTEN__) && !defined(LEONOS_QUICKJS_NO_ATOMICS)`n#define CONFIG_ATOMICS`n#endif")
    $Text = $Text.Replace(
        "#if !defined(__EMSCRIPTEN__)`n/* enable stack limitation */`n#define CONFIG_STACK_CHECK`n#endif",
        "#if !defined(__EMSCRIPTEN__) && !defined(LEONOS_QUICKJS_NO_STACK_CHECK)`n/* enable stack limitation */`n#define CONFIG_STACK_CHECK`n#endif")
    $Text = $Text.Replace(
        "#elif defined(__EMSCRIPTEN__)`n    return 0;",
        "#elif defined(__EMSCRIPTEN__) || defined(LEONOS_QUICKJS_NO_MALLOC_USABLE_SIZE)`n    return 0;")

    [System.IO.File]::WriteAllText($QuickJsC, $Text, [System.Text.Encoding]::ASCII)
}

Patch-QuickJsSource

$Toolchain = Get-LeonOsI386Toolchain
$Nasm = Get-LeonOsNasmPath

$CFlags = @(
    "--target=i686-elf",
    "-std=c11",
    "-Os",
    "-ffreestanding",
    "-fno-builtin",
    "-fno-stack-protector",
    "-fno-jump-tables",
    "-fno-pic",
    "-fno-pie",
    "-ffunction-sections",
    "-fdata-sections",
    "-mno-sse",
    "-mno-mmx",
    "-m32",
    "-nostdinc",
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
    "-Wno-sign-compare",
    "-Wno-missing-field-initializers",
    "-Wno-unused-variable",
    "-Wno-unused-but-set-variable",
    "-Wno-sometimes-uninitialized",
    "-Wno-return-type",
    "-DLEONOS_USER_APP",
    "-DLEONOS_QUICKJS_NO_ATOMICS",
    "-DLEONOS_QUICKJS_NO_STACK_CHECK",
    "-DLEONOS_QUICKJS_NO_MALLOC_USABLE_SIZE",
    "-Dalloca=__builtin_alloca",
    "-D_GNU_SOURCE",
    "-DCONFIG_VERSION=\`"$Version\`"",
    "-I$Include",
    "-I$LibcInclude",
    "-I$QuickJsSource"
)

if ($Toolchain.Kind -ne "clang-lld") {
    $CFlags = $CFlags | Where-Object { $_ -ne "--target=i686-elf" }
}

$Objects = [System.Collections.Generic.List[string]]::new()

function Add-AsmObject {
    param([string] $Source, [string] $Name)
    $Obj = Join-Path $Dist $Name
    & $script:Nasm -f elf32 $Source -o $Obj
    if ($LASTEXITCODE -ne 0) {
        throw "NASM failed while assembling $Source"
    }
    [void] $script:Objects.Add($Obj)
}

function Add-CObject {
    param([string] $Source, [string] $Name, [string[]] $ExtraFlags = @())
    $Obj = Join-Path $Dist $Name
    & $script:Toolchain.Cc @script:CFlags @ExtraFlags -c $Source -o $Obj
    if ($LASTEXITCODE -ne 0) {
        throw "C compiler failed while building $Source"
    }
    [void] $script:Objects.Add($Obj)
}

Add-AsmObject (Join-Path $UserRoot "crt0.asm") "crt0.o"
Add-AsmObject (Join-Path $LibcSrc "setjmp_i386.asm") "setjmp_i386.o"
Add-CObject (Join-Path $LibcSrc "leostd.c") "leostd.o"
Add-CObject (Join-Path $QuickJsSource "cutils.c") "cutils.o"
Add-CObject (Join-Path $QuickJsSource "libunicode.c") "libunicode.o"
Add-CObject (Join-Path $QuickJsSource "libregexp.c") "libregexp.o"
Add-CObject (Join-Path $QuickJsSource "dtoa.c") "dtoa.o"
Add-CObject (Join-Path $QuickJsSource "quickjs.c") "quickjs.o" @("-Wno-implicit-fallthrough")
Add-CObject (Join-Path $UserRoot "uquickjs.c") "uquickjs.o"

$Elf = Join-Path $Dist "UQJS.elf"
$Leo = Join-Path $Dist "UQJS.LEO"

if ($Toolchain.Kind -eq "clang-lld") {
    & $Toolchain.Ld `
        "-m" "elf_i386" `
        "-T" (Join-Path $UserRoot "leonos_user.ld") `
        "-nostdlib" `
        "--gc-sections" `
        "-Map" (Join-Path $Dist "UQJS.link.map") `
        "-o" $Elf `
        @Objects
    if ($LASTEXITCODE -ne 0) {
        throw "ld.lld failed while linking UQJS.elf"
    }
} elseif ($Toolchain.Kind -eq "i686-elf-gcc") {
    & $Toolchain.Ld `
        "-T" (Join-Path $UserRoot "leonos_user.ld") `
        "-nostdlib" `
        "--gc-sections" `
        "-Map" (Join-Path $Dist "UQJS.link.map") `
        "-o" $Elf `
        @Objects
    if ($LASTEXITCODE -ne 0) {
        throw "i686-elf-ld failed while linking UQJS.elf"
    }
} else {
    throw "Unsupported toolchain kind '$($Toolchain.Kind)' while building UQJS.LEO."
}

& $Toolchain.Objcopy "-O" "binary" $Elf $Leo
if ($LASTEXITCODE -ne 0) {
    throw "objcopy failed while producing UQJS.LEO"
}

$Bytes = [System.IO.File]::ReadAllBytes($Leo)
if ($Bytes.Length -lt 32 -or
    $Bytes[0] -ne 0x4C -or $Bytes[1] -ne 0x45 -or
    $Bytes[2] -ne 0x4F -or $Bytes[3] -ne 0x31) {
    throw "UQJS.LEO is missing the LEO1 header."
}
if ($Bytes[4] -ne 0x02) {
    throw "UQJS.LEO must declare LEO1 ABI version 2."
}
if ($Bytes.Length -gt 4MB) {
    throw "UQJS.LEO exceeds the current 4 MiB user-app loader limit."
}

Write-Host "LeonOS QuickJS probe: linked $Leo ($($Bytes.Length) bytes)"
