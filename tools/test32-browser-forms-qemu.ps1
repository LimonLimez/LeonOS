param(
    [ValidateSet("720p", "1080p")]
    [string] $Resolution = "1080p",
    [int] $TimeoutSeconds = 150
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qemu-common.ps1")
. (Join-Path $PSScriptRoot "visual-common.ps1")

function Wait-SerialPattern {
    param(
        [string] $LogPath,
        [string] $Pattern,
        [int] $TimeoutMs
    )

    $Text = Wait-LeonOsSerialLog $LogPath $Pattern $TimeoutMs
    if (-not $Text.Contains($Pattern)) {
        throw "Timed out waiting for serial proof: $Pattern"
    }
    return $Text
}

function Wait-SerialNewPattern {
    param(
        [string] $LogPath,
        [string] $Pattern,
        [int] $StartLength,
        [int] $TimeoutMs
    )

    $Deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $Deadline) {
        $Text = Read-LeonOsSerialLog $LogPath
        if ($Text.Length -gt $StartLength) {
            $Tail = $Text.Substring($StartLength)
            if ($Tail.Contains($Pattern)) {
                return $Text
            }
        }
        Start-Sleep -Milliseconds 200
    }
    throw "Timed out waiting for new serial proof: $Pattern"
}

& (Join-Path $PSScriptRoot "build32-image.ps1") -Resolution $Resolution | Write-Host
& (Join-Path $PSScriptRoot "build32-hdd.ps1") -ExcludeNetSurf | Write-Host

$Qemu = Get-LeonOsQemu
$ImagePath = (Get-LeonOsImagePath "dist32\leonos32.img") -replace '"', '\"'
$HddPath = (Get-LeonOsImagePath "dist32\leonos32-hdd.img") -replace '"', '\"'
$SerialLog = Get-LeonOsSerialLogPath "test32-browser-forms-$Resolution"
if (Test-Path -LiteralPath $SerialLog) {
    Remove-Item -LiteralPath $SerialLog -Force
}
$SerialArg = Get-LeonOsQemuSerialFileArg $SerialLog

$Listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$Listener.Start()
$MonitorPort = ([System.Net.IPEndPoint] $Listener.LocalEndpoint).Port
$Listener.Stop()

$StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
$StartInfo.FileName = $Qemu
$StartInfo.Arguments = "-name LeonOS-BrowserForms-$Resolution -machine pc -cpu qemu32 -m 32M -drive file=`"$ImagePath`",format=raw,if=floppy -drive file=`"$HddPath`",format=raw,if=ide,index=0,media=disk -boot a -vga std -display none -serial $SerialArg -monitor tcp:127.0.0.1:$MonitorPort,server,nowait -nic user,model=rtl8139 -no-reboot"
$StartInfo.UseShellExecute = $false
$StartInfo.RedirectStandardError = $true

$Process = [System.Diagnostics.Process]::Start($StartInfo)
$Client = $null
$Writer = $null

try {
    for ($Attempt = 0; $Attempt -lt 40 -and -not $Client; $Attempt += 1) {
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

    $Stream = $Client.GetStream()
    $Writer = [System.IO.StreamWriter]::new($Stream)
    $Writer.AutoFlush = $true
    Wait-QemuMonitorPrompt $Stream 8000

    $null = Wait-SerialPattern $SerialLog "LeonOS shell ready" 60000
    $null = Wait-SerialPattern $SerialLog "LeonOS net ARP DNS resolved" 60000
    $Writer.WriteLine("sendkey f11")
    Wait-QemuMonitorPrompt $Stream 3000
    $null = Wait-SerialPattern $SerialLog "LeonOS shell app=net" 10000
    $Writer.WriteLine("sendkey h")
    Wait-QemuMonitorPrompt $Stream 3000

    $null = Wait-SerialPattern $SerialLog "LeonOS net browser URL open https://www.google.com/?igu=1&hl=en" 15000
    $null = Wait-SerialPattern $SerialLog "LeonOS net HTTPS status" 60000
    $null = Wait-SerialPattern $SerialLog "LeonOS net browser response complete" 90000
    $null = Wait-SerialPattern $SerialLog "LeonOS net browser HTML structure" 60000
    $null = Wait-SerialPattern $SerialLog "LeonOS net browser form model" 60000
    $null = Wait-SerialPattern $SerialLog "LeonOS net browser layout boxes" 60000

    $Writer.WriteLine("sendkey tab")
    Wait-QemuMonitorPrompt $Stream 3000
    $null = Wait-SerialPattern $SerialLog "LeonOS net browser form focus" 10000

    foreach ($Key in @("l", "e", "o", "n", "o", "s")) {
        $Writer.WriteLine("sendkey $Key")
        Wait-QemuMonitorPrompt $Stream 1000
        Start-Sleep -Milliseconds 40
    }
    $null = Wait-SerialPattern $SerialLog "LeonOS net browser form edit" 10000

    $SubmitStartLength = (Read-LeonOsSerialLog $SerialLog).Length
    $Writer.WriteLine("sendkey ret")
    Wait-QemuMonitorPrompt $Stream 3000
    $null = Wait-SerialNewPattern $SerialLog "LeonOS net browser form submit" $SubmitStartLength 10000
    $null = Wait-SerialNewPattern $SerialLog "LeonOS net browser URL open https://www.google.com/search?q=leonos" $SubmitStartLength 30000
    $null = Wait-SerialNewPattern $SerialLog "LeonOS net HTTPS status" $SubmitStartLength 60000
    $AfterSubmitText = Wait-SerialNewPattern $SerialLog "LeonOS net browser response complete" $SubmitStartLength 90000

    $Writer.WriteLine("quit")
    $Writer.Dispose()
    $Writer = $null
    $Client.Close()
    $Client = $null

    $Exited = $Process.WaitForExit($TimeoutSeconds * 1000)
    if (-not $Exited) {
        $Process.Kill()
        $Process.WaitForExit()
    }

    $Stderr = $Process.StandardError.ReadToEnd()
    if ($Stderr.Trim().Length -gt 0) {
        Write-Host "QEMU stderr (LeonOS-BrowserForms-$Resolution):"
        Write-Host $Stderr
    }
} finally {
    if ($Writer) { $Writer.Dispose() }
    if ($Client) { $Client.Close() }
    if (-not $Process.HasExited) {
        $Process.Kill()
        $Process.WaitForExit()
    }
}

$Output = Read-LeonOsSerialLog $SerialLog
Write-Host $Output

$Expected = @(
    "LeonOS net browser URL open https://www.google.com/?igu=1&hl=en",
    "LeonOS net browser HTML structure",
    "LeonOS net browser form model",
    "LeonOS net browser layout boxes",
    "LeonOS net browser form focus",
    "LeonOS net browser form edit",
    "LeonOS net browser form submit",
    "LeonOS net browser URL open https://www.google.com/search?q=leonos",
    "LeonOS net browser response complete"
)
foreach ($Line in $Expected) {
    if (-not $Output.Contains($Line)) {
        throw "Missing browser form serial proof: $Line"
    }
}

$FormMatch = [regex]::Match(
    $Output,
    'LeonOS net browser form model forms\s+(?<Forms>\d+) controls (?<Controls>\d+) focusable (?<Focusable>\d+) editable (?<Editable>\d+) hidden (?<Hidden>\d+) submit (?<Submit>\d+)')
if (-not $FormMatch.Success) {
    throw "Missing parsed form model counters."
}
foreach ($Name in @("Forms", "Controls", "Focusable", "Editable")) {
    if ([int] $FormMatch.Groups[$Name].Value -le 0) {
        throw "Expected form model $Name to be greater than zero."
    }
}

if ($Output -notmatch 'LeonOS net browser form focus \d+ name q') {
    throw "The focused real Google form control was not name q."
}
if ($Output -notmatch 'LeonOS net browser form edit \d+ value leonos') {
    throw "The Google q control did not retain typed text 'leonos'."
}
if ($Output -notmatch 'LeonOS net browser form submit \d+ url https://www\.google\.com/search\?q=leonos') {
    throw "The real Google GET form did not submit q=leonos."
}
$SubmitTail = $AfterSubmitText.Substring($SubmitStartLength)
$CompleteMatch = [regex]::Match(
    $SubmitTail,
    'LeonOS net browser response complete\s+\S+\s+decoded\s+(?<Bytes>\d+)')
if (-not $CompleteMatch.Success) {
    throw "Missing post-submit decoded response completion."
}
if ([int] $CompleteMatch.Groups["Bytes"].Value -lt 3000) {
    throw "Post-submit Google search decoded only $($CompleteMatch.Groups["Bytes"].Value) bytes."
}

foreach ($Bad in @("CPU exception", "PANIC", "unhandled")) {
    if ($Output -like "*$Bad*") {
        throw "Browser forms test hit fault: $Bad"
    }
}

Write-Host "QEMU browser forms test passed: real Google q field edited and submitted ($Resolution)."
Write-Host "Serial log: $SerialLog"
