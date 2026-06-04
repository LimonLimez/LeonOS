param(
    [string] $OutDir = "dist32",
    [ValidateSet("1080p", "720p", "768p", "1366")]
    [string] $Resolution = "1080p"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "toolchain.ps1")

$Root = Split-Path -Parent $PSScriptRoot
$Dist = if ([System.IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $Root $OutDir }
$Src = Join-Path $Root "src"
$FsRoot = Join-Path $Root "fs"

New-Item -ItemType Directory -Path $Dist -Force | Out-Null

& (Join-Path $PSScriptRoot "build32.ps1") -OutDir $Dist | Write-Host

$Nasm = Get-LeonOsNasmPath
$Kernel32Bin = Join-Path $Dist "kernel32.bin"
$Stage2Bin = Join-Path $Dist "stage2-kernel.sys"
$BootBin = Join-Path $Dist "boot32.bin"
$ImagePath = Join-Path $Dist "leonos32.img"

$Kernel32ForNasm = (($Kernel32Bin.Substring($Root.Length + 1)) -replace "\\", "/")

switch ($Resolution) {
    "1080p" { $PrefW = 1920; $PrefH = 1080; $Pref720 = $false }
    "720p" { $PrefW = 1280; $PrefH = 720; $Pref720 = $true }
    "768p" { $PrefW = 1024; $PrefH = 768; $Pref720 = $false }
    "1366" { $PrefW = 1366; $PrefH = 768; $Pref720 = $false }
    default { throw "Unknown Resolution '$Resolution'." }
}

$Stage2Defines = @(
    "-DHAS_KERNEL32=1",
    "-DKERNEL32_BIN='$Kernel32ForNasm'",
    "-DVBE_PREF_W=$PrefW",
    "-DVBE_PREF_H=$PrefH"
)
if ($Pref720) {
    $Stage2Defines += "-DVBE_PREF_720"
}

& $Nasm `
    -f bin `
    @Stage2Defines `
    (Join-Path $Src "stage2.asm") `
    -o $Stage2Bin
if ($LASTEXITCODE -ne 0) {
    throw "NASM failed while building stage2 with embedded kernel32 payload."
}

$Stage2Bytes = [System.IO.File]::ReadAllBytes($Stage2Bin)
$Stage2Sectors = [int] [Math]::Ceiling($Stage2Bytes.Length / 512.0)
$MaxStage2Sectors = 512
if ($Stage2Sectors -gt $MaxStage2Sectors) {
    throw "Experimental stage2 KERNEL.SYS is too large for the current simple boot loader ($Stage2Sectors sectors; max $MaxStage2Sectors)."
}

& $Nasm -f bin "-DKERNEL_SECTORS=$Stage2Sectors" (Join-Path $Src "boot.asm") -o $BootBin
if ($LASTEXITCODE -ne 0) {
    throw "NASM failed while building boot.asm for the 32-bit image."
}

$BootBytes = [System.IO.File]::ReadAllBytes($BootBin)
if ($BootBytes.Length -ne 512) {
    throw "Boot sector must be exactly 512 bytes; got $($BootBytes.Length)."
}
if ($BootBytes[510] -ne 0x55 -or $BootBytes[511] -ne 0xAA) {
    throw "Boot sector is missing the 0xAA55 signature."
}

function Set-UInt16Le {
    param([byte[]] $Bytes, [int] $Offset, [int] $Value)
    $Bytes[$Offset] = [byte] ($Value -band 0xFF)
    $Bytes[$Offset + 1] = [byte] (($Value -shr 8) -band 0xFF)
}

function Set-UInt32Le {
    param([byte[]] $Bytes, [int] $Offset, [int64] $Value)
    $Bytes[$Offset] = [byte] ($Value -band 0xFF)
    $Bytes[$Offset + 1] = [byte] (($Value -shr 8) -band 0xFF)
    $Bytes[$Offset + 2] = [byte] (($Value -shr 16) -band 0xFF)
    $Bytes[$Offset + 3] = [byte] (($Value -shr 24) -band 0xFF)
}

function Set-Fat12Entry {
    param([byte[]] $Fat, [int] $Cluster, [int] $Value)
    $Value = $Value -band 0xFFF
    $Offset = [int] ($Cluster + [Math]::Floor($Cluster / 2))
    if (($Cluster -band 1) -eq 0) {
        $Fat[$Offset] = [byte] ($Value -band 0xFF)
        $Fat[$Offset + 1] = [byte] (($Fat[$Offset + 1] -band 0xF0) -bor (($Value -shr 8) -band 0x0F))
    } else {
        $Fat[$Offset] = [byte] (($Fat[$Offset] -band 0x0F) -bor (($Value -shl 4) -band 0xF0))
        $Fat[$Offset + 1] = [byte] (($Value -shr 4) -band 0xFF)
    }
}

function ConvertTo-FatName {
    param([string] $Name)
    $Upper = $Name.ToUpperInvariant()
    $Parts = $Upper.Split(".")
    if ($Parts.Count -gt 2 -or $Parts[0].Length -lt 1 -or $Parts[0].Length -gt 8) {
        throw "FAT12 filename '$Name' must be 8.3."
    }
    if ($Parts.Count -eq 2 -and $Parts[1].Length -gt 3) {
        throw "FAT12 extension '$Name' must be 8.3."
    }
    $Base = $Parts[0].PadRight(8, " ")
    $Ext = if ($Parts.Count -eq 2) { $Parts[1].PadRight(3, " ") } else { "   " }
    return [System.Text.Encoding]::ASCII.GetBytes($Base + $Ext)
}

function Add-RootEntry {
    param([byte[]] $RootDirectory, [int] $EntryIndex, [string] $Name, [int] $Attributes, [int] $FirstCluster, [int64] $Size)
    $Offset = $EntryIndex * 32
    $FatName = ConvertTo-FatName $Name
    [Array]::Copy($FatName, 0, $RootDirectory, $Offset, 11)
    $RootDirectory[$Offset + 11] = [byte] $Attributes
    $FatTime = (12 -shl 11)
    $FatDate = ((2026 - 1980) -shl 9) -bor (5 -shl 5) -bor 29
    Set-UInt16Le $RootDirectory ($Offset + 14) $FatTime
    Set-UInt16Le $RootDirectory ($Offset + 16) $FatDate
    Set-UInt16Le $RootDirectory ($Offset + 22) $FatTime
    Set-UInt16Le $RootDirectory ($Offset + 24) $FatDate
    Set-UInt16Le $RootDirectory ($Offset + 26) $FirstCluster
    Set-UInt32Le $RootDirectory ($Offset + 28) $Size
}

$BytesPerSector = 512
$SectorsPerFat = 9
$RootDirSectors = 14
$FirstFatSector = 1
$FirstRootSector = 19
$FirstDataSector = 33
$TotalSectors = 2880
$ImageSize = $TotalSectors * $BytesPerSector

$Image = New-Object byte[] $ImageSize
[Array]::Copy($BootBytes, 0, $Image, 0, 512)

$Fat = New-Object byte[] ($SectorsPerFat * $BytesPerSector)
$Fat[0] = 0xF0
$Fat[1] = 0xFF
$Fat[2] = 0xFF

$RootDirectory = New-Object byte[] ($RootDirSectors * $BytesPerSector)
$NextCluster = 2
$RootIndex = 0
$Records = [System.Collections.Generic.List[object]]::new()
$FatNames = [System.Collections.Generic.HashSet[string]]::new()

function Add-FileToImage {
    param([string] $Name, [byte[]] $Content, [int] $Attributes)
    $FatNameText = [System.Text.Encoding]::ASCII.GetString((ConvertTo-FatName $Name))
    if (-not $script:FatNames.Add($FatNameText)) {
        throw "Duplicate FAT12 8.3 filename '$Name'."
    }
    $ClusterCount = [Math]::Max(1, [int] [Math]::Ceiling($Content.Length / 512.0))
    $FirstCluster = $script:NextCluster
    $LastCluster = $FirstCluster + $ClusterCount - 1
    $LastDataSector = $script:FirstDataSector + ($LastCluster - 2)
    if ($LastDataSector -ge $script:TotalSectors) {
        throw "File '$Name' would exceed the 1.44 MB FAT12 image."
    }
    for ($Index = 0; $Index -lt $ClusterCount; $Index++) {
        $Cluster = $script:NextCluster + $Index
        $NextValue = if ($Index -eq ($ClusterCount - 1)) { 0xFFF } else { $Cluster + 1 }
        Set-Fat12Entry $script:Fat $Cluster $NextValue
        $DataSector = $script:FirstDataSector + ($Cluster - 2)
        $ImageOffset = $DataSector * 512
        $ContentOffset = $Index * 512
        $BytesToCopy = [Math]::Min(512, [Math]::Max(0, $Content.Length - $ContentOffset))
        if ($BytesToCopy -gt 0) {
            [Array]::Copy($Content, $ContentOffset, $script:Image, $ImageOffset, $BytesToCopy)
        }
    }
    Add-RootEntry $script:RootDirectory $script:RootIndex $Name $Attributes $FirstCluster $Content.Length
    [void] $script:Records.Add([pscustomobject] @{
        Name = $Name
        FirstCluster = $FirstCluster
        Clusters = $ClusterCount
        Size = $Content.Length
        Attributes = $Attributes
    })
    $script:RootIndex += 1
    $script:NextCluster += $ClusterCount
}

Add-FileToImage "KERNEL.SYS" $Stage2Bytes 0x06

$FsFiles = Get-ChildItem -LiteralPath $FsRoot -File | Sort-Object Name
foreach ($File in $FsFiles) {
    Add-FileToImage $File.Name ([System.IO.File]::ReadAllBytes($File.FullName)) 0x20
}

for ($Copy = 0; $Copy -lt 2; $Copy++) {
    $Offset = ($FirstFatSector + ($Copy * $SectorsPerFat)) * $BytesPerSector
    [Array]::Copy($Fat, 0, $Image, $Offset, $Fat.Length)
}

[Array]::Copy($RootDirectory, 0, $Image, $FirstRootSector * $BytesPerSector, $RootDirectory.Length)
[System.IO.File]::WriteAllBytes($ImagePath, $Image)

$MapLines = [System.Collections.Generic.List[string]]::new()
[void] $MapLines.Add("LeonOS experimental 32-bit FAT12 floppy image")
[void] $MapLines.Add("image_bytes = $ImageSize")
[void] $MapLines.Add("stage2_kernel_sys_bytes = $($Stage2Bytes.Length)")
[void] $MapLines.Add("stage2_kernel_sys_sectors = $Stage2Sectors")
[void] $MapLines.Add("kernel32_bin = $Kernel32Bin")
[void] $MapLines.Add("")
foreach ($Record in $Records) {
    [void] $MapLines.Add(("{0,-12} attr=0x{1:X2} cluster={2} clusters={3} size={4}" -f $Record.Name, $Record.Attributes, $Record.FirstCluster, $Record.Clusters, $Record.Size))
}
[System.IO.File]::WriteAllLines((Join-Path $Dist "leonos32.map"), $MapLines)

Write-Host ("Built {0} ({1} bytes)." -f $ImagePath, $ImageSize)
Write-Host ("Stage2 KERNEL.SYS: {0} bytes / {1} sectors." -f $Stage2Bytes.Length, $Stage2Sectors)
Write-Host ("Stage2 VBE preference: {0}x{1} (Resolution={2})." -f $PrefW, $PrefH, $Resolution)
