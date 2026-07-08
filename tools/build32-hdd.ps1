param(
    [string] $OutDir = "dist32",
    [switch] $ExcludeNetSurf
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
$Src32 = Join-Path $Root "src32"
$Include = Join-Path $Src32 "include"
$UserRoot = Join-Path $Src32 "user"

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

function Sync-Leo1ImageSize {
    param([byte[]] $Bytes)
    if ($Bytes.Length -lt 20) {
        throw "LEO1 image is too small to patch."
    }
    $ImageSize = $Bytes.Length
    $Bytes[12] = [byte] ($ImageSize -band 0xFF)
    $Bytes[13] = [byte] (($ImageSize -shr 8) -band 0xFF)
    $Bytes[14] = [byte] (($ImageSize -shr 16) -band 0xFF)
    $Bytes[15] = [byte] (($ImageSize -shr 24) -band 0xFF)
    return $Bytes
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

# Assemble the first user-mode framebuffer syscall probe. UGFX.LEO intentionally
# spans more than one page so the ring-3 loader exercises multi-page reads,
# mapping, and cleanup.
$UserGfxSrc = Join-Path $Root "src32\app\ugfx.asm"
$UserGfxBin = Join-Path $Dist "UGFX.LEO"
& $Nasm -f bin $UserGfxSrc -o $UserGfxBin
if ($LASTEXITCODE -ne 0) {
    throw "NASM failed while assembling src32\app\ugfx.asm."
}
$UserGfxBytes = [System.IO.File]::ReadAllBytes($UserGfxBin)
if ($UserGfxBytes.Length -lt 32 -or
    $UserGfxBytes[0] -ne 0x4C -or $UserGfxBytes[1] -ne 0x45 -or
    $UserGfxBytes[2] -ne 0x4F -or $UserGfxBytes[3] -ne 0x31) {
    throw "UGFX.LEO is missing the LEO1 header."
}
if ($UserGfxBytes[4] -ne 0x02) {
    throw "UGFX.LEO must declare LEO1 ABI version 2 (ring-3 user app)."
}
if ($UserGfxBytes.Length -gt 1048576) {
    throw "UGFX.LEO exceeds the current 1 MiB user-app loader limit."
}
Add-FileToImage "UGFX.LEO" $UserGfxBytes 0x20

# Build a freestanding C ring-3 user app. This is the first real step toward
# compiling an existing C browser frontend for LeonOS: C code enters through a
# tiny LEO1 crt0 and calls only the implemented syscall ABI.
$Toolchain = Get-LeonOsI386Toolchain
$UserCrtObj = Join-Path $Dist "ucdemo-crt0.o"
$UserCObj = Join-Path $Dist "ucdemo.o"
$UserElf = Join-Path $Dist "UCDEMO.elf"
$UserCBin = Join-Path $Dist "UCDEMO.LEO"
& $Nasm -f elf32 (Join-Path $UserRoot "crt0.asm") -o $UserCrtObj
if ($LASTEXITCODE -ne 0) {
    throw "NASM failed while assembling src32\user\crt0.asm."
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
        "-c" (Join-Path $UserRoot "cdemo.c") `
        "-o" $UserCObj
    if ($LASTEXITCODE -ne 0) {
        throw "clang failed while building src32\user\cdemo.c."
    }
    & $Toolchain.Ld `
        "-m" "elf_i386" `
        "-T" (Join-Path $UserRoot "leonos_user.ld") `
        "-nostdlib" `
        "-Map" (Join-Path $Dist "UCDEMO.link.map") `
        "-o" $UserElf `
        $UserCrtObj `
        $UserCObj
    if ($LASTEXITCODE -ne 0) {
        throw "ld.lld failed while linking UCDEMO.elf."
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
        "-c" (Join-Path $UserRoot "cdemo.c") `
        "-o" $UserCObj
    if ($LASTEXITCODE -ne 0) {
        throw "i686-elf-gcc failed while building src32\user\cdemo.c."
    }
    & $Toolchain.Ld `
        "-T" (Join-Path $UserRoot "leonos_user.ld") `
        "-nostdlib" `
        "-Map" (Join-Path $Dist "UCDEMO.link.map") `
        "-o" $UserElf `
        $UserCrtObj `
        $UserCObj
    if ($LASTEXITCODE -ne 0) {
        throw "i686-elf-ld failed while linking UCDEMO.elf."
    }
} else {
    throw "Unsupported toolchain kind '$($Toolchain.Kind)' while building UCDEMO.LEO."
}
& $Toolchain.Objcopy "-O" "binary" $UserElf $UserCBin
if ($LASTEXITCODE -ne 0) {
    throw "objcopy failed while producing UCDEMO.LEO."
}
$UserCBytes = [System.IO.File]::ReadAllBytes($UserCBin)
if ($UserCBytes.Length -lt 32 -or
    $UserCBytes[0] -ne 0x4C -or $UserCBytes[1] -ne 0x45 -or
    $UserCBytes[2] -ne 0x4F -or $UserCBytes[3] -ne 0x31) {
    throw "UCDEMO.LEO is missing the LEO1 header."
}
if ($UserCBytes[4] -ne 0x02) {
    throw "UCDEMO.LEO must declare LEO1 ABI version 2 (ring-3 user app)."
}
if ($UserCBytes.Length -gt 1048576) {
    throw "UCDEMO.LEO exceeds the current 1 MiB user-app loader limit."
}
Add-FileToImage "UCDEMO.LEO" $UserCBytes 0x20

# Build the ring-3 browser launcher (opens the kernel HTTPS browser after exit).
$UserBrowserCrtObj = Join-Path $Dist "ubrowser-crt0.o"
$UserBrowserCObj = Join-Path $Dist "ubrowser.o"
$UserBrowserElf = Join-Path $Dist "UBROWSER.elf"
$UserBrowserBin = Join-Path $Dist "UBROWSER.LEO"
& $Nasm -f elf32 (Join-Path $UserRoot "crt0.asm") -o $UserBrowserCrtObj
if ($LASTEXITCODE -ne 0) {
    throw "NASM failed while assembling src32\user\crt0.asm for UBROWSER."
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
        "-c" (Join-Path $UserRoot "ubrowser.c") `
        "-o" $UserBrowserCObj
    if ($LASTEXITCODE -ne 0) {
        throw "clang failed while building src32\user\ubrowser.c."
    }
    & $Toolchain.Ld `
        "-m" "elf_i386" `
        "-T" (Join-Path $UserRoot "leonos_user.ld") `
        "-nostdlib" `
        "-Map" (Join-Path $Dist "UBROWSER.link.map") `
        "-o" $UserBrowserElf `
        $UserBrowserCrtObj `
        $UserBrowserCObj
    if ($LASTEXITCODE -ne 0) {
        throw "ld.lld failed while linking UBROWSER.elf."
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
        "-c" (Join-Path $UserRoot "ubrowser.c") `
        "-o" $UserBrowserCObj
    if ($LASTEXITCODE -ne 0) {
        throw "i686-elf-gcc failed while building src32\user\ubrowser.c."
    }
    & $Toolchain.Ld `
        "-T" (Join-Path $UserRoot "leonos_user.ld") `
        "-nostdlib" `
        "-Map" (Join-Path $Dist "UBROWSER.link.map") `
        "-o" $UserBrowserElf `
        $UserBrowserCrtObj `
        $UserBrowserCObj
    if ($LASTEXITCODE -ne 0) {
        throw "i686-elf-ld failed while linking UBROWSER.elf."
    }
} else {
    throw "Unsupported toolchain kind '$($Toolchain.Kind)' while building UBROWSER.LEO."
}
& $Toolchain.Objcopy "-O" "binary" $UserBrowserElf $UserBrowserBin
if ($LASTEXITCODE -ne 0) {
    throw "objcopy failed while producing UBROWSER.LEO."
}
$UserBrowserBytes = Sync-Leo1ImageSize ([System.IO.File]::ReadAllBytes($UserBrowserBin))
[System.IO.File]::WriteAllBytes($UserBrowserBin, $UserBrowserBytes)
if ($UserBrowserBytes.Length -lt 32 -or
    $UserBrowserBytes[0] -ne 0x4C -or $UserBrowserBytes[1] -ne 0x45 -or
    $UserBrowserBytes[2] -ne 0x4F -or $UserBrowserBytes[3] -ne 0x31) {
    throw "UBROWSER.LEO is missing the LEO1 header."
}
if ($UserBrowserBytes[4] -ne 0x02) {
    throw "UBROWSER.LEO must declare LEO1 ABI version 2 (ring-3 user app)."
}
if ($UserBrowserBytes.Length -gt 1048576) {
    throw "UBROWSER.LEO exceeds the current 1 MiB user-app loader limit."
}
Add-FileToImage "UBROWSER.LEO" $UserBrowserBytes 0x20

# NetSurf port runtime harness: yield/millis/malloc while drawing, then browser open.
$UserNetCrtObj = Join-Path $Dist "unetsurf-crt0.o"
$UserNetCObj = Join-Path $Dist "unetsurf.o"
$UserNetElf = Join-Path $Dist "UNETRUN.elf"
$UserNetBin = Join-Path $Dist "UNETRUN.LEO"
& $Nasm -f elf32 (Join-Path $UserRoot "crt0.asm") -o $UserNetCrtObj
if ($LASTEXITCODE -ne 0) {
    throw "NASM failed while assembling src32\user\crt0.asm for UNETRUN."
}
if ($Toolchain.Kind -eq "clang-lld") {
    & $Toolchain.Cc `
        "--target=i686-elf" "-std=c11" "-Os" "-ffreestanding" "-fno-builtin" `
        "-fno-stack-protector" "-fno-jump-tables" "-fno-pic" "-fno-pie" `
        "-mno-sse" "-mno-mmx" "-m32" "-nostdinc" "-Wall" "-Wextra" `
        "-I$Include" "-c" (Join-Path $UserRoot "unetsurf.c") "-o" $UserNetCObj
    if ($LASTEXITCODE -ne 0) { throw "clang failed while building unetsurf.c." }
    & $Toolchain.Ld "-m" "elf_i386" "-T" (Join-Path $UserRoot "leonos_user.ld") `
        "-nostdlib" "-Map" (Join-Path $Dist "UNETRUN.link.map") "-o" $UserNetElf `
        $UserNetCrtObj $UserNetCObj
    if ($LASTEXITCODE -ne 0) { throw "ld.lld failed while linking UNETRUN.elf." }
} elseif ($Toolchain.Kind -eq "i686-elf-gcc") {
    & $Toolchain.Cc `
        "-std=c11" "-Os" "-ffreestanding" "-fno-builtin" "-fno-stack-protector" `
        "-fno-jump-tables" "-fno-pic" "-fno-pie" "-mno-sse" "-mno-mmx" "-m32" `
        "-nostdinc" "-Wall" "-Wextra" "-I$Include" `
        "-c" (Join-Path $UserRoot "unetsurf.c") "-o" $UserNetCObj
    if ($LASTEXITCODE -ne 0) { throw "i686-elf-gcc failed while building unetsurf.c." }
    & $Toolchain.Ld "-T" (Join-Path $UserRoot "leonos_user.ld") "-nostdlib" `
        "-Map" (Join-Path $Dist "UNETRUN.link.map") "-o" $UserNetElf $UserNetCrtObj $UserNetCObj
    if ($LASTEXITCODE -ne 0) { throw "i686-elf-ld failed while linking UNETRUN.elf." }
} else {
    throw "Unsupported toolchain kind '$($Toolchain.Kind)' while building UNETRUN.LEO."
}
& $Toolchain.Objcopy "-O" "binary" $UserNetElf $UserNetBin
if ($LASTEXITCODE -ne 0) { throw "objcopy failed while producing UNETRUN.LEO." }
$UserNetBytes = Sync-Leo1ImageSize ([System.IO.File]::ReadAllBytes($UserNetBin))
[System.IO.File]::WriteAllBytes($UserNetBin, $UserNetBytes)
if ($UserNetBytes.Length -gt 1048576) {
    throw "UNETRUN.LEO exceeds the current 1 MiB user-app loader limit."
}
Add-FileToImage "UNETRUN.LEO" $UserNetBytes 0x20

# User HTTPS fetch then kernel browser render (UWEB.LEO).
$UserWebCrtObj = Join-Path $Dist "uweb-crt0.o"
$UserWebCObj = Join-Path $Dist "uweb.o"
$UserWebElf = Join-Path $Dist "UWEB.elf"
$UserWebBin = Join-Path $Dist "UWEB.LEO"
& $Nasm -f elf32 (Join-Path $UserRoot "crt0.asm") -o $UserWebCrtObj
if ($Toolchain.Kind -eq "clang-lld") {
    & $Toolchain.Cc `
        "--target=i686-elf" "-std=c11" "-Os" "-ffreestanding" "-fno-builtin" `
        "-fno-stack-protector" "-fno-jump-tables" "-fno-pic" "-fno-pie" `
        "-mno-sse" "-mno-mmx" "-m32" "-nostdinc" "-Wall" "-Wextra" `
        "-I$Include" "-c" (Join-Path $UserRoot "uweb.c") "-o" $UserWebCObj
    if ($LASTEXITCODE -ne 0) { throw "clang failed while building uweb.c." }
    & $Toolchain.Ld "-m" "elf_i386" "-T" (Join-Path $UserRoot "leonos_user.ld") `
        "-nostdlib" "-Map" (Join-Path $Dist "UWEB.link.map") "-o" $UserWebElf `
        $UserWebCrtObj $UserWebCObj
    if ($LASTEXITCODE -ne 0) { throw "ld.lld failed while linking UWEB.elf." }
} elseif ($Toolchain.Kind -eq "i686-elf-gcc") {
    & $Toolchain.Cc `
        "-std=c11" "-Os" "-ffreestanding" "-fno-builtin" "-fno-stack-protector" `
        "-fno-jump-tables" "-fno-pic" "-fno-pie" "-mno-sse" "-mno-mmx" "-m32" `
        "-nostdinc" "-Wall" "-Wextra" "-I$Include" `
        "-c" (Join-Path $UserRoot "uweb.c") "-o" $UserWebCObj
    if ($LASTEXITCODE -ne 0) { throw "i686-elf-gcc failed while building uweb.c." }
    & $Toolchain.Ld "-T" (Join-Path $UserRoot "leonos_user.ld") "-nostdlib" `
        "-Map" (Join-Path $Dist "UWEB.link.map") "-o" $UserWebElf $UserWebCrtObj $UserWebCObj
    if ($LASTEXITCODE -ne 0) { throw "i686-elf-ld failed while linking UWEB.elf." }
} else {
    throw "Unsupported toolchain while building UWEB.LEO."
}
& $Toolchain.Objcopy "-O" "binary" $UserWebElf $UserWebBin
$UserWebBytes = Sync-Leo1ImageSize ([System.IO.File]::ReadAllBytes($UserWebBin))
[System.IO.File]::WriteAllBytes($UserWebBin, $UserWebBytes)
if ($UserWebBytes.Length -gt 1048576) {
    throw "UWEB.LEO exceeds the current 1 MiB user-app loader limit."
}
Add-FileToImage "UWEB.LEO" $UserWebBytes 0x20

# User HTTPS stream probe (USTREAM.LEO).
$UserStreamCrtObj = Join-Path $Dist "ustream-crt0.o"
$UserStreamCObj = Join-Path $Dist "ustream.o"
$UserStreamElf = Join-Path $Dist "USTREAM.elf"
$UserStreamBin = Join-Path $Dist "USTREAM.LEO"
& $Nasm -f elf32 (Join-Path $UserRoot "crt0.asm") -o $UserStreamCrtObj
if ($Toolchain.Kind -eq "clang-lld") {
    & $Toolchain.Cc `
        "--target=i686-elf" "-std=c11" "-Os" "-ffreestanding" "-fno-builtin" `
        "-fno-stack-protector" "-fno-jump-tables" "-fno-pic" "-fno-pie" `
        "-mno-sse" "-mno-mmx" "-m32" "-nostdinc" "-Wall" "-Wextra" `
        "-I$Include" "-c" (Join-Path $UserRoot "ustream.c") "-o" $UserStreamCObj
    if ($LASTEXITCODE -ne 0) { throw "clang failed while building ustream.c." }
    & $Toolchain.Ld "-m" "elf_i386" "-T" (Join-Path $UserRoot "leonos_user.ld") `
        "-nostdlib" "-Map" (Join-Path $Dist "USTREAM.link.map") "-o" $UserStreamElf `
        $UserStreamCrtObj $UserStreamCObj
    if ($LASTEXITCODE -ne 0) { throw "ld.lld failed while linking USTREAM.elf." }
} elseif ($Toolchain.Kind -eq "i686-elf-gcc") {
    & $Toolchain.Cc `
        "-std=c11" "-Os" "-ffreestanding" "-fno-builtin" "-fno-stack-protector" `
        "-fno-jump-tables" "-fno-pic" "-fno-pie" "-mno-sse" "-mno-mmx" "-m32" `
        "-nostdinc" "-Wall" "-Wextra" "-I$Include" `
        "-c" (Join-Path $UserRoot "ustream.c") "-o" $UserStreamCObj
    if ($LASTEXITCODE -ne 0) { throw "i686-elf-gcc failed while building ustream.c." }
    & $Toolchain.Ld "-T" (Join-Path $UserRoot "leonos_user.ld") "-nostdlib" `
        "-Map" (Join-Path $Dist "USTREAM.link.map") "-o" $UserStreamElf $UserStreamCrtObj $UserStreamCObj
    if ($LASTEXITCODE -ne 0) { throw "i686-elf-ld failed while linking USTREAM.elf." }
} else {
    throw "Unsupported toolchain while building USTREAM.LEO."
}
& $Toolchain.Objcopy "-O" "binary" $UserStreamElf $UserStreamBin
$UserStreamBytes = Sync-Leo1ImageSize ([System.IO.File]::ReadAllBytes($UserStreamBin))
[System.IO.File]::WriteAllBytes($UserStreamBin, $UserStreamBytes)
if ($UserStreamBytes.Length -gt 1048576) {
    throw "USTREAM.LEO exceeds the current 1 MiB user-app loader limit."
}
Add-FileToImage "USTREAM.LEO" $UserStreamBytes 0x20

$NetSurfLeoPath = Join-Path $Dist "netsurf-probe\NETSURF.LEO"
if (-not $ExcludeNetSurf -and (Test-Path -LiteralPath $NetSurfLeoPath)) {
    $NetSurfLeoBytes = [System.IO.File]::ReadAllBytes($NetSurfLeoPath)
    if ($NetSurfLeoBytes.Length -lt 32 -or
        $NetSurfLeoBytes[0] -ne 0x4C -or $NetSurfLeoBytes[1] -ne 0x45 -or
        $NetSurfLeoBytes[2] -ne 0x4F -or $NetSurfLeoBytes[3] -ne 0x31) {
        throw "NETSURF.LEO is missing the LEO1 header."
    }
    if ($NetSurfLeoBytes[4] -ne 0x02) {
        throw "NETSURF.LEO must declare LEO1 ABI version 2 (ring-3 user app)."
    }
    if ($NetSurfLeoBytes.Length -gt (4 * 1048576)) {
        throw "NETSURF.LEO exceeds the current 4 MiB user-app loader limit."
    }
    Add-FileToImage "NETSURF.LEO" $NetSurfLeoBytes 0x20
}

$QuickJsLeoPath = Join-Path $Dist "quickjs-probe\UQJS.LEO"
if (Test-Path -LiteralPath $QuickJsLeoPath) {
    $QuickJsLeoBytes = [System.IO.File]::ReadAllBytes($QuickJsLeoPath)
    if ($QuickJsLeoBytes.Length -lt 32 -or
        $QuickJsLeoBytes[0] -ne 0x4C -or $QuickJsLeoBytes[1] -ne 0x45 -or
        $QuickJsLeoBytes[2] -ne 0x4F -or $QuickJsLeoBytes[3] -ne 0x31) {
        throw "UQJS.LEO is missing the LEO1 header."
    }
    if ($QuickJsLeoBytes[4] -ne 0x02) {
        throw "UQJS.LEO must declare LEO1 ABI version 2 (ring-3 user app)."
    }
    if ($QuickJsLeoBytes.Length -gt (4 * 1048576)) {
        throw "UQJS.LEO exceeds the current 4 MiB user-app loader limit."
    }
    Add-FileToImage "UQJS.LEO" $QuickJsLeoBytes 0x20
}

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
