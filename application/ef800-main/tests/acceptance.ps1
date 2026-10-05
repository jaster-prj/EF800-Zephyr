param(
    [string]$Port = "COM7",
    [int]$TimeoutSeconds = 8
)

$ErrorActionPreference = "Stop"
$serial = [System.IO.Ports.SerialPort]::new($Port, 115200,
    [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.ReadTimeout = 250
$serial.WriteTimeout = 1000

function Read-Until {
    param([string]$Pattern, [string]$Description)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $text = ""
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($serial.BytesToRead -gt 0) {
            $text += $serial.ReadExisting()
            if ($text -match $Pattern) { return $text }
        } else {
            Start-Sleep -Milliseconds 50
        }
    }
    throw "Timed out waiting for $Description. Received: $text"
}

try {
    $serial.Open()
    $serial.DiscardInBuffer()
    $serial.WriteLine("")
    $startup = Read-Until -Pattern "uart:~\$|ef800 main application" -Description "EF800 MainApplication startup"
    $serial.WriteLine("plugin status")
    $status = Read-Until -Pattern "plugin|storage|Unknown command" -Description "production plugin status"
    Write-Output $startup
    Write-Output $status
    if ($status -match "mounted=no") {
        Write-Output "BLOCKED: EF800 internal storage is unavailable"
        exit 2
    }
    Write-Output "PASS: production MainApplication acceptance console is reachable"
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
