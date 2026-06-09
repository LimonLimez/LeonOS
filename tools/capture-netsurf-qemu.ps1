param(
    [string] $Image = "dist32\leonos32.img",
    [string] $HddImage = "dist32\leonos32-hdd.img",
    [string] $OutputPng = "dist32\netsurf-visible.png",
    [string] $WaitSerialPattern = "LeonOS user fb present",
    [int] $ExtraDelayMs = 500,
    [int] $TimeoutSeconds = 180,
    [switch] $SkipVisualAssert
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")

function Read-PpmToken {
    param(
        [byte[]] $Bytes,
        [ref] $Index
    )

    while ($Index.Value -lt $Bytes.Length) {
        $C = $Bytes[$Index.Value]
        if ($C -eq 35) {
            while ($Index.Value -lt $Bytes.Length -and
                   $Bytes[$Index.Value] -ne 10) {
                $Index.Value += 1
            }
        } elseif ($C -le 32) {
            $Index.Value += 1
        } else {
            break
        }
    }

    $Start = $Index.Value
    while ($Index.Value -lt $Bytes.Length -and $Bytes[$Index.Value] -gt 32) {
        $Index.Value += 1
    }
    return [System.Text.Encoding]::ASCII.GetString(
        $Bytes, $Start, $Index.Value - $Start)
}

function Convert-PpmToPng {
    param(
        [string] $PpmPath,
        [string] $PngPath
    )

    Add-Type -AssemblyName System.Drawing

    $Bytes = [System.IO.File]::ReadAllBytes($PpmPath)
    $Index = 0
    $Magic = Read-PpmToken $Bytes ([ref] $Index)
    if ($Magic -ne "P6") {
        throw "Unexpected PPM magic '$Magic'."
    }
    $Width = [int] (Read-PpmToken $Bytes ([ref] $Index))
    $Height = [int] (Read-PpmToken $Bytes ([ref] $Index))
    $Max = [int] (Read-PpmToken $Bytes ([ref] $Index))
    if ($Max -ne 255) {
        throw "Unsupported PPM max value '$Max'."
    }
    if ($Index -lt $Bytes.Length -and $Bytes[$Index] -le 32) {
        $Index += 1
    }

    $Bitmap = [System.Drawing.Bitmap]::new(
        $Width, $Height,
        [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    try {
        $Rect = [System.Drawing.Rectangle]::new(0, 0, $Width, $Height)
        $Data = $Bitmap.LockBits(
            $Rect,
            [System.Drawing.Imaging.ImageLockMode]::WriteOnly,
            [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
        try {
            $Stride = [Math]::Abs($Data.Stride)
            $Rgb = New-Object byte[] ($Stride * $Height)
            $Pos = $Index
            for ($Y = 0; $Y -lt $Height; $Y += 1) {
                $Row = $Y * $Stride
                for ($X = 0; $X -lt $Width; $X += 1) {
                    $R = $Bytes[$Pos]
                    $G = $Bytes[$Pos + 1]
                    $B = $Bytes[$Pos + 2]
                    $Dst = $Row + ($X * 3)
                    $Rgb[$Dst] = $B
                    $Rgb[$Dst + 1] = $G
                    $Rgb[$Dst + 2] = $R
                    $Pos += 3
                }
            }
            [System.Runtime.InteropServices.Marshal]::Copy(
                $Rgb, 0, $Data.Scan0, $Rgb.Length)
        } finally {
            $Bitmap.UnlockBits($Data)
        }
        $Bitmap.Save($PngPath, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $Bitmap.Dispose()
    }
}

function Test-PpmViewportNotBlank {
    param([string] $PpmPath)

    $Bytes = [System.IO.File]::ReadAllBytes($PpmPath)
    $Index = 0
    $Magic = Read-PpmToken $Bytes ([ref] $Index)
    if ($Magic -ne "P6") {
        throw "Unexpected PPM magic '$Magic'."
    }
    $Width = [int] (Read-PpmToken $Bytes ([ref] $Index))
    $Height = [int] (Read-PpmToken $Bytes ([ref] $Index))
    $Max = [int] (Read-PpmToken $Bytes ([ref] $Index))
    if ($Max -ne 255) {
        throw "Unsupported PPM max value '$Max'."
    }
    if ($Index -lt $Bytes.Length -and $Bytes[$Index] -le 32) {
        $Index += 1
    }

    $X0 = [Math]::Min([Math]::Max(48, 0), $Width - 1)
    $Y0 = [Math]::Min([Math]::Max(112, 0), $Height - 1)
    $X1 = [Math]::Max($X0 + 1, $Width - 48)
    $Y1 = [Math]::Max($Y0 + 1, $Height - 48)
    $StepX = [Math]::Max(1, [int](($X1 - $X0) / 160))
    $StepY = [Math]::Max(1, [int](($Y1 - $Y0) / 90))
    $Interesting = 0

    for ($Y = $Y0; $Y -lt $Y1; $Y += $StepY) {
        for ($X = $X0; $X -lt $X1; $X += $StepX) {
            $Pos = $Index + (($Y * $Width + $X) * 3)
            if ($Pos + 2 -ge $Bytes.Length) {
                continue
            }
            $R = $Bytes[$Pos]
            $G = $Bytes[$Pos + 1]
            $B = $Bytes[$Pos + 2]
            $NotPaper = -not ($R -ge 238 -and $G -ge 238 -and $B -ge 238)
            $Saturated = ([Math]::Max($R, [Math]::Max($G, $B)) -
                [Math]::Min($R, [Math]::Min($G, $B))) -ge 40
            $DarkText = ($R -lt 120 -and $G -lt 120 -and $B -lt 120)
            if ($NotPaper -and ($Saturated -or $DarkText)) {
                $Interesting += 1
            }
        }
    }

    if ($Interesting -lt 20) {
        throw "NetSurf screenshot viewport looks blank; only $Interesting interesting sampled pixels found."
    }
}

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$Qemu = Get-LeonOsQemu
$ImagePath = Get-LeonOsImagePath $Image
$HddPath = Get-LeonOsImagePath $HddImage
$OutputPngPath = if ([System.IO.Path]::IsPathRooted($OutputPng)) {
    $OutputPng
} else {
    Join-Path $Root $OutputPng
}
$OutputPngPath = [System.IO.Path]::GetFullPath($OutputPngPath)
$OutputPpmPath = [System.IO.Path]::ChangeExtension($OutputPngPath, ".ppm")

$SerialLog = Get-LeonOsSerialLogPath "LeonOS-NetSurf-Screenshot"
Remove-Item -LiteralPath $SerialLog -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $OutputPpmPath -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $OutputPngPath -Force -ErrorAction SilentlyContinue
$SerialArg = Get-LeonOsQemuSerialFileArg $SerialLog

$Listener = [System.Net.Sockets.TcpListener]::new(
    [System.Net.IPAddress]::Loopback, 0)
$Listener.Start()
$MonitorPort = ([System.Net.IPEndPoint] $Listener.LocalEndpoint).Port
$Listener.Stop()

$StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
$StartInfo.FileName = $Qemu
$EscapedImagePath = $ImagePath -replace '"', '\"'
$EscapedHddPath = $HddPath -replace '"', '\"'
$StartInfo.Arguments = "-name LeonOS-NetSurf-Screenshot -machine pc -cpu qemu32 -m 256M -drive file=`"$EscapedImagePath`",format=raw,if=floppy -drive file=`"$EscapedHddPath`",format=raw,if=ide,index=0,media=disk -boot a -vga std -serial $SerialArg -display none -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -nic user,model=rtl8139 -no-reboot"
$StartInfo.UseShellExecute = $false
$StartInfo.RedirectStandardError = $true

$Process = [System.Diagnostics.Process]::Start($StartInfo)
$Client = $null

try {
    for ($Attempt = 0; $Attempt -lt 120 -and -not $Client; $Attempt += 1) {
        try {
            Start-Sleep -Milliseconds 150
            $Client = [System.Net.Sockets.TcpClient]::new(
                "127.0.0.1", $MonitorPort)
        } catch {
            $Client = $null
        }
    }
    if (-not $Client) {
        throw "Could not connect to QEMU monitor."
    }

    $Stream = $Client.GetStream()
    $Writer = [System.IO.StreamWriter]::new($Stream)
    $Writer.AutoFlush = $true
    Wait-QemuMonitorPrompt $Stream 8000

    $BootText = Wait-LeonOsSerialLog $SerialLog "LeonOS shell ready" 90000
    if (-not $BootText.Contains("LeonOS shell ready")) {
        throw "QEMU did not reach LeonOS shell ready."
    }
    $SchedulerText = Wait-LeonOsSerialLog $SerialLog "LeonOS cooperative scheduler OK" 90000
    if (-not $SchedulerText.Contains("LeonOS cooperative scheduler OK")) {
        throw "QEMU did not reach LeonOS cooperative scheduler."
    }

    $Writer.WriteLine("sendkey m")
    Wait-QemuMonitorPrompt $Stream 3000

    $Deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $SawRequestedState = $false
    while ([DateTime]::UtcNow -lt $Deadline -and -not $Process.HasExited) {
        $Text = Read-LeonOsSerialLog $SerialLog
        if ($Text.Contains($WaitSerialPattern) -and
            $Text.Contains("WINDOW REDRAW WIN 0 STOP")) {
            $SawRequestedState = $true
            break
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $SawRequestedState) {
        throw "QEMU did not reach requested NetSurf capture state '$WaitSerialPattern'."
    }
    if ($ExtraDelayMs -gt 0) {
        Start-Sleep -Milliseconds $ExtraDelayMs
    }

    $QemuPpmPath = $OutputPpmPath -replace "\\", "/"
    $Writer.WriteLine("screendump $QemuPpmPath")
    Wait-QemuMonitorPrompt $Stream 10000
    Start-Sleep -Milliseconds 500

    $Writer.WriteLine("quit")
    $Writer.Dispose()
    $Client.Close()
    $Client = $null

    if (-not $Process.WaitForExit(10000)) {
        $Process.Kill()
        $Process.WaitForExit()
    }
} finally {
    if ($Client) {
        $Client.Close()
    }
    if (-not $Process.HasExited) {
        $Process.Kill()
        $Process.WaitForExit()
    }
}

if (-not (Test-Path -LiteralPath $OutputPpmPath)) {
    throw "QEMU screendump did not create $OutputPpmPath."
}

if (-not $SkipVisualAssert) {
    Test-PpmViewportNotBlank -PpmPath $OutputPpmPath
}
Convert-PpmToPng -PpmPath $OutputPpmPath -PngPath $OutputPngPath
Write-Host "Captured $OutputPngPath"
