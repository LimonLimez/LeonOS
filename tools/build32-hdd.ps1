param(
    [string] $OutDir = "dist32"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "toolchain.ps1")

# Builds a deterministic FAT32 hard-disk image at dist32\leonos32-hdd.img.
#
# Layout:
#   LBA 0                 : MBR with one primary partition (type 0x0C, FAT32 LBA).
#   LBA $PartitionLba     : FAT32 volume boot record (BPB).
#   LBA $PartitionLba + 1 : FSInfo sector.
#   LBA $PartitionLba + 6 : backup boot record.
#   reserved sectors      : two FATs followed by the data region.
#
# Root files come from the fs32\ fixture directory. The image is read-only as
# far as LeonOS is concerned; this script is the only writer.

$Root = Split-Path -Parent $PSScriptRoot
$Dist = if ([System.IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $Root $OutDir }
$FsRoot = Join-Path $Root "fs32"

New-Item -ItemType Directory -Path $Dist -Force | Out-Null

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

function ConvertTo-FatName {
    param([string] $Name)
    $Upper = $Name.ToUpperInvariant()
    $Parts = $Upper.Split(".")
    if ($Parts.Count -gt 2 -or $Parts[0].Length -lt 1 -or $Parts[0].Length -gt 8) {
        throw "FAT32 fixture filename '$Name' must be 8.3 (short name only, no LFN)."
    }
    if ($Parts.Count -eq 2 -and $Parts[1].Length -gt 3) {
        throw "FAT32 fixture extension '$Name' must be 8.3."
    }
    $Base = $Parts[0].PadRight(8, " ")
    $Ext = if ($Parts.Count -eq 2) { $Parts[1].PadRight(3, " ") } else { "   " }
    return [System.Text.Encoding]::ASCII.GetBytes($Base + $Ext)
}

# --- Geometry -------------------------------------------------------------

$BytesPerSector = 512
$SectorsPerCluster = 1
$ReservedSectors = 32
$NumFats = 2
$PartitionLba = 2048
$VolumeSectors = 80000

# Solve for a FAT size that can hold every cluster entry the data region needs.
$FatSize = 1
while ($true) {
    $DataSectors = $VolumeSectors - $ReservedSectors - ($NumFats * $FatSize)
    $ClusterCount = [int] [Math]::Floor($DataSectors / $SectorsPerCluster)
    $NeededFatBytes = ($ClusterCount + 2) * 4
    $NeededFatSectors = [int] [Math]::Ceiling($NeededFatBytes / $BytesPerSector)
    if ($NeededFatSectors -le $FatSize) { break }
    $FatSize = $NeededFatSectors
}

if ($ClusterCount -lt 65525) {
    throw "Computed cluster count $ClusterCount is below the FAT32 minimum (65525). Increase VolumeSectors."
}

$FirstFatRel = $ReservedSectors
$FirstDataRel = $ReservedSectors + ($NumFats * $FatSize)
$RootCluster = 2

$DiskSectors = $PartitionLba + $VolumeSectors
$DiskBytes = $DiskSectors * $BytesPerSector
$Image = New-Object byte[] $DiskBytes

# --- MBR ------------------------------------------------------------------

$MbrPartOffset = 446
$Image[$MbrPartOffset + 0] = 0x80              # bootable flag (informational)
$Image[$MbrPartOffset + 1] = 0xFE              # CHS first (legacy placeholder)
$Image[$MbrPartOffset + 2] = 0xFF
$Image[$MbrPartOffset + 3] = 0xFF
$Image[$MbrPartOffset + 4] = 0x0C              # type: FAT32 LBA
$Image[$MbrPartOffset + 5] = 0xFE              # CHS last (legacy placeholder)
$Image[$MbrPartOffset + 6] = 0xFF
$Image[$MbrPartOffset + 7] = 0xFF
Set-UInt32Le $Image ($MbrPartOffset + 8) $PartitionLba
Set-UInt32Le $Image ($MbrPartOffset + 12) $VolumeSectors
$Image[510] = 0x55
$Image[511] = 0xAA

# --- FAT32 volume boot record (BPB) --------------------------------------

$VbrOffset = $PartitionLba * $BytesPerSector
$Image[$VbrOffset + 0] = 0xEB                  # jmp short + nop
$Image[$VbrOffset + 1] = 0x58
$Image[$VbrOffset + 2] = 0x90
[Array]::Copy([System.Text.Encoding]::ASCII.GetBytes("LEONOS  "), 0, $Image, $VbrOffset + 3, 8)
Set-UInt16Le $Image ($VbrOffset + 0x0B) $BytesPerSector
$Image[$VbrOffset + 0x0D] = [byte] $SectorsPerCluster
Set-UInt16Le $Image ($VbrOffset + 0x0E) $ReservedSectors
$Image[$VbrOffset + 0x10] = [byte] $NumFats
Set-UInt16Le $Image ($VbrOffset + 0x11) 0      # root entry count = 0 on FAT32
Set-UInt16Le $Image ($VbrOffset + 0x13) 0      # total sectors 16 = 0 (use 32-bit field)
$Image[$VbrOffset + 0x15] = 0xF8               # media descriptor (fixed disk)
Set-UInt16Le $Image ($VbrOffset + 0x16) 0      # FAT size 16 = 0 on FAT32
Set-UInt16Le $Image ($VbrOffset + 0x18) 63     # sectors per track (placeholder)
Set-UInt16Le $Image ($VbrOffset + 0x1A) 255    # heads (placeholder)
Set-UInt32Le $Image ($VbrOffset + 0x1C) $PartitionLba   # hidden sectors
Set-UInt32Le $Image ($VbrOffset + 0x20) $VolumeSectors  # total sectors 32
Set-UInt32Le $Image ($VbrOffset + 0x24) $FatSize        # FAT size 32
Set-UInt16Le $Image ($VbrOffset + 0x28) 0      # ext flags
Set-UInt16Le $Image ($VbrOffset + 0x2A) 0      # fs version
Set-UInt32Le $Image ($VbrOffset + 0x2C) $RootCluster
Set-UInt16Le $Image ($VbrOffset + 0x30) 1      # FSInfo sector
Set-UInt16Le $Image ($VbrOffset + 0x32) 6      # backup boot sector
$Image[$VbrOffset + 0x40] = 0x80               # drive number
$Image[$VbrOffset + 0x42] = 0x29               # extended boot signature
Set-UInt32Le $Image ($VbrOffset + 0x43) 0x1E0A0532   # volume id
[Array]::Copy([System.Text.Encoding]::ASCII.GetBytes("LEONOS HDD "), 0, $Image, $VbrOffset + 0x47, 11)
[Array]::Copy([System.Text.Encoding]::ASCII.GetBytes("FAT32   "), 0, $Image, $VbrOffset + 0x52, 8)
$Image[$VbrOffset + 510] = 0x55
$Image[$VbrOffset + 511] = 0xAA

# Backup boot record at relative sector 6.
[Array]::Copy($Image, $VbrOffset, $Image, ($PartitionLba + 6) * $BytesPerSector, 512)

# --- FSInfo ---------------------------------------------------------------

$FsInfoOffset = ($PartitionLba + 1) * $BytesPerSector
Set-UInt32Le $Image ($FsInfoOffset + 0) 0x41615252      # lead signature
Set-UInt32Le $Image ($FsInfoOffset + 484) 0x61417272    # struct signature
Set-UInt32Le $Image ($FsInfoOffset + 488) 0xFFFFFFFF    # free count unknown
Set-UInt32Le $Image ($FsInfoOffset + 492) 0xFFFFFFFF    # next free unknown
Set-UInt32Le $Image ($FsInfoOffset + 508) 0xAA550000    # trail signature

# --- FAT + data -----------------------------------------------------------

$FatBytes = $FatSize * $BytesPerSector
$Fat = New-Object byte[] $FatBytes
Set-UInt32Le $Fat 0 0x0FFFFFF8                  # media descriptor in entry 0
Set-UInt32Le $Fat 4 0x0FFFFFFF                  # entry 1 / EOC marker
Set-UInt32Le $Fat 8 0x0FFFFFFF                  # entry 2 (root dir), single cluster

$NextCluster = 3
$RootEntries = New-Object byte[] $BytesPerSector
$RootIndex = 0
$Records = [System.Collections.Generic.List[object]]::new()
$FatNames = [System.Collections.Generic.HashSet[string]]::new()

function Set-Fat32Entry {
    param([byte[]] $Fat, [int] $Cluster, [int64] $Value)
    Set-UInt32Le $Fat ($Cluster * 4) ($Value -band 0x0FFFFFFF)
}

function Add-RootEntry {
    param([byte[]] $Root, [int] $EntryIndex, [string] $Name, [int] $Attributes, [int] $FirstCluster, [int64] $Size)
    $Offset = $EntryIndex * 32
    $FatName = ConvertTo-FatName $Name
    [Array]::Copy($FatName, 0, $Root, $Offset, 11)
    $Root[$Offset + 11] = [byte] $Attributes
    $FatTime = (12 -shl 11)
    $FatDate = ((2026 - 1980) -shl 9) -bor (5 -shl 5) -bor 29
    Set-UInt16Le $Root ($Offset + 14) $FatTime          # create time
    Set-UInt16Le $Root ($Offset + 16) $FatDate          # create date
    Set-UInt16Le $Root ($Offset + 18) $FatDate          # access date
    Set-UInt16Le $Root ($Offset + 20) (($FirstCluster -shr 16) -band 0xFFFF)  # cluster high
    Set-UInt16Le $Root ($Offset + 22) $FatTime          # write time
    Set-UInt16Le $Root ($Offset + 24) $FatDate          # write date
    Set-UInt16Le $Root ($Offset + 26) ($FirstCluster -band 0xFFFF)            # cluster low
    Set-UInt32Le $Root ($Offset + 28) $Size
}

function Add-FileToImage {
    param([string] $Name, [byte[]] $Content, [int] $Attributes)
    $FatNameText = [System.Text.Encoding]::ASCII.GetString((ConvertTo-FatName $Name))
    if (-not $script:FatNames.Add($FatNameText)) {
        throw "Duplicate FAT32 8.3 filename '$Name'."
    }
    if ($script:RootIndex -ge 15) {
        throw "Root directory fixture exceeds the single-cluster layout (max 15 files)."
    }

    $BytesPerCluster = $script:BytesPerSector * $script:SectorsPerCluster
    $ClusterCount = [Math]::Max(1, [int] [Math]::Ceiling($Content.Length / [double] $BytesPerCluster))
    $FirstCluster = $script:NextCluster

    for ($Index = 0; $Index -lt $ClusterCount; $Index++) {
        $Cluster = $FirstCluster + $Index
        $NextValue = if ($Index -eq ($ClusterCount - 1)) { 0x0FFFFFFF } else { $Cluster + 1 }
        Set-Fat32Entry $script:Fat $Cluster $NextValue

        $DataRelSector = $script:FirstDataRel + (($Cluster - 2) * $script:SectorsPerCluster)
        $AbsSector = $script:PartitionLba + $DataRelSector
        $ImageOffset = $AbsSector * $script:BytesPerSector
        $ContentOffset = $Index * $BytesPerCluster
        $BytesToCopy = [Math]::Min($BytesPerCluster, [Math]::Max(0, $Content.Length - $ContentOffset))
        if ($BytesToCopy -gt 0) {
            [Array]::Copy($Content, $ContentOffset, $script:Image, $ImageOffset, $BytesToCopy)
        }
    }

    Add-RootEntry $script:RootEntries $script:RootIndex $Name $Attributes $FirstCluster $Content.Length
    [void] $script:Records.Add([pscustomobject] @{
        Name = $Name
        FirstCluster = $FirstCluster
        Clusters = $ClusterCount
        Size = $Content.Length
    })
    $script:RootIndex += 1
    $script:NextCluster += $ClusterCount
}

if (-not (Test-Path -LiteralPath $FsRoot)) {
    throw "FAT32 fixture directory '$FsRoot' was not found."
}

$FsFiles = Get-ChildItem -LiteralPath $FsRoot -File | Sort-Object Name
if ($FsFiles.Count -eq 0) {
    throw "FAT32 fixture directory '$FsRoot' is empty."
}
foreach ($File in $FsFiles) {
    Add-FileToImage $File.Name ([System.IO.File]::ReadAllBytes($File.FullName)) 0x20
}

# Assemble the LEO1 flat app and store it on the FAT32 volume as HELLOAPP.LEO so
# the 32-bit kernel can really load an external program from disk.
$Nasm = Get-LeonOsNasmPath
$AppSrc = Join-Path $Root "src32\app\helloapp.asm"
$AppBin = Join-Path $Dist "HELLOAPP.LEO"
& $Nasm -f bin $AppSrc -o $AppBin
if ($LASTEXITCODE -ne 0) {
    throw "NASM failed while assembling src32\app\helloapp.asm."
}
$AppBytes = [System.IO.File]::ReadAllBytes($AppBin)
if ($AppBytes.Length -lt 32 -or
    $AppBytes[0] -ne 0x4C -or $AppBytes[1] -ne 0x45 -or
    $AppBytes[2] -ne 0x4F -or $AppBytes[3] -ne 0x31) {
    throw "HELLOAPP.LEO is missing the LEO1 header."
}
Add-FileToImage "HELLOAPP.LEO" $AppBytes 0x20

# Assemble the LEO1 v2 ring-3 user app and store it as UHELLO.LEO. This one runs
# in user mode (ring 3) and talks to the kernel only through int 0x80.
$UserSrc = Join-Path $Root "src32\app\uhello.asm"
$UserBin = Join-Path $Dist "UHELLO.LEO"
& $Nasm -f bin $UserSrc -o $UserBin
if ($LASTEXITCODE -ne 0) {
    throw "NASM failed while assembling src32\app\uhello.asm."
}
$UserBytes = [System.IO.File]::ReadAllBytes($UserBin)
if ($UserBytes.Length -lt 32 -or
    $UserBytes[0] -ne 0x4C -or $UserBytes[1] -ne 0x45 -or
    $UserBytes[2] -ne 0x4F -or $UserBytes[3] -ne 0x31) {
    throw "UHELLO.LEO is missing the LEO1 header."
}
if ($UserBytes[4] -ne 0x02) {
    throw "UHELLO.LEO must declare LEO1 ABI version 2 (ring-3 user app)."
}
Add-FileToImage "UHELLO.LEO" $UserBytes 0x20

# Write both FAT copies.
for ($Copy = 0; $Copy -lt $NumFats; $Copy++) {
    $Offset = ($PartitionLba + $FirstFatRel + ($Copy * $FatSize)) * $BytesPerSector
    [Array]::Copy($Fat, 0, $Image, $Offset, $Fat.Length)
}

# Write the root directory cluster (cluster 2).
$RootRelSector = $FirstDataRel + (($RootCluster - 2) * $SectorsPerCluster)
$RootAbsOffset = ($PartitionLba + $RootRelSector) * $BytesPerSector
[Array]::Copy($RootEntries, 0, $Image, $RootAbsOffset, $RootEntries.Length)

$ImagePath = Join-Path $Dist "leonos32-hdd.img"
[System.IO.File]::WriteAllBytes($ImagePath, $Image)

$MapLines = [System.Collections.Generic.List[string]]::new()
[void] $MapLines.Add("LeonOS experimental 32-bit FAT32 hard-disk image")
[void] $MapLines.Add("disk_bytes = $DiskBytes")
[void] $MapLines.Add("disk_sectors = $DiskSectors")
[void] $MapLines.Add("partition_lba = $PartitionLba")
[void] $MapLines.Add("volume_sectors = $VolumeSectors")
[void] $MapLines.Add("bytes_per_sector = $BytesPerSector")
[void] $MapLines.Add("sectors_per_cluster = $SectorsPerCluster")
[void] $MapLines.Add("reserved_sectors = $ReservedSectors")
[void] $MapLines.Add("num_fats = $NumFats")
[void] $MapLines.Add("fat_size_sectors = $FatSize")
[void] $MapLines.Add("cluster_count = $ClusterCount")
[void] $MapLines.Add("root_cluster = $RootCluster")
[void] $MapLines.Add("first_data_rel_sector = $FirstDataRel")
[void] $MapLines.Add("")
foreach ($Record in $Records) {
    [void] $MapLines.Add(("{0,-12} cluster={1} clusters={2} size={3}" -f $Record.Name, $Record.FirstCluster, $Record.Clusters, $Record.Size))
}
[System.IO.File]::WriteAllLines((Join-Path $Dist "leonos32-hdd.map"), $MapLines)

Write-Host ("Built {0} ({1} bytes, {2} sectors)." -f $ImagePath, $DiskBytes, $DiskSectors)
Write-Host ("FAT32 partition at LBA {0}, {1} clusters, FAT size {2} sectors." -f $PartitionLba, $ClusterCount, $FatSize)
Write-Host ("FAT32 files: {0}" -f (($Records | ForEach-Object { $_.Name }) -join ", "))
