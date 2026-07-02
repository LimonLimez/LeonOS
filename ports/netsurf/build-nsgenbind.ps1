param(
    [string] $VendorDir = "ports\netsurf\vendor",
    [string] $OutputDir = "dist32\nsgenbind-build"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$VendorPath = if ([System.IO.Path]::IsPathRooted($VendorDir)) {
    $VendorDir
} else {
    Join-Path $Root $VendorDir
}
$Nsgenbind = Join-Path $VendorPath "nsgenbind"
if (-not (Test-Path -LiteralPath $Nsgenbind)) {
    throw "nsgenbind source missing at $Nsgenbind. Run ports\netsurf\fetch-netsurf.ps1 first."
}

function Ensure-NsgenbindLeonOsPatches {
    param([string] $NsgenbindDir)

    $Utils = Join-Path $NsgenbindDir "src\utils.c"
    $UtilsText = [System.IO.File]::ReadAllText($Utils)
    $UtilsText = $UtilsText.Replace(
        "        fpathl = strlen(options->outdirname) + strlen(fname) + 3;",
        "        fpathl = strlen(options->outdirname) + strlen(fname) + 32;")
    if (-not $UtilsText.Contains("        int rename_res;")) {
        # Declare rename_res inside genb_fclose_tmp (after its last local,
        # size_t frd;), not after the file's first "FILE *filef;" which
        # belongs to a different function.
        $FrdPattern = [regex] "(        size_t frd;\r?\n)"
        $UtilsText = $FrdPattern.Replace(
            $UtilsText,
            '${1}        int rename_res;' + "`n",
            1)
    }
    $RenameBlock = @"
remove(fpath);
                rename_res = rename(tpath, fpath);
                if (rename_res != 0) {
                        fprintf(stderr, "Error: unable to rename file %s to %s (%s)\n",
                                tpath, fpath, strerror(errno));
                }
"@
    $RenamePattern = [regex] "remove\(fpath\);\s+rename\(tpath, fpath\);"
    $UtilsText = $RenamePattern.Replace($UtilsText, $RenameBlock, 2)
    [System.IO.File]::WriteAllText($Utils, $UtilsText)

    $Common = Join-Path $NsgenbindDir "src\duk-libdom-common.c"
    $CommonText = [System.IO.File]::ReadAllText($Common)
    if (-not $CommonText.Contains("duktape/binding.h")) {
        $Prologue = @"
int output_tool_prologue(struct opctx *outc)
{
        outputf(outc, "\n#include \"duktape/binding.h\"\n");
        outputf(outc, "#include \"duktape/private.h\"\n");
        outputf(outc, "#include \"duktape/prototype.h\"\n");

        return 0;
}
"@
        $ProloguePattern = [regex] "(?s)int output_tool_prologue\(struct opctx \*outc\)\s*\{.*?\n\}"
        $CommonText = $ProloguePattern.Replace($CommonText, $Prologue, 1)
        [System.IO.File]::WriteAllText($Common, $CommonText)
    }
}

Ensure-NsgenbindLeonOsPatches -NsgenbindDir $Nsgenbind

$SystemFlex = Get-Command flex -ErrorAction SilentlyContinue
$SystemBison = Get-Command bison -ErrorAction SilentlyContinue
if ($SystemFlex -and $SystemBison) {
    $Flex = $SystemFlex.Source
    $Bison = $SystemBison.Source
} else {
    & (Join-Path $PSScriptRoot "bootstrap-winflexbison.ps1") | Write-Host
    $Flex = Join-Path $Root ".tools\winflexbison\win_flex.exe"
    $Bison = Join-Path $Root ".tools\winflexbison\win_bison.exe"
}

$Build = if ([System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir
} else {
    Join-Path $Root $OutputDir
}
New-Item -ItemType Directory -Path $Build -Force | Out-Null
$Cc = Join-Path $Root ".tools\llvm-mingw\bin\clang.exe"
if (-not (Test-Path -LiteralPath $Cc)) {
    $Command = Get-Command clang -ErrorAction SilentlyContinue
    if (-not $Command) {
        throw "clang was not found. Run tools\check-toolchain.ps1."
    }
    $Cc = $Command.Source
}

& $Bison -d -t "--define=api.prefix={nsgenbind_}" --report=all `
    "--output=$(Join-Path $Build 'nsgenbind-parser.c')" `
    "--defines=$(Join-Path $Build 'nsgenbind-parser.h')" `
    (Join-Path $Nsgenbind "src\nsgenbind-parser.y")
if ($LASTEXITCODE -ne 0) { throw "Bison failed for nsgenbind-parser.y." }
& $Flex "--outfile=$(Join-Path $Build 'nsgenbind-lexer.c')" `
    "--header-file=$(Join-Path $Build 'nsgenbind-lexer.h')" `
    (Join-Path $Nsgenbind "src\nsgenbind-lexer.l")
if ($LASTEXITCODE -ne 0) { throw "Flex failed for nsgenbind-lexer.l." }
& $Bison -d -t "--define=api.prefix={webidl_}" --report=all `
    "--output=$(Join-Path $Build 'webidl-parser.c')" `
    "--defines=$(Join-Path $Build 'webidl-parser.h')" `
    (Join-Path $Nsgenbind "src\webidl-parser.y")
if ($LASTEXITCODE -ne 0) { throw "Bison failed for webidl-parser.y." }
& $Flex "--outfile=$(Join-Path $Build 'webidl-lexer.c')" `
    "--header-file=$(Join-Path $Build 'webidl-lexer.h')" `
    (Join-Path $Nsgenbind "src\webidl-lexer.l")
if ($LASTEXITCODE -ne 0) { throw "Flex failed for webidl-lexer.l." }

$Sources = @(
    "nsgenbind.c",
    "utils.c",
    "output.c",
    "webidl-ast.c",
    "nsgenbind-ast.c",
    "ir.c",
    "duk-libdom.c",
    "duk-libdom-interface.c",
    "duk-libdom-dictionary.c",
    "duk-libdom-common.c",
    "duk-libdom-generated.c"
) | ForEach-Object { Join-Path $Nsgenbind "src\$_" }
$Generated = @(
    "nsgenbind-parser.c",
    "nsgenbind-lexer.c",
    "webidl-parser.c",
    "webidl-lexer.c"
) | ForEach-Object { Join-Path $Build $_ }

$Output = Join-Path $Build "nsgenbind.exe"
& $Cc `
    "-D_BSD_SOURCE" "-D_DEFAULT_SOURCE" "-D_POSIX_C_SOURCE=200809L" `
    "-DYYENABLE_NLS=0" "-std=c99" "-O2" `
    "-I$Build" "-I$(Join-Path $Nsgenbind 'src')" `
    $Sources $Generated "-o" $Output
if ($LASTEXITCODE -ne 0) {
    throw "clang failed while building nsgenbind.exe."
}

Write-Host "Built $Output"
