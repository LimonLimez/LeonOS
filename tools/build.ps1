param(
    [string] $OutDir = "dist"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$Dist = if ([System.IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $Root $OutDir }
$Src = Join-Path $Root "src"
$FsRoot = Join-Path $Root "fs"

New-Item -ItemType Directory -Path $Dist -Force | Out-Null

function Get-LeonOsNasm {
    foreach ($Name in @("nasm.exe", "nasm")) {
        $Command = Get-Command $Name -ErrorAction SilentlyContinue
        if ($Command) {
            return $Command.Source
        }
    }

    $Candidates = @(
        "C:\Program Files\NASM\nasm.exe",
        "C:\Program Files (x86)\NASM\nasm.exe"
    )
    if ($env:LOCALAPPDATA) {
        $Candidates = @(Join-Path $env:LOCALAPPDATA "bin\NASM\nasm.exe") + $Candidates
    }

    foreach ($Candidate in $Candidates) {
        if (Test-Path -LiteralPath $Candidate) {
            return $Candidate
        }
    }

    throw @"
nasm.exe was not found.

Install NASM, then retry:
  winget install --id NASM.NASM --exact --accept-source-agreements --accept-package-agreements
"@
}

function Set-UInt16Le {
    param(
        [byte[]] $Bytes,
        [int] $Offset,
        [int] $Value
    )

    $Bytes[$Offset] = [byte] ($Value -band 0xFF)
    $Bytes[$Offset + 1] = [byte] (($Value -shr 8) -band 0xFF)
}

function Set-UInt32Le {
    param(
        [byte[]] $Bytes,
        [int] $Offset,
        [int64] $Value
    )

    $Bytes[$Offset] = [byte] ($Value -band 0xFF)
    $Bytes[$Offset + 1] = [byte] (($Value -shr 8) -band 0xFF)
    $Bytes[$Offset + 2] = [byte] (($Value -shr 16) -band 0xFF)
    $Bytes[$Offset + 3] = [byte] (($Value -shr 24) -band 0xFF)
}

function Set-Fat12Entry {
    param(
        [byte[]] $Fat,
        [int] $Cluster,
        [int] $Value
    )

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
    param(
        [byte[]] $RootDirectory,
        [int] $EntryIndex,
        [string] $Name,
        [int] $Attributes,
        [int] $FirstCluster,
        [int64] $Size
    )

    $Offset = $EntryIndex * 32
    $FatName = ConvertTo-FatName $Name
    [Array]::Copy($FatName, 0, $RootDirectory, $Offset, 11)
    $RootDirectory[$Offset + 11] = [byte] $Attributes

    # Stable timestamp: 2026-05-29 12:00:00.
    $FatTime = (12 -shl 11)
    $FatDate = ((2026 - 1980) -shl 9) -bor (5 -shl 5) -bor 29
    Set-UInt16Le $RootDirectory ($Offset + 14) $FatTime
    Set-UInt16Le $RootDirectory ($Offset + 16) $FatDate
    Set-UInt16Le $RootDirectory ($Offset + 22) $FatTime
    Set-UInt16Le $RootDirectory ($Offset + 24) $FatDate
    Set-UInt16Le $RootDirectory ($Offset + 26) $FirstCluster
    Set-UInt32Le $RootDirectory ($Offset + 28) $Size
}

$Nasm = Get-LeonOsNasm
$KernelBin = Join-Path $Dist "kernel.bin"
$BootBin = Join-Path $Dist "boot.bin"
$ImagePath = Join-Path $Dist "leonos.img"

& $Nasm -f bin (Join-Path $Src "kernel.asm") -o $KernelBin
if ($LASTEXITCODE -ne 0) {
    throw "NASM failed while building kernel.asm."
}

$KernelBytes = [System.IO.File]::ReadAllBytes($KernelBin)
$KernelSectors = [int] [Math]::Ceiling($KernelBytes.Length / 512.0)
if ($KernelSectors -lt 1) {
    throw "Kernel image is empty."
}

& $Nasm -f bin "-DKERNEL_SECTORS=$KernelSectors" (Join-Path $Src "boot.asm") -o $BootBin
if ($LASTEXITCODE -ne 0) {
    throw "NASM failed while building boot.asm."
}

$BootBytes = [System.IO.File]::ReadAllBytes($BootBin)
if ($BootBytes.Length -ne 512) {
    throw "Boot sector must be exactly 512 bytes; got $($BootBytes.Length)."
}
if ($BootBytes[510] -ne 0x55 -or $BootBytes[511] -ne 0xAA) {
    throw "Boot sector is missing the 0xAA55 signature."
}

$BytesPerSector = 512
$SectorsPerCluster = 1
$ReservedSectors = 1
$FatCount = 2
$RootEntries = 224
$TotalSectors = 2880
$SectorsPerFat = 9
$RootDirSectors = 14
$FirstFatSector = 1
$FirstRootSector = 19
$FirstDataSector = 33
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
$FileRecords = [System.Collections.Generic.List[object]]::new()
$FatNames = [System.Collections.Generic.HashSet[string]]::new()

function Add-FileToImage {
    param(
        [string] $Name,
        [byte[]] $Content,
        [int] $Attributes
    )

    $FatNameText = [System.Text.Encoding]::ASCII.GetString((ConvertTo-FatName $Name))
    if (-not $script:FatNames.Add($FatNameText)) {
        throw "Duplicate FAT12 8.3 filename '$Name'."
    }
    if ($script:RootIndex -ge $RootEntries) {
        throw "Too many root directory entries; FAT12 image supports $RootEntries entries."
    }

    $ClusterCount = [Math]::Max(1, [int] [Math]::Ceiling($Content.Length / 512.0))
    $FirstCluster = $script:NextCluster
    $LastCluster = $FirstCluster + $ClusterCount - 1
    $LastDataSector = $script:FirstDataSector + ($LastCluster - 2)
    if ($LastDataSector -ge $TotalSectors) {
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
    [void] $script:FileRecords.Add([pscustomobject] @{
        Name = $Name
        FirstCluster = $FirstCluster
        Clusters = $ClusterCount
        Size = $Content.Length
        Attributes = $Attributes
    })
    $script:RootIndex += 1
    $script:NextCluster += $ClusterCount
}

Add-FileToImage "KERNEL.SYS" $KernelBytes 0x06

$KernelRecord = $FileRecords[0]
if ($KernelRecord.Name -ne "KERNEL.SYS" -or $KernelRecord.FirstCluster -ne 2) {
    throw "Boot sector requires KERNEL.SYS to be first and start at cluster 2."
}
if ($KernelRecord.Clusters -ne $KernelSectors) {
    throw "KERNEL.SYS cluster count does not match KERNEL_SECTORS."
}

$FsFiles = Get-ChildItem -LiteralPath $FsRoot -File | Sort-Object Name
foreach ($File in $FsFiles) {
    Add-FileToImage $File.Name ([System.IO.File]::ReadAllBytes($File.FullName)) 0x20
}

for ($Copy = 0; $Copy -lt $FatCount; $Copy++) {
    $Offset = ($FirstFatSector + ($Copy * $SectorsPerFat)) * $BytesPerSector
    [Array]::Copy($Fat, 0, $Image, $Offset, $Fat.Length)
}

[Array]::Copy($RootDirectory, 0, $Image, $FirstRootSector * $BytesPerSector, $RootDirectory.Length)
[System.IO.File]::WriteAllBytes($ImagePath, $Image)

$MapLines = [System.Collections.Generic.List[string]]::new()
[void] $MapLines.Add("LeonOS FAT12 floppy image")
[void] $MapLines.Add("image_bytes = $ImageSize")
[void] $MapLines.Add("kernel_bytes = $($KernelBytes.Length)")
[void] $MapLines.Add("kernel_sectors = $KernelSectors")
[void] $MapLines.Add("first_data_sector = $FirstDataSector")
[void] $MapLines.Add("")
foreach ($Record in $FileRecords) {
    [void] $MapLines.Add(("{0,-12} attr=0x{1:X2} cluster={2} clusters={3} size={4}" -f $Record.Name, $Record.Attributes, $Record.FirstCluster, $Record.Clusters, $Record.Size))
}
[System.IO.File]::WriteAllLines((Join-Path $Dist "leonos.map"), $MapLines)

Write-Host ("Built {0} ({1} bytes)." -f $ImagePath, $ImageSize)
Write-Host ("Kernel: {0} bytes / {1} sectors." -f $KernelBytes.Length, $KernelSectors)
Write-Host ("FAT12 files: {0}" -f (($FileRecords | ForEach-Object { $_.Name }) -join ", "))
