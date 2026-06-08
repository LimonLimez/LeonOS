param(
    [string] $RepositoryBase = "git://git.netsurf-browser.org",
    [string] $VendorDir = "ports\netsurf\vendor"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$VendorPath = if ([System.IO.Path]::IsPathRooted($VendorDir)) {
    $VendorDir
} else {
    Join-Path $Root $VendorDir
}
$Git = Get-Command git -ErrorAction SilentlyContinue
if (-not $Git) {
    $Fallback = "C:\Program Files\Git\cmd\git.exe"
    if (Test-Path -LiteralPath $Fallback) {
        $GitPath = $Fallback
    } else {
        throw "git was not found. Install Git for Windows or add git.exe to PATH."
    }
} else {
    $GitPath = $Git.Source
}

New-Item -ItemType Directory -Path $VendorPath -Force | Out-Null

$Repos = @(
    "buildsystem",
    "libwapcaplet",
    "libparserutils",
    "libhubbub",
    "libcss",
    "libdom",
    "libnsbmp",
    "libnsgif",
    "libsvgtiny",
    "libnsutils",
    "libnsfb",
    "nsgenbind",
    "netsurf"
)

foreach ($Repo in $Repos) {
    $Target = Join-Path $VendorPath $Repo
    if (Test-Path -LiteralPath $Target) {
        Write-Host "$Repo already exists at $Target"
        continue
    }

    $Url = "$RepositoryBase/$Repo.git"
    Write-Host "Cloning $Repo from $Url"
    if ($Repo -eq "libnsbmp" -or $Repo -eq "libnsgif") {
        # Some image libs have AFL fixture filenames containing ':' characters,
        # are invalid on Windows. The browser build does not need the test
        # fixtures, so check out only the source/build paths. This uses manual
        # init/fetch because clone can still trip Windows path validation before
        # sparse checkout patterns are active.
        if ($Repo -eq "libnsbmp") {
            $CheckoutPaths = @(".gitattributes", ".gitignore", "COPYING",
                               "Makefile", "examples", "include",
                               "libnsbmp.pc.in", "src")
        } else {
            $CheckoutPaths = @(".gitattributes", ".gitignore", "COPYING",
                               "Makefile", "README.md", "docs", "examples",
                               "include", "libnsgif.pc.in", "src")
        }
        New-Item -ItemType Directory -Path $Target -Force | Out-Null
        & $GitPath -C $Target init
        if ($LASTEXITCODE -ne 0) {
            throw "$Repo init failed."
        }
        & $GitPath -C $Target remote add origin $Url
        if ($LASTEXITCODE -ne 0) {
            throw "$Repo remote add failed."
        }
        & $GitPath -C $Target fetch --depth=1 origin HEAD
        if ($LASTEXITCODE -ne 0) {
            throw "$Repo fetch failed."
        }
        $ArchivePath = Join-Path ([System.IO.Path]::GetTempPath()) "$Repo-source.tar"
        if (Test-Path -LiteralPath $ArchivePath) {
            Remove-Item -LiteralPath $ArchivePath -Force
        }
        & $GitPath -C $Target -c core.protectNTFS=false archive --format=tar -o $ArchivePath FETCH_HEAD -- $CheckoutPaths
        if ($LASTEXITCODE -ne 0) {
            throw "$Repo source archive failed."
        }
        tar -xf $ArchivePath -C $Target
        if ($LASTEXITCODE -ne 0) {
            throw "$Repo source extract failed."
        }
        Remove-Item -LiteralPath $ArchivePath -Force
    } else {
        & $GitPath clone $Url $Target
        if ($LASTEXITCODE -ne 0) {
            throw "$Repo clone failed."
        }
    }
}

Write-Host "Fetched NetSurf workspace sources to $VendorPath"
