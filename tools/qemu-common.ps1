Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-LeonOsQemu {
    foreach ($Name in @("qemu-system-i386.exe", "qemu-system-i386")) {
        $Command = Get-Command $Name -ErrorAction SilentlyContinue
        if ($Command) {
            return $Command.Source
        }
    }

    $Candidates = @(
        "C:\Program Files\qemu\qemu-system-i386.exe",
        "C:\Program Files (x86)\qemu\qemu-system-i386.exe",
        "C:\qemu\qemu-system-i386.exe",
        "C:\tools\qemu\qemu-system-i386.exe"
    )

    foreach ($Candidate in $Candidates) {
        if (Test-Path -LiteralPath $Candidate) {
            return $Candidate
        }
    }

    throw @"
qemu-system-i386.exe was not found.

Install QEMU, then retry:
  winget install --id SoftwareFreedomConservancy.QEMU --exact --accept-source-agreements --accept-package-agreements
"@
}

function Get-LeonOsImagePath {
    param([string] $Image)

    $Root = Split-Path -Parent $PSScriptRoot
    if ([System.IO.Path]::IsPathRooted($Image)) {
        $ImagePath = $Image
    } else {
        $ImagePath = Join-Path $Root $Image
    }

    if (-not (Test-Path -LiteralPath $ImagePath)) {
        throw "Image was not found at '$ImagePath'. Build first with tools\build.ps1."
    }

    return (Resolve-Path -LiteralPath $ImagePath).Path
}

function Join-QemuArguments {
    param([string[]] $Arguments)

    return ($Arguments | ForEach-Object {
        if ($_ -match '[\s"]') {
            '"' + ($_ -replace '"', '\"') + '"'
        } else {
            $_
        }
    }) -join " "
}

function Get-LeonOsSerialLogPath {
    param([string] $Name = "qemu-serial")

    $Root = Split-Path -Parent $PSScriptRoot
    $Dir = Join-Path $Root "dist32"
    New-Item -ItemType Directory -Path $Dir -Force | Out-Null
    return (Join-Path $Dir "$Name.log")
}

function Get-LeonOsQemuSerialFileArg {
    param([string] $LogPath)

    if (-not [System.IO.Path]::IsPathRooted($LogPath)) {
        $Root = Split-Path -Parent $PSScriptRoot
        $LogPath = Join-Path $Root $LogPath
    }
    $ForQemu = ([System.IO.Path]::GetFullPath($LogPath)) -replace '\\', '/'
    return "file:$ForQemu"
}

function Read-LeonOsSerialLog {
    param([string] $LogPath)

    if (-not (Test-Path -LiteralPath $LogPath)) {
        return ""
    }
    try {
        $Stream = [System.IO.FileStream]::new(
            $LogPath,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::ReadWrite)
        try {
            $Buffer = New-Object byte[] ([Math]::Max(0, $Stream.Length))
            if ($Buffer.Length -eq 0) {
                return ""
            }
            [void] $Stream.Read($Buffer, 0, $Buffer.Length)
            return ([System.Text.Encoding]::ASCII.GetString($Buffer) -replace "`0", "")
        } finally {
            $Stream.Dispose()
        }
    } catch {
        return ""
    }
}

function Wait-QemuMonitorPrompt {
    param(
        [System.IO.Stream] $Stream,
        [int] $TimeoutMs = 3000
    )

    $Deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    $Buffer = New-Object byte[] 8192
    $Collected = New-Object System.Text.StringBuilder
    while ((Get-Date) -lt $Deadline) {
        if ($Stream.DataAvailable) {
            $Read = $Stream.Read($Buffer, 0, $Buffer.Length)
            if ($Read -le 0) {
                break
            }
            [void] $Collected.Append([System.Text.Encoding]::ASCII.GetString($Buffer, 0, $Read))
            if ($Collected.ToString() -match '\(qemu\)') {
                return
            }
        } else {
            Start-Sleep -Milliseconds 30
        }
    }
}

function Wait-LeonOsSerialLog {
    param(
        [string] $LogPath,
        [string] $Pattern,
        [int] $TimeoutMs = 45000
    )

    $Deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $Deadline) {
        $Text = Read-LeonOsSerialLog $LogPath
        if ($Text -like "*$Pattern*") {
            return $Text
        }
        Start-Sleep -Milliseconds 200
    }
    return Read-LeonOsSerialLog $LogPath
}

