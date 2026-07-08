Set-StrictMode -Version Latest

function Scale-LeonOsX {
    param([int] $RefX, [int] $Width)
    return [int](($RefX * $Width + 960) / 1920)
}

function Scale-LeonOsY {
    param([int] $RefY, [int] $Height)
    return [int](($RefY * $Height + 540) / 1080)
}

function Get-PpmRgbFromKernelColor {
    param([int] $Color)
    $Kr = ($Color -shr 16) -band 0xFF
    $Kg = ($Color -shr 8) -band 0xFF
    $Kb = $Color -band 0xFF
    return @{ R = $Kr; G = $Kg; B = $Kb }
}

function Get-LeonOsResolutionSize {
    param(
        [ValidateSet("1080p", "720p", "768p", "1366")]
        [string] $Resolution = "1080p"
    )
    switch ($Resolution) {
        "1080p" { return @{ Width = 1920; Height = 1080 } }
        "720p" { return @{ Width = 1280; Height = 720 } }
        "768p" { return @{ Width = 1024; Height = 768 } }
        "1366" { return @{ Width = 1366; Height = 768 } }
        default { throw "Unknown resolution '$Resolution'." }
    }
}

function Get-LeonOsVisualExpectations {
    param(
        [int] $Width,
        [int] $Height
    )

    $Sx = { param([int] $V) Scale-LeonOsX $V $Width }
    $Sy = { param([int] $V) Scale-LeonOsY $V $Height }

    $MarginX = (& $Sx 12)
    $WinY = (& $Sy 10)
    $WinW = $Width - (& $Sx 24)
    $WinX = $MarginX
    if ($Height -ge 1000) {
        $CapW = [int]($Width * 0.90)
        $CapH = [int](($Height - $WinY) * 0.90)
        if ($WinW -gt $CapW) {
            $WinW = $CapW
            $WinX = [int](($Width - $CapW) / 2)
        }
        if ($CapH -lt ($Height - $WinY - (& $Sy 60))) {
            # WinH computed below may be capped similarly in the guest; probes use actual WinX/WinW.
        }
    }
    $TaskbarH = if ($Height -le 720) { (& $Sy 52) } else { (& $Sy 48) }
    if ($TaskbarH -lt 40) { $TaskbarH = 40 }
    $TaskbarY = $Height - $TaskbarH
    $TitleH = if ($Height -le 720) { (& $Sy 48) } else { (& $Sy 42) }
    if ($TitleH -lt 36) { $TitleH = 36 }
    $TabH = if ($Height -le 720) { (& $Sy 36) } else { (& $Sy 32) }
    if ($TabH -lt 28) { $TabH = 28 }
    $LineH = 21
    $AddrH = $LineH + (& $Sy 10)
    if ($AddrH -lt 28) { $AddrH = 28 }
    $BodyY = $WinY + $TitleH + $TabH + $AddrH
    $SidebarW = [Math]::Max((& $Sx 140), [int]($WinW * 0.36))
    if ($SidebarW -gt (& $Sx 280)) {
        $SidebarW = (& $Sx 280)
    }
    if (($SidebarW + (& $Sx 120)) -gt $WinW) {
        $SidebarW = $WinW - (& $Sx 120)
    }
    $ContentX = $SidebarW + (& $Sx 2)
    $CloseX = $WinX + $WinW - (& $Sx 32)
    $CursorX = (& $Sx 820)
    $CursorY = (& $Sy 470)
    $WallY = 4
    $WallX = 8
    $WallKr = 11
    $WallKg = 45 + [int]((70 * $WallY + $Height / 2) / $Height)
    $WallKb = 70 + [int]((140 * $WallY + $Height / 2) / $Height)
    $Wall = @{ X = $WallX; Y = $WallY; R = $WallKr; G = $WallKg; B = $WallKb }
    $Title = Get-PpmRgbFromKernelColor 0x001A73E8
    $Close = Get-PpmRgbFromKernelColor 0x00D93025
    $SidebarRgb = Get-PpmRgbFromKernelColor 0x00EEF1F5
    $ContentRgb = Get-PpmRgbFromKernelColor 0x00F7F9FC
    $StartRgb = Get-PpmRgbFromKernelColor 0x001A73E8
    $TaskRgb = Get-PpmRgbFromKernelColor 0x001F2226

    $BtnW = if ($Height -le 720) { (& $Sx 72) } else { (& $Sx 54) }
    if ($BtnW -lt 40) { $BtnW = 40 }
    $StartW = if ($Height -le 720) { (& $Sy 58) } else { (& $Sy 54) }
    $StartBoxW = $StartW - (& $Sx 4)
    $StartBoxH = $TaskbarH - (& $Sy 10)
    $StartCx = (& $Sx 8) + [int]($StartBoxW / 2)
    $StartCy = $TaskbarY + (& $Sy 5) + [int]($StartBoxH / 2)
    $StartPane = [int]([Math]::Min($StartBoxW, $StartBoxH) / 4)
    if ($StartPane -lt 5) { $StartPane = 5 }
    $StartGap = [int]($StartPane / 2)
    if ($StartGap -lt 3) { $StartGap = 3 }
    $StartIconX = $StartCx - $StartPane - [int]($StartGap / 2) + [int]($StartPane / 2)
    $StartIconY = $StartCy - $StartPane - [int]($StartGap / 2) + [int]($StartPane / 2)
    $CloseIconX = $WinX + $WinW - [int]($BtnW / 2)
    $CloseIconY = $WinY + [int]($TitleH / 2)

    $StartSpriteRgb = @{ R = 255; G = 255; B = 255 }
    $CloseSpriteRgb = @{ R = 255; G = 255; B = 255 }

    return [pscustomobject] @{
        Width = $Width
        Height = $Height
        Wallpaper = $Wall
        TitleBar = @{
            X = ($WinX + (& $Sx 300)); Y = ($WinY + (& $Sy 14))
            R = $Title.R; G = $Title.G; B = $Title.B
        }
        CloseButton = @{
            X = $CloseX; Y = ($WinY + (& $Sy 12))
            R = $Close.R; G = $Close.G; B = $Close.B
        }
        Sidebar = @{
            X = ($WinX + [Math]::Min($SidebarW - (& $Sx 24), (& $Sx 140))); Y = ($BodyY + (& $Sy 200))
            R = $SidebarRgb.R; G = $SidebarRgb.G; B = $SidebarRgb.B
        }
        ContentPane = @{
            X = ($WinX + $ContentX + (& $Sx 24)); Y = ($BodyY + (& $Sy 90))
            R = $ContentRgb.R; G = $ContentRgb.G; B = $ContentRgb.B
        }
        StartButton = @{
            X = (& $Sx 8); Y = ($TaskbarY + [int]($TaskbarH / 2))
            R = $StartRgb.R; G = $StartRgb.G; B = $StartRgb.B
        }
        StartSpriteIcon = @{
            X = $StartIconX; Y = $StartIconY
            R = $StartSpriteRgb.R; G = $StartSpriteRgb.G; B = $StartSpriteRgb.B
        }
        TitlebarCloseSprite = @{
            X = $CloseIconX; Y = $CloseIconY
            R = $CloseSpriteRgb.R; G = $CloseSpriteRgb.G; B = $CloseSpriteRgb.B
        }
        Taskbar = @{
            X = (& $Sx 700); Y = ($TaskbarY + [int]($TaskbarH / 2))
            R = $TaskRgb.R; G = $TaskRgb.G; B = $TaskRgb.B
        }
        TabActive = @{
            X = ($WinX + (& $Sx 132))
            Y = ($WinY + $TitleH + 10)
            R = 255; G = 255; B = 255
        }
        OverlapGuard = @{
            ListX = $WinX + (& $Sx 8)
            ListY = ($BodyY + (& $Sy 8))
            ListW = $SidebarW - (& $Sx 24)
            ListH = (& $Sy 80)
            SpillX = $WinX + $ContentX + (& $Sx 40)
            SpillW = (& $Sx 120)
            MaxDark = [Math]::Max(12, [int](35 * $Width / 1920))
        }
        TitleTextBox = @{
            X = ($WinX + (& $Sx 12)); Y = ($BodyY + (& $Sy 8))
            Width = ($SidebarW - (& $Sx 24)); Height = (& $Sy 80)
            MinDark = [Math]::Max(25, [int](40 * $Width / 1920))
        }
        ContentTextBox = @{
            X = ($WinX + $ContentX + (& $Sx 12)); Y = ($WinY + (& $Sy 170))
            Width = (& $Sx 200); Height = (& $Sy 60)
            MinDark = 0
        }
        CursorBox = @{
            X = $CursorX; Y = $CursorY
            Width = (& $Sx 24); Height = (& $Sy 24)
            MinDark = 0
        }
        CursorTargetX = $CursorX
        CursorTargetY = $CursorY
        WinW = $WinW
        WinH = $Height - $WinY - (& $Sy 52) - (& $Sy 8)
        MinWinW720 = 1000
        MinWinH720 = 520
    }
}

function Get-QemuRelativeMouseMoves {
    param(
        [int] $TargetX,
        [int] $TargetY,
        [int] $FromX = 0,
        [int] $FromY = 0,
        [int] $Step = 80
    )

    $Lines = [System.Collections.Generic.List[string]]::new()
    $RemainingX = $TargetX - $FromX
    $RemainingY = $TargetY - $FromY
    while ($RemainingX -ne 0 -or $RemainingY -ne 0) {
        $DeltaX = $RemainingX
        if ($DeltaX -gt $Step) { $DeltaX = $Step }
        if ($DeltaX -lt -$Step) { $DeltaX = -$Step }
        $DeltaY = $RemainingY
        if ($DeltaY -gt $Step) { $DeltaY = $Step }
        if ($DeltaY -lt -$Step) { $DeltaY = -$Step }
        $Lines.Add("mouse_move $DeltaX $DeltaY") | Out-Null
        $RemainingX -= $DeltaX
        $RemainingY -= $DeltaY
    }
    return $Lines.ToArray()
}

function Get-LeonOsInitialMousePosition {
    param(
        [int] $Width,
        [int] $Height
    )

    return [pscustomobject] @{
        X = [int]($Width / 2)
        Y = [int]($Height / 2)
    }
}

function Move-QemuMouseTo {
    param(
        [System.IO.StreamWriter] $Writer,
        [int] $TargetX,
        [int] $TargetY,
        [int] $Step = 80,
        [int] $FromX = 0,
        [int] $FromY = 0
    )

    foreach ($Line in (Get-QemuRelativeMouseMoves $TargetX $TargetY -FromX $FromX -FromY $FromY -Step $Step)) {
        $Writer.WriteLine($Line)
        Start-Sleep -Milliseconds 40
    }
}

function Read-Ppm {
    param([string] $Path)

    $Bytes = [System.IO.File]::ReadAllBytes($Path)
    $Index = 0

    function Next-Token {
        param([byte[]] $Data, [ref] $Cursor)

        while ($Cursor.Value -lt $Data.Length) {
            $Char = [char] $Data[$Cursor.Value]
            if ([char]::IsWhiteSpace($Char)) {
                $Cursor.Value += 1
                continue
            }
            if ($Char -eq "#") {
                while ($Cursor.Value -lt $Data.Length -and $Data[$Cursor.Value] -ne 10) {
                    $Cursor.Value += 1
                }
                continue
            }
            break
        }

        $Start = $Cursor.Value
        while ($Cursor.Value -lt $Data.Length -and -not [char]::IsWhiteSpace([char] $Data[$Cursor.Value])) {
            $Cursor.Value += 1
        }

        return [System.Text.Encoding]::ASCII.GetString($Data, $Start, $Cursor.Value - $Start)
    }

    $Magic = Next-Token $Bytes ([ref] $Index)
    if ($Magic -ne "P6") {
        throw "Unsupported PPM magic '$Magic'."
    }

    $PpmWidth = [int] (Next-Token $Bytes ([ref] $Index))
    $PpmHeight = [int] (Next-Token $Bytes ([ref] $Index))
    $Max = [int] (Next-Token $Bytes ([ref] $Index))
    if ($Max -ne 255) {
        throw "Unsupported PPM max value '$Max'."
    }

    # P6 has exactly one whitespace separator after maxval; pixel bytes can
    # legally look like ASCII whitespace, so do not skip through the payload.
    if ($Index -lt $Bytes.Length -and [char]::IsWhiteSpace([char] $Bytes[$Index])) {
        $Index += 1
    }

    return [pscustomobject] @{
        Bytes = $Bytes
        Width = $PpmWidth
        Height = $PpmHeight
        DataOffset = $Index
    }
}

function Get-Pixel {
    param(
        [pscustomobject] $Ppm,
        [int] $X,
        [int] $Y
    )

    $Offset = $Ppm.DataOffset + (($Y * $Ppm.Width + $X) * 3)
    return [pscustomobject] @{
        R = [int] $Ppm.Bytes[$Offset]
        G = [int] $Ppm.Bytes[$Offset + 1]
        B = [int] $Ppm.Bytes[$Offset + 2]
    }
}

function Assert-PixelNear {
    param(
        [pscustomobject] $Ppm,
        [int] $X,
        [int] $Y,
        [int] $R,
        [int] $G,
        [int] $B,
        [int] $Tolerance,
        [string] $Name,
        [switch] $Loose
    )

    $Pixel = Get-Pixel $Ppm $X $Y
    $Tol = if ($Loose) { [Math]::Max($Tolerance, 40) } else { $Tolerance }
    if ([Math]::Abs($Pixel.R - $R) -gt $Tol -or
        [Math]::Abs($Pixel.G - $G) -gt $Tol -or
        [Math]::Abs($Pixel.B - $B) -gt $Tol) {
        throw "$Name pixel at ($X,$Y) was RGB($($Pixel.R),$($Pixel.G),$($Pixel.B)); expected near RGB($R,$G,$B)."
    }
}

function Count-DarkPixels {
    param(
        [pscustomobject] $Ppm,
        [int] $X,
        [int] $Y,
        [int] $Width,
        [int] $Height
    )

    Add-LeonOsVisualNativeHelpers
    return [LeonOsVisual.PpmNative]::CountDarkPixels(
        $Ppm.Bytes,
        $Ppm.DataOffset,
        $Ppm.Width,
        $Ppm.Height,
        $X,
        $Y,
        $Width,
        $Height)
}

function Add-LeonOsVisualNativeHelpers {
    if ("LeonOsVisual.PpmNative" -as [type]) {
        return
    }

    Add-Type -TypeDefinition @"
using System;
using System.IO;

namespace LeonOsVisual
{
    public static class PpmNative
    {
        public static int CountDarkPixels(
            byte[] ppmBytes,
            int dataOffset,
            int sourceWidth,
            int sourceHeight,
            int x,
            int y,
            int width,
            int height)
        {
            ValidatePpm(ppmBytes, dataOffset, sourceWidth, sourceHeight);
            if (x < 0 || y < 0 || width < 0 || height < 0 ||
                x + width > sourceWidth || y + height > sourceHeight)
            {
                throw new ArgumentOutOfRangeException("Requested pixel rectangle is outside the PPM image.");
            }

            int count = 0;
            int sourceStride = sourceWidth * 3;
            int rowOffset = dataOffset + ((y * sourceWidth + x) * 3);
            for (int row = 0; row < height; row++)
            {
                int offset = rowOffset;
                for (int col = 0; col < width; col++)
                {
                    if (ppmBytes[offset] + ppmBytes[offset + 1] + ppmBytes[offset + 2] < 120)
                    {
                        count++;
                    }
                    offset += 3;
                }
                rowOffset += sourceStride;
            }
            return count;
        }

        public static void SaveScaledPng(
            byte[] ppmBytes,
            int dataOffset,
            int sourceWidth,
            int sourceHeight,
            string path,
            int outWidth,
            int outHeight)
        {
            ValidatePpm(ppmBytes, dataOffset, sourceWidth, sourceHeight);
            if (String.IsNullOrEmpty(path))
            {
                throw new ArgumentException("Output path is required.", "path");
            }
            if (outWidth <= 0 || outHeight <= 0)
            {
                throw new ArgumentOutOfRangeException("Scaled PNG dimensions must be positive.");
            }

            // Nearest-neighbour scale into filter-prefixed PNG scanlines.
            int[] sourceXOffsets = new int[outWidth];
            for (int x = 0; x < outWidth; x++)
            {
                sourceXOffsets[x] = ((int)(((long)x * sourceWidth) / outWidth)) * 3;
            }

            int outStride = 1 + outWidth * 3;
            byte[] raw = new byte[outStride * outHeight];
            int sourceStride = sourceWidth * 3;
            for (int y = 0; y < outHeight; y++)
            {
                int sourceY = (int)(((long)y * sourceHeight) / outHeight);
                int sourceRow = dataOffset + (sourceY * sourceStride);
                int dest = y * outStride;
                raw[dest++] = 0; // filter: none
                for (int x = 0; x < outWidth; x++)
                {
                    int source = sourceRow + sourceXOffsets[x];
                    raw[dest] = ppmBytes[source];
                    raw[dest + 1] = ppmBytes[source + 1];
                    raw[dest + 2] = ppmBytes[source + 2];
                    dest += 3;
                }
            }

            // Written as a minimal managed PNG (zlib stored blocks), so no
            // System.Drawing dependency is needed on any platform.
            using (FileStream file = new FileStream(path, FileMode.Create, FileAccess.Write))
            {
                byte[] signature = { 137, 80, 78, 71, 13, 10, 26, 10 };
                file.Write(signature, 0, signature.Length);

                byte[] ihdr = new byte[13];
                WriteBigEndian(ihdr, 0, outWidth);
                WriteBigEndian(ihdr, 4, outHeight);
                ihdr[8] = 8;  // bit depth
                ihdr[9] = 2;  // color type: truecolor
                WriteChunk(file, "IHDR", ihdr);

                WriteChunk(file, "IDAT", ZlibStore(raw));
                WriteChunk(file, "IEND", new byte[0]);
            }
        }

        private static void WriteBigEndian(byte[] buffer, int offset, int value)
        {
            buffer[offset] = (byte)((value >> 24) & 0xFF);
            buffer[offset + 1] = (byte)((value >> 16) & 0xFF);
            buffer[offset + 2] = (byte)((value >> 8) & 0xFF);
            buffer[offset + 3] = (byte)(value & 0xFF);
        }

        private static void WriteChunk(Stream stream, string type, byte[] data)
        {
            byte[] header = new byte[8];
            WriteBigEndian(header, 0, data.Length);
            for (int i = 0; i < 4; i++)
            {
                header[4 + i] = (byte)type[i];
            }
            stream.Write(header, 0, 8);
            stream.Write(data, 0, data.Length);

            uint crc = Crc32(header, 4, 4, 0xFFFFFFFF);
            crc = Crc32(data, 0, data.Length, crc) ^ 0xFFFFFFFF;
            byte[] crcBytes = new byte[4];
            WriteBigEndian(crcBytes, 0, (int)crc);
            stream.Write(crcBytes, 0, 4);
        }

        private static uint[] crcTable;

        private static uint Crc32(byte[] data, int offset, int count, uint crc)
        {
            if (crcTable == null)
            {
                crcTable = new uint[256];
                for (uint n = 0; n < 256; n++)
                {
                    uint c = n;
                    for (int k = 0; k < 8; k++)
                    {
                        c = (c & 1) != 0 ? 0xEDB88320 ^ (c >> 1) : c >> 1;
                    }
                    crcTable[n] = c;
                }
            }
            for (int i = 0; i < count; i++)
            {
                crc = crcTable[(crc ^ data[offset + i]) & 0xFF] ^ (crc >> 8);
            }
            return crc;
        }

        private static byte[] ZlibStore(byte[] raw)
        {
            const int blockMax = 65535;
            int blocks = (raw.Length + blockMax - 1) / blockMax;
            if (blocks == 0)
            {
                blocks = 1;
            }
            byte[] output = new byte[2 + blocks * 5 + raw.Length + 4];
            int pos = 0;
            output[pos++] = 0x78; // zlib header: deflate, 32K window
            output[pos++] = 0x01;

            int remaining = raw.Length;
            int source = 0;
            for (int block = 0; block < blocks; block++)
            {
                int len = remaining > blockMax ? blockMax : remaining;
                output[pos++] = (byte)(block == blocks - 1 ? 1 : 0);
                output[pos++] = (byte)(len & 0xFF);
                output[pos++] = (byte)((len >> 8) & 0xFF);
                output[pos++] = (byte)(~len & 0xFF);
                output[pos++] = (byte)((~len >> 8) & 0xFF);
                Array.Copy(raw, source, output, pos, len);
                pos += len;
                source += len;
                remaining -= len;
            }

            uint a = 1, b = 0;
            for (int i = 0; i < raw.Length; i++)
            {
                a = (a + raw[i]) % 65521;
                b = (b + a) % 65521;
            }
            uint adler = (b << 16) | a;
            WriteBigEndian(output, pos, (int)adler);
            return output;
        }

        private static void ValidatePpm(byte[] ppmBytes, int dataOffset, int sourceWidth, int sourceHeight)
        {
            if (ppmBytes == null)
            {
                throw new ArgumentNullException("ppmBytes");
            }
            if (dataOffset < 0 || sourceWidth <= 0 || sourceHeight <= 0)
            {
                throw new ArgumentOutOfRangeException("Invalid PPM metadata.");
            }

            long payloadLength = (long)sourceWidth * sourceHeight * 3;
            if ((long)dataOffset + payloadLength > ppmBytes.LongLength)
            {
                throw new ArgumentException("PPM pixel payload is shorter than the declared dimensions.");
            }
        }
    }
}
"@
}

function Save-ScaledPng {
    param(
        [pscustomobject] $Ppm,
        [string] $Path,
        [int] $OutWidth = 960,
        [int] $OutHeight = 540
    )

    Add-LeonOsVisualNativeHelpers
    [LeonOsVisual.PpmNative]::SaveScaledPng(
        $Ppm.Bytes,
        $Ppm.DataOffset,
        $Ppm.Width,
        $Ppm.Height,
        $Path,
        $OutWidth,
        $OutHeight)
}

function Assert-LeonOsVisualCapture {
    param(
        [pscustomobject] $Ppm,
        [pscustomobject] $Expect,
        [int] $ExpectedWidth,
        [int] $ExpectedHeight
    )

    if ($Ppm.Width -ne $ExpectedWidth -or $Ppm.Height -ne $ExpectedHeight) {
        throw "Expected ${ExpectedWidth}x${ExpectedHeight} capture; got $($Ppm.Width)x$($Ppm.Height)."
    }

    Assert-PixelNear $Ppm $Expect.Wallpaper.X $Expect.Wallpaper.Y `
        $Expect.Wallpaper.R $Expect.Wallpaper.G $Expect.Wallpaper.B 8 "Wallpaper" -Loose
    Assert-PixelNear $Ppm $Expect.TitleBar.X $Expect.TitleBar.Y `
        $Expect.TitleBar.R $Expect.TitleBar.G $Expect.TitleBar.B 8 "Window title bar"
    Assert-PixelNear $Ppm $Expect.CloseButton.X $Expect.CloseButton.Y `
        $Expect.CloseButton.R $Expect.CloseButton.G $Expect.CloseButton.B 8 "Close button"
    Assert-PixelNear $Ppm $Expect.Sidebar.X $Expect.Sidebar.Y `
        $Expect.Sidebar.R $Expect.Sidebar.G $Expect.Sidebar.B 8 "Sidebar"
    Assert-PixelNear $Ppm $Expect.ContentPane.X $Expect.ContentPane.Y `
        $Expect.ContentPane.R $Expect.ContentPane.G $Expect.ContentPane.B 8 "Content pane"
    Assert-PixelNear $Ppm $Expect.StartButton.X $Expect.StartButton.Y `
        $Expect.StartButton.R $Expect.StartButton.G $Expect.StartButton.B 12 "Start button" -Loose
    Assert-PixelNear $Ppm $Expect.StartSpriteIcon.X $Expect.StartSpriteIcon.Y `
        $Expect.StartSpriteIcon.R $Expect.StartSpriteIcon.G $Expect.StartSpriteIcon.B 40 "Start sprite icon" -Loose
    Assert-PixelNear $Ppm $Expect.TitlebarCloseSprite.X $Expect.TitlebarCloseSprite.Y `
        $Expect.TitlebarCloseSprite.R $Expect.TitlebarCloseSprite.G $Expect.TitlebarCloseSprite.B 56 "Titlebar close sprite" -Loose
    Assert-PixelNear $Ppm $Expect.Taskbar.X $Expect.Taskbar.Y `
        $Expect.Taskbar.R $Expect.Taskbar.G $Expect.Taskbar.B 12 "Taskbar"
    Assert-PixelNear $Ppm $Expect.TabActive.X $Expect.TabActive.Y `
        $Expect.TabActive.R $Expect.TabActive.G $Expect.TabActive.B 8 "Active tab"

    $TitleDark = Count-DarkPixels $Ppm $Expect.TitleTextBox.X $Expect.TitleTextBox.Y `
        $Expect.TitleTextBox.Width $Expect.TitleTextBox.Height
    if ($TitleDark -lt $Expect.TitleTextBox.MinDark) {
        throw "Title text area has too few dark pixels; text may not be rendering."
    }

    if ($Expect.ContentTextBox.MinDark -gt 0) {
        $ContentDark = Count-DarkPixels $Ppm $Expect.ContentTextBox.X $Expect.ContentTextBox.Y `
            $Expect.ContentTextBox.Width $Expect.ContentTextBox.Height
        if ($ContentDark -lt $Expect.ContentTextBox.MinDark) {
            throw "Content text area has too few dark pixels; preview text may not be rendering."
        }
    }

    if ($ExpectedHeight -le 720) {
        if ($Ppm.Width -lt 1280 -or $Ppm.Height -lt 720) {
            throw "720p capture size unexpected."
        }
        if ($Expect.WinW -lt $Expect.MinWinW720) {
            throw "720p default window is too narrow ($($Expect.WinW) px)."
        }
        if ($Expect.WinH -lt $Expect.MinWinH720) {
            throw "720p default window is too short ($($Expect.WinH) px)."
        }
    }

    if ($ExpectedHeight -le 720 -and $Expect.OverlapGuard) {
        $SpillDark = Count-DarkPixels $Ppm $Expect.OverlapGuard.SpillX $Expect.OverlapGuard.ListY `
            $Expect.OverlapGuard.SpillW $Expect.OverlapGuard.ListH
        if ($SpillDark -gt $Expect.OverlapGuard.MaxDark) {
            throw "720p overlap guard failed: file-list text spilled into preview pane ($SpillDark dark pixels)."
        }
        $ListDark = Count-DarkPixels $Ppm $Expect.OverlapGuard.ListX $Expect.OverlapGuard.ListY `
            $Expect.OverlapGuard.ListW $Expect.OverlapGuard.ListH
        if ($ListDark -lt 20) {
            throw "720p file list area has too few dark pixels; list may not be rendering."
        }
    }

    if ($Expect.CursorBox.MinDark -gt 0) {
        $CursorDark = Count-DarkPixels $Ppm $Expect.CursorBox.X $Expect.CursorBox.Y `
            $Expect.CursorBox.Width $Expect.CursorBox.Height
        if ($CursorDark -lt $Expect.CursorBox.MinDark) {
            throw "Cursor outline pixels were not detected near the expected mouse location."
        }
    }
}
