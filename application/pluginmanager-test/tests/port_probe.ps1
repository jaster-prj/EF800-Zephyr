param([string]$Port = "COM14", [switch]$Fennel)
$ErrorActionPreference = "Stop"
$serial = [System.IO.Ports.SerialPort]::new($Port, 115200,
    [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
try {
    $serial.Open()
    $serial.WriteLine($(if ($Fennel) { "port probe-fennel" } else { "port probe" }))
    Start-Sleep -Seconds 3
    Write-Output $serial.ReadExisting()
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
