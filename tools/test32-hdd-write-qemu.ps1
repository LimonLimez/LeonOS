param(
    [string] $Image = "dist32\leonos32.img",
    [string] $HddImage = "dist32\leonos32-hdd.img",
    [int] $TimeoutSeconds = 25
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")

# Boots the FAT12 floppy with the FAT32 hard disk attached, then presses 'w' so
# the kernel creates+overwrites WRITE32.TXT on the FAT32 volume and reads it
# back in the same boot. After QEMU exits the host independently parses the
# FAT32 image and confirms WRITE32.TXT holds the expected bytes.
& (Join-Path $PSScriptRoot "build32-image.ps1") | Write-Host
& (Join-Path $PSScriptRoot "build32-hdd.ps1") | Write-Host

$Qemu = Get-LeonOsQemu
$ImagePath = Get-LeonOsImagePath $Image
$HddPath = Get-LeonOsImagePath $HddImage
$EscapedImagePath = $ImagePath -replace '"', '\"'
$EscapedHddPath = $HddPath -replace '"', '\"'

$ExpectedText = "LeonOS FAT32 write OK 2026`n"
$ExpectedBytes = [System.Text.Encoding]::ASCII.GetBytes($ExpectedText)

function Start-LeonOs32WriteTest {
    param([string] $Name, [string[]] $Commands)

    $Listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $Listener.Start()
    $MonitorPort = ([System.Net.IPEndPoint] $Listener.LocalEndpoint).Port
    $Listener.Stop()

    $StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $StartInfo.FileName = $script:Qemu
    $StartInfo.Arguments = "-name $Name -machine pc -cpu qemu32 -m 32M -drive file=`"$script:EscapedImagePath`",format=raw,if=floppy -drive file=`"$script:EscapedHddPath`",format=raw,if=ide,index=0,media=disk,cache=writethrough -boot a -serial stdio -display none -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -no-reboot"
    $StartInfo.UseShellExecute = $false
    $StartInfo.RedirectStandardOutput = $true
    $StartInfo.RedirectStandardError = $true

    $Process = [System.Diagnostics.Process]::Start($StartInfo)
    $Client = $null

    try {
        for ($Attempt = 0; $Attempt -lt 20 -and -not $Client; $Attempt++) {
            try {
                Start-Sleep -Milliseconds 150
                $Client = [System.Net.Sockets.TcpClient]::new("127.0.0.1", $MonitorPort)
            } catch {
                $Client = $null
            }
        }

        if (-not $Client) {
            throw "Could not connect to QEMU monitor on port $MonitorPort."
        }

        Start-Sleep -Seconds 8
        $Stream = $Client.GetStream()
        $Writer = [System.IO.StreamWriter]::new($Stream)
        $Writer.AutoFlush = $true

        foreach ($Command in $Commands) {
            if ($Command.StartsWith("sleep ")) {
                Start-Sleep -Milliseconds ([int] $Command.Substring(6))
            } else {
                $Writer.WriteLine($Command)
            }
        }

        Start-Sleep -Seconds 3
        $Writer.WriteLine("quit")
        $Writer.Dispose()
        $Client.Close()
        $Client = $null

        $Exited = $Process.WaitForExit($script:TimeoutSeconds * 1000)
        if (-not $Exited) {
            $Process.Kill()
            $Process.WaitForExit()
        }

        $Stdout = $Process.StandardOutput.ReadToEnd()
        $Stderr = $Process.StandardError.ReadToEnd()

        Write-Host "QEMU serial output ($Name):"
        Write-Host $Stdout
        if ($Stderr.Trim().Length -gt 0) {
            Write-Host "QEMU stderr ($Name):"
            Write-Host $Stderr
        }
        return $Stdout
    } finally {
        if ($Client) {
            $Client.Close()
        }
        if (-not $Process.HasExited) {
            $Process.Kill()
            $Process.WaitForExit()
        }
    }
}

# --- Host-side FAT32 reader (root dir, 8.3 names, cluster chain) ----------

function Read-U16 { param([byte[]] $B, [int] $O) return [int] $B[$O] -bor ([int] $B[$O + 1] -shl 8) }
function Read-U32 {
    param([byte[]] $B, [int] $O)
    return ([uint32] $B[$O]) -bor ([uint32] $B[$O + 1] -shl 8) -bor ([uint32] $B[$O + 2] -shl 16) -bor ([uint32] $B[$O + 3] -shl 24)
}

function Get-Fat32RootFile {
    param([byte[]] $Image, [string] $Name11)

    $PartLba = 0
    if ($Image[510] -eq 0x55 -and $Image[511] -eq 0xAA) {
        $Type = $Image[446 + 4]
        $Start = Read-U32 $Image (446 + 8)
        if (($Type -eq 0x0B -or $Type -eq 0x0C) -and $Start -ne 0) {
            $PartLba = [int] $Start
        }
    }

    $Vbr = $PartLba * 512
    $BytesPerSector = Read-U16 $Image ($Vbr + 0x0B)
    $SectorsPerCluster = [int] $Image[$Vbr + 0x0D]
    $Reserved = Read-U16 $Image ($Vbr + 0x0E)
    $NumFats = [int] $Image[$Vbr + 0x10]
    $FatSize = [int] (Read-U32 $Image ($Vbr + 0x24))
    $RootCluster = [int] (Read-U32 $Image ($Vbr + 0x2C))

    if ($BytesPerSector -ne 512) {
        throw "Host parser: unexpected bytes-per-sector $BytesPerSector."
    }

    $FirstFatSector = $PartLba + $Reserved
    $FirstDataSector = $PartLba + $Reserved + ($NumFats * $FatSize)

    function Get-NextCluster {
        param([int] $Cluster)
        $FatOffset = $Cluster * 4
        $Sector = $FirstFatSector + [int] ($FatOffset / 512)
        $EntryOffset = $FatOffset % 512
        $Value = Read-U32 $Image (($Sector * 512) + $EntryOffset)
        return [int] ($Value -band 0x0FFFFFFF)
    }

    $NameBytes = [System.Text.Encoding]::ASCII.GetBytes($Name11)
    $Cluster = $RootCluster
    $Guard = 0

    while ($Cluster -ge 2 -and $Cluster -lt 0x0FFFFFF8 -and $Guard -lt 65536) {
        $Guard += 1
        $BaseSector = $FirstDataSector + (($Cluster - 2) * $SectorsPerCluster)
        for ($S = 0; $S -lt $SectorsPerCluster; $S++) {
            $SectorOffset = ($BaseSector + $S) * 512
            for ($E = 0; $E -lt 16; $E++) {
                $EntryOffset = $SectorOffset + ($E * 32)
                if ($Image[$EntryOffset] -eq 0) {
                    return $null
                }
                if ($Image[$EntryOffset] -eq 0xE5) {
                    continue
                }
                if (($Image[$EntryOffset + 11] -band 0x0F) -eq 0x0F) {
                    continue
                }
                $Match = $true
                for ($K = 0; $K -lt 11; $K++) {
                    if ($Image[$EntryOffset + $K] -ne $NameBytes[$K]) {
                        $Match = $false
                        break
                    }
                }
                if ($Match) {
                    $FirstCluster = ((Read-U16 $Image ($EntryOffset + 20)) -shl 16) -bor (Read-U16 $Image ($EntryOffset + 26))
                    $Size = [int] (Read-U32 $Image ($EntryOffset + 28))
                    # follow the cluster chain and collect $Size bytes
                    $Content = New-Object System.Collections.Generic.List[byte]
                    $DataCluster = [int] $FirstCluster
                    $DataGuard = 0
                    while ($DataCluster -ge 2 -and $DataCluster -lt 0x0FFFFFF8 -and $Content.Count -lt $Size -and $DataGuard -lt 65536) {
                        $DataGuard += 1
                        $DataBase = $FirstDataSector + (($DataCluster - 2) * $SectorsPerCluster)
                        for ($DS = 0; $DS -lt $SectorsPerCluster -and $Content.Count -lt $Size; $DS++) {
                            $DataOffset = ($DataBase + $DS) * 512
                            for ($I = 0; $I -lt 512 -and $Content.Count -lt $Size; $I++) {
                                $Content.Add($Image[$DataOffset + $I])
                            }
                        }
                        $DataCluster = Get-NextCluster $DataCluster
                    }
                    return [pscustomobject] @{
                        FirstCluster = [int] $FirstCluster
                        Size = $Size
                        Content = $Content.ToArray()
                    }
                }
            }
        }
        $Cluster = Get-NextCluster $Cluster
    }
    return $null
}

# --- Run ------------------------------------------------------------------

$Output = Start-LeonOs32WriteTest "LeonOS-32BitHddWrite" @(
    "sendkey w",
    "sleep 1500"
)

$ExpectedLines = @(
    "LeonOS stage2 VBE 1920x1080x32 ready",
    "LeonOS 32-bit FAT32 volume mounted",
    "LeonOS 32-bit GUI ready",
    "LeonOS shell ready",
    "LeonOS cooperative scheduler OK"
)
foreach ($Expected in $ExpectedLines) {
    if ($Output -notlike "*$Expected*") {
        throw "QEMU did not emit expected serial line: $Expected"
    }
}
foreach ($Bad in @("FAT32 write FAILED", "readback FAILED", "readback MISMATCH")) {
    if ($Output -like "*$Bad*") {
        throw "Kernel reported a FAT32 write failure: $Bad"
    }
}

# Independent host-side confirmation that the bytes are really on the disk.
$ImageBytes = [System.IO.File]::ReadAllBytes($HddPath)
$File = Get-Fat32RootFile $ImageBytes "WRITE32 TXT"
if ($null -eq $File) {
    throw "Host FAT32 parser did not find WRITE32.TXT in $HddPath."
}
if ($File.Size -ne $ExpectedBytes.Length) {
    throw "WRITE32.TXT size is $($File.Size); expected $($ExpectedBytes.Length)."
}
for ($I = 0; $I -lt $ExpectedBytes.Length; $I++) {
    if ($File.Content[$I] -ne $ExpectedBytes[$I]) {
        throw "WRITE32.TXT byte $I is $($File.Content[$I]); expected $($ExpectedBytes[$I])."
    }
}

Write-Host ("Host confirmed WRITE32.TXT cluster={0} size={1}." -f $File.FirstCluster, $File.Size)
Write-Host ("Content: '{0}'" -f ([System.Text.Encoding]::ASCII.GetString($File.Content)).Replace("`n", "\n"))
Write-Host "QEMU 32-bit FAT32 HDD write/readback test passed."
