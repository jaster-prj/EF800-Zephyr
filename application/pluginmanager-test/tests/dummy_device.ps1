param(
    [string]$Port = "COM7",
    [ValidateSet("elta3", "fts3xx")]
    [string]$Instrument = "elta3"
)

$ErrorActionPreference = "Stop"
if ($Instrument -eq "elta3") {
    $serial = [System.IO.Ports.SerialPort]::new($Port, 1200,
        [System.IO.Ports.Parity]::Odd, 7, [System.IO.Ports.StopBits]::Two)
} else {
    $serial = [System.IO.Ports.SerialPort]::new($Port, 9600,
        [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
}
$serial.ReadTimeout = 100
$serial.Open()
$serial.DiscardInBuffer()
$pending = $false
$line = New-Object System.Collections.Generic.List[byte]
function Write-Bytes([byte[]]$bytes) {
    foreach ($byte in $bytes) {
        $serial.Write([byte[]]$byte, 0, 1)
        Start-Sleep -Milliseconds 2
    }
}
try {
    while ($true) {
        try { $byte = $serial.ReadByte() } catch [TimeoutException] { continue }
        if ($Instrument -eq "fts3xx" -and $byte -eq 0x11) {
            Write-Output "RX: 17"
            Write-Output "TX: measurement"
            $response = [Text.Encoding]::ASCII.GetBytes("1234 0452700 0900000`r`n")
            Write-Output ("TX bytes: " + (($response | ForEach-Object { $_.ToString("x2") }) -join " "))
            Write-Bytes $response
            continue
        }
        if ($byte -eq 13) {
            $received = $line.ToArray()
            $text = [Text.Encoding]::ASCII.GetString($line.ToArray())
            $line.Clear()
            Write-Output ("RX: " + (($text.ToCharArray() | ForEach-Object { [int][char]$_ }) -join ","))
        } elseif ($byte -eq 10) {
            continue
        } else {
            $line.Add([byte]$byte)
            continue
        }
        if ($Instrument -eq "elta3") {
            switch ($text) {
                "S14" { Write-Output "TX: A"; Write-Bytes ([byte[]](0x41, 0x0d, 0x0a)); $pending = $true }
                "B" { if ($pending) { Write-Output "TX: measurement"; Write-Bytes ([Text.Encoding]::ASCII.GetBytes("#48 0 1.234; 100.0000; 50.5000`r`n")) } }
                "Q" { $pending = $false }
            }
        } elseif ($received.Count -ge 1 -and $received[0] -eq 0x11) {
            Write-Bytes ([Text.Encoding]::ASCII.GetBytes("1234 0452700 0900000`r`n"))
        }
    }
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
