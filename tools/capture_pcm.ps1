param(
    [string]$Port = "COM9",
    [int]$Baud = 115200,
    [string]$Out = "capture_8192_s16le.pcm",
    [int]$TimeoutSeconds = 60,
    [switch]$Reset,
    [string]$ProgrammerCli = "C:\ProgramData\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
)

$serial = [System.IO.Ports.SerialPort]::new($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.ReadTimeout = 200
$serial.DtrEnable = $true
$serial.RtsEnable = $true

$bytes = New-Object System.Collections.Generic.List[byte]
$line = ""
$started = $false
$done = $false
$rate = $null
$samples = $null

function Add-HexSamples {
    param([string]$HexLine)

    $hex = $HexLine.Trim()
    if (($hex.Length % 4) -ne 0) {
        throw "Bad PCM hex line length: $($hex.Length)"
    }

    for ($i = 0; $i -lt $hex.Length; $i += 4) {
        $word = [Convert]::ToUInt16($hex.Substring($i, 4), 16)
        $bytes.Add([byte]($word -band 0xff))
        $bytes.Add([byte](($word -shr 8) -band 0xff))
    }
}

try {
    $serial.Open()
    Start-Sleep -Milliseconds 300
    $serial.DiscardInBuffer()
    if ($Reset) {
        Write-Host "Resetting target over SWD..."
        & $ProgrammerCli -c port=SWD -rst | Out-Null
        Start-Sleep -Milliseconds 500
        $serial.DiscardInBuffer()
    }
    Write-Host "Waiting for [pcm16] dump on $Port..."

    $start = Get-Date
    while (-not $done -and ((Get-Date) - $start).TotalSeconds -lt $TimeoutSeconds) {
        $chunk = $serial.ReadExisting()
        if ($chunk.Length -eq 0) {
            Start-Sleep -Milliseconds 20
            continue
        }

        foreach ($ch in $chunk.ToCharArray()) {
            if ($ch -eq "`n") {
                $clean = $line.Trim("`r")
                if ($clean -match '^\[pcm16\] begin rate=(\d+) samples=(\d+)') {
                    $started = $true
                    $rate = [int]$Matches[1]
                    $samples = [int]$Matches[2]
                    Write-Host "PCM begin: rate=$rate samples=$samples"
                } elseif ($started -and $clean -match '^\[pcm16\] data ([0-9A-Fa-f]+)$') {
                    Add-HexSamples $Matches[1]
                } elseif ($started -and $clean -match '^\[pcm16\] end$') {
                    $done = $true
                    break
                } elseif ($clean.Length -gt 0) {
                    Write-Host $clean
                }
                $line = ""
            } elseif ($ch -ne "`r") {
                $line += $ch
            }
        }
    }
} finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
}

if (-not $done) {
    throw "Timed out before [pcm16] end"
}

$outPath = $Out
if (-not [System.IO.Path]::IsPathRooted($outPath)) {
    $outPath = Join-Path (Resolve-Path .).Path $outPath
}
[System.IO.File]::WriteAllBytes($outPath, $bytes.ToArray())
Write-Host "Wrote $Out ($($bytes.Count) bytes, $rate Hz, signed 16-bit little-endian mono)"
