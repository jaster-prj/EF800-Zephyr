param(
    [string]$Port = "COM7",
    [int]$BaudRate = 115200
)

$ErrorActionPreference = "Stop"
$serial = [System.IO.Ports.SerialPort]::new($Port, $BaudRate, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.ReadTimeout = 250

try {
    $serial.Open()
    $serial.DiscardInBuffer()
    $serial.WriteLine("calc demo 5")
    Start-Sleep -Milliseconds 500
    $response = $serial.ReadExisting()
    if ($response -notmatch "105\s+-15") {
        throw "Unexpected calculation response: $response"
    }
    Write-Output "PASS: bounded calculation service returned 105 / -15"
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
